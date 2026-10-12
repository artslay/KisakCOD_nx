#ifdef __SWITCH__

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <cmath>
#include <vector>

#include <AL/al.h>
#include <AL/alc.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "binklib/bink.h"

extern void Switch_LogWrite(const char *msg);

struct SwitchBinkState
{
    AVFormatContext *format;
    AVIOContext *memoryIo;
    const uint8_t *memoryData;
    int64_t memorySize;
    int64_t memoryPosition;
    AVCodecContext *codec;
    AVStream *videoStream;
    int audioStreamIndex;
    AVCodecContext *audioCodec;
    AVFrame *audioFrame;
    ALuint audioSource;
    float audioGain;
    bool audioSourceReady;
    bool audioDecodeFailed;
    bool audioInputFlushed;
    bool audioMuted;
    bool firstVideoFrameAccepted;
    std::vector<int16_t> audioPcm;
    AVFrame *frame;
    AVFrame *convertedFrame;
    AVPacket *packet;
    SwsContext *sws;
    int videoStreamIndex;
    bool inputEof;
    bool packetPending;
    bool frameDecodeFinished;
    bool firstPostFrameWaitLogged;
    bool decodeFailed;
    bool paused;
    bool convertedAllocated;
    uint32_t decodedFrames;
    uint32_t fpsNum;
    uint32_t fpsDen;
    uint32_t startTicks;
};

static char g_switchBinkError[256] = "";

static SwitchBinkState *Switch_BinkState(HBINK bink)
{
    return bink ? reinterpret_cast<SwitchBinkState *>(bink->ioptr) : nullptr;
}

static void Switch_BinkSetError(const char *message)
{
    std::snprintf(
        g_switchBinkError,
        sizeof(g_switchBinkError),
        "%s",
        message ? message : "unknown error");
}

static void Switch_BinkSetAvError(int err, const char *where)
{
    char avError[128] = {};
    av_strerror(err, avError, sizeof(avError));

    std::snprintf(
        g_switchBinkError,
        sizeof(g_switchBinkError),
        "%s: %s",
        where ? where : "FFmpeg",
        avError);
}

static uint32_t Switch_BinkReadU32LE(const uint8_t *bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
}

static bool Switch_BinkGetMemorySize(
    const uint8_t *data,
    int64_t *sizeOut)
{
    if (!data || !sizeOut || std::memcmp(data, "BIK", 3) != 0)
    {
        Switch_BinkSetError(
            "BINKFROMMEMORY input has no supported Bink 1 header");
        return false;
    }

    // Bink stores the file size minus the 8-byte signature/size prefix at
    // offset 4. BinkOpen's memory ABI supplies only the data pointer, so this
    // header field is the authoritative bound for FFmpeg's custom input IO.
    const uint64_t declaredSize =
        static_cast<uint64_t>(Switch_BinkReadU32LE(data + 4)) + 8u;
    if (declaredSize < 44u)
    {
        Switch_BinkSetError("BINKFROMMEMORY input has an invalid Bink size");
        return false;
    }

    *sizeOut = static_cast<int64_t>(declaredSize);
    return true;
}

static int Switch_BinkReadMemory(
    void *opaque,
    uint8_t *buffer,
    int bufferSize)
{
    SwitchBinkState *state = static_cast<SwitchBinkState *>(opaque);
    if (!state || !buffer || bufferSize < 0)
        return AVERROR(EINVAL);
    if (bufferSize == 0)
        return 0;
    if (state->memoryPosition >= state->memorySize)
        return AVERROR_EOF;

    const int64_t remaining = state->memorySize - state->memoryPosition;
    const int bytesToCopy = static_cast<int>(
        std::min<int64_t>(remaining, bufferSize));
    std::memcpy(
        buffer,
        state->memoryData + state->memoryPosition,
        static_cast<size_t>(bytesToCopy));
    state->memoryPosition += bytesToCopy;
    return bytesToCopy;
}

static int64_t Switch_BinkSeekMemory(
    void *opaque,
    int64_t offset,
    int whence)
{
    SwitchBinkState *state = static_cast<SwitchBinkState *>(opaque);
    if (!state)
        return AVERROR(EINVAL);
    if (whence & AVSEEK_SIZE)
        return state->memorySize;

    whence &= ~AVSEEK_FORCE;
    int64_t base = 0;
    switch (whence)
    {
    case SEEK_SET:
        base = 0;
        break;
    case SEEK_CUR:
        base = state->memoryPosition;
        break;
    case SEEK_END:
        base = state->memorySize;
        break;
    default:
        return AVERROR(EINVAL);
    }

    if (offset < -base || offset > state->memorySize - base)
        return AVERROR(EINVAL);

    state->memoryPosition = base + offset;
    return state->memoryPosition;
}

static void Switch_BinkFreeState(SwitchBinkState *state)
{
    if (!state)
        return;

    if (state->audioSourceReady && alcGetCurrentContext())
    {
        alSourceStop(state->audioSource);
        ALint queued = 0;
        alGetSourcei(state->audioSource, AL_BUFFERS_QUEUED, &queued);
        while (queued-- > 0)
        {
            ALuint buffer = 0;
            alSourceUnqueueBuffers(state->audioSource, 1, &buffer);
            if (buffer)
                alDeleteBuffers(1, &buffer);
        }
        alDeleteSources(1, &state->audioSource);
        state->audioSourceReady = false;
    }

    if (state->audioFrame)
        av_frame_free(&state->audioFrame);
    if (state->audioCodec)
        avcodec_free_context(&state->audioCodec);

    if (state->sws)
        sws_freeContext(state->sws);

    if (state->convertedFrame)
    {
        if (state->convertedAllocated)
            av_freep(&state->convertedFrame->data[0]);
        av_frame_free(&state->convertedFrame);
    }

    if (state->packet)
        av_packet_free(&state->packet);
    if (state->frame)
        av_frame_free(&state->frame);
    if (state->codec)
        avcodec_free_context(&state->codec);
    if (state->format)
        avformat_close_input(&state->format);
    if (state->memoryIo)
    {
        av_freep(&state->memoryIo->buffer);
        avio_context_free(&state->memoryIo);
    }

    delete state;
}

static bool Switch_BinkEnsureAudioSource(SwitchBinkState *state)
{
    if (!state || !state->audioCodec)
        return false;
    if (state->audioSourceReady)
        return true;
    if (!alcGetCurrentContext())
        return false;

    while (alGetError() != AL_NO_ERROR) {}
    alGenSources(1, &state->audioSource);
    if (!state->audioSource || alGetError() != AL_NO_ERROR)
    {
        state->audioSource = 0;
        return false;
    }

    alSourcei(state->audioSource, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(state->audioSource, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSourcef(state->audioSource, AL_ROLLOFF_FACTOR, 0.0f);
    alSourcef(state->audioSource, AL_GAIN, state->audioGain);
    state->audioSourceReady = alGetError() == AL_NO_ERROR;
    if (!state->audioSourceReady)
    {
        alDeleteSources(1, &state->audioSource);
        state->audioSource = 0;
    }
    return state->audioSourceReady;
}

static double Switch_BinkReadAudioSample(
    const AVFrame *frame,
    AVSampleFormat sampleFormat,
    bool planar,
    int bytesPerSample,
    int inputChannels,
    int channel,
    int sample)
{
    const uint8_t *plane = frame->extended_data[planar ? channel : 0];
    const size_t sampleIndex = planar
        ? static_cast<size_t>(sample)
        : static_cast<size_t>(sample) * static_cast<size_t>(inputChannels) + static_cast<size_t>(channel);
    const uint8_t *p = plane + sampleIndex * static_cast<size_t>(bytesPerSample);

    switch (sampleFormat)
    {
    case AV_SAMPLE_FMT_U8:
        return (static_cast<int>(*p) - 128) / 128.0;
    case AV_SAMPLE_FMT_S16:
        return *reinterpret_cast<const int16_t *>(p) / 32768.0;
    case AV_SAMPLE_FMT_S32:
        return *reinterpret_cast<const int32_t *>(p) / 2147483648.0;
    case AV_SAMPLE_FMT_S64:
        return static_cast<double>(*reinterpret_cast<const int64_t *>(p) / 9223372036854775808.0L);
    case AV_SAMPLE_FMT_FLT:
        return *reinterpret_cast<const float *>(p);
    case AV_SAMPLE_FMT_DBL:
        return *reinterpret_cast<const double *>(p);
    default:
        return 0.0;
    }
}

static void Switch_BinkQueueAudioFrame(SwitchBinkState *state, const AVFrame *frame)
{
    if (!state || !frame || state->paused || state->audioMuted ||
        !Switch_BinkEnsureAudioSource(state))
        return;

    int inputChannels = frame->ch_layout.nb_channels;
    if (inputChannels <= 0)
        inputChannels = state->audioCodec->ch_layout.nb_channels;
    if (inputChannels <= 0 || frame->nb_samples <= 0)
        return;

    int sampleRate = frame->sample_rate > 0
        ? frame->sample_rate
        : state->audioCodec->sample_rate;
    if (sampleRate <= 0)
        return;

    const AVSampleFormat format = static_cast<AVSampleFormat>(frame->format);
    const bool planar = av_sample_fmt_is_planar(format) != 0;
    const AVSampleFormat packedFormat = av_get_packed_sample_fmt(format);
    const int bytesPerSample = av_get_bytes_per_sample(format);
    if (bytesPerSample <= 0 || packedFormat == AV_SAMPLE_FMT_NONE)
        return;

    const int outputChannels = inputChannels == 1 ? 1 : 2;
    const size_t outputSampleCount =
        static_cast<size_t>(frame->nb_samples) * static_cast<size_t>(outputChannels);
    state->audioPcm.resize(outputSampleCount);

    for (int i = 0; i < frame->nb_samples; ++i)
    {
        for (int channel = 0; channel < outputChannels; ++channel)
        {
            const int sourceChannel = inputChannels == 1 ? 0 : channel;
            double value = Switch_BinkReadAudioSample(
                frame,
                packedFormat,
                planar,
                bytesPerSample,
                inputChannels,
                sourceChannel,
                i);
            if (!std::isfinite(value))
                value = 0.0;
            value = std::max(-1.0, std::min(1.0, value));
            state->audioPcm[
                static_cast<size_t>(i) * static_cast<size_t>(outputChannels) +
                static_cast<size_t>(channel)] =
                static_cast<int16_t>(std::lrint(value * 32767.0));
        }
    }

    ALint processed = 0;
    alGetSourcei(state->audioSource, AL_BUFFERS_PROCESSED, &processed);
    while (processed-- > 0)
    {
        ALuint oldBuffer = 0;
        alSourceUnqueueBuffers(state->audioSource, 1, &oldBuffer);
        if (oldBuffer)
            alDeleteBuffers(1, &oldBuffer);
    }

    ALuint buffer = 0;
    alGenBuffers(1, &buffer);
    if (!buffer || alGetError() != AL_NO_ERROR)
    {
        if (buffer)
            alDeleteBuffers(1, &buffer);
        return;
    }

    const ALenum outputFormat =
        outputChannels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    const size_t pcmBytes = outputSampleCount * sizeof(int16_t);
    if (pcmBytes > static_cast<size_t>(INT32_MAX))
    {
        alDeleteBuffers(1, &buffer);
        return;
    }

    alBufferData(
        buffer,
        outputFormat,
        state->audioPcm.data(),
        static_cast<ALsizei>(pcmBytes),
        sampleRate);
    if (alGetError() != AL_NO_ERROR)
    {
        alDeleteBuffers(1, &buffer);
        return;
    }

    alSourceQueueBuffers(state->audioSource, 1, &buffer);
    if (alGetError() != AL_NO_ERROR)
    {
        alDeleteBuffers(1, &buffer);
        return;
    }

    ALint sourceState = AL_STOPPED;
    alGetSourcei(state->audioSource, AL_SOURCE_STATE, &sourceState);
    if (sourceState != AL_PLAYING && state->firstVideoFrameAccepted &&
        !state->paused && !state->audioMuted)
        alSourcePlay(state->audioSource);
}

static void Switch_BinkReceiveAudioFrames(SwitchBinkState *state)
{
    if (!state || !state->audioCodec || !state->audioFrame)
        return;

    for (;;)
    {
        const int ret = avcodec_receive_frame(state->audioCodec, state->audioFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        if (ret < 0)
        {
            state->audioDecodeFailed = true;
            return;
        }

        Switch_BinkQueueAudioFrame(state, state->audioFrame);
        av_frame_unref(state->audioFrame);
    }
}

static void Switch_BinkDecodeAudioPacket(SwitchBinkState *state, const AVPacket *packet)
{
    if (!state || !state->audioCodec || state->audioDecodeFailed)
        return;

    int ret = avcodec_send_packet(state->audioCodec, packet);
    if (ret == AVERROR(EAGAIN))
    {
        Switch_BinkReceiveAudioFrames(state);
        ret = avcodec_send_packet(state->audioCodec, packet);
    }
    if (ret < 0 && ret != AVERROR_EOF)
    {
        state->audioDecodeFailed = true;
        return;
    }

    Switch_BinkReceiveAudioFrames(state);
}

static void Switch_BinkFlushAudio(SwitchBinkState *state)
{
    if (!state || !state->audioCodec || state->audioInputFlushed)
        return;

    state->audioInputFlushed = true;
    int ret = avcodec_send_packet(state->audioCodec, nullptr);
    if (ret == AVERROR(EAGAIN))
    {
        Switch_BinkReceiveAudioFrames(state);
        ret = avcodec_send_packet(state->audioCodec, nullptr);
    }
    if (ret >= 0 || ret == AVERROR_EOF)
        Switch_BinkReceiveAudioFrames(state);
}

static bool Switch_BinkInitConvertedFrame(SwitchBinkState *state)
{
    if (!state || !state->codec)
        return false;

    if (state->codec->pix_fmt == AV_PIX_FMT_YUV420P)
        return true;

    state->convertedFrame = av_frame_alloc();
    if (!state->convertedFrame)
    {
        Switch_BinkSetError("av_frame_alloc failed");
        return false;
    }

    state->convertedFrame->format = AV_PIX_FMT_YUV420P;
    state->convertedFrame->width = state->codec->width;
    state->convertedFrame->height = state->codec->height;

    const int allocated = av_image_alloc(
        state->convertedFrame->data,
        state->convertedFrame->linesize,
        state->codec->width,
        state->codec->height,
        AV_PIX_FMT_YUV420P,
        1);
    if (allocated < 0)
    {
        Switch_BinkSetAvError(allocated, "av_image_alloc");
        return false;
    }

    state->convertedAllocated = true;
    return true;
}

static bool Switch_BinkDecodeNext(SwitchBinkState *state)
{
    if (!state || !state->codec || !state->frame)
        return false;

    for (;;)
    {
        int ret = avcodec_receive_frame(state->codec, state->frame);
        if (ret == 0)
            return true;

        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
        {
            Switch_BinkSetAvError(ret, "avcodec_receive_frame");
            state->decodeFailed = true;
            return false;
        }

        // avcodec_send_packet() returning EAGAIN means the decoder did not
        // accept that packet. Keep it referenced and retry after draining any
        // output; discarding it can break inter-frame decoding after frame 1.
        if (state->packetPending)
        {
            ret = avcodec_send_packet(state->codec, state->packet);
            if (ret == AVERROR(EAGAIN))
                continue;

            if (ret < 0)
            {
                Switch_BinkSetAvError(ret, "avcodec_send_packet");
                state->decodeFailed = true;
                return false;
            }

            av_packet_unref(state->packet);
            state->packetPending = false;
            continue;
        }

        if (state->inputEof)
            return false;

        ret = av_read_frame(state->format, state->packet);
        if (ret == AVERROR_EOF)
        {
            state->inputEof = true;
            Switch_BinkFlushAudio(state);
            ret = avcodec_send_packet(state->codec, nullptr);
            if (ret < 0 && ret != AVERROR_EOF)
            {
                Switch_BinkSetAvError(ret, "avcodec_send_packet(eof)");
                state->decodeFailed = true;
                return false;
            }
            continue;
        }
        if (ret < 0)
        {
            // A damaged/truncated file or an underlying IO failure is not a
            // successful end-of-stream. The old code treated every negative
            // av_read_frame result as EOF, making BinkDoFrame finish normally
            // even when no video frame had been decoded.
            Switch_BinkSetAvError(ret, "av_read_frame");
            state->decodeFailed = true;
            return false;
        }

        if (state->packet->stream_index == state->audioStreamIndex)
        {
            Switch_BinkDecodeAudioPacket(state, state->packet);
            av_packet_unref(state->packet);
            continue;
        }

        if (state->packet->stream_index != state->videoStreamIndex)
        {
            av_packet_unref(state->packet);
            continue;
        }

        state->packetPending = true;
    }
}

static bool Switch_BinkGetFrameForCopy(
    SwitchBinkState *state,
    const AVFrame **frameOut)
{
    if (!state || !frameOut)
        return false;

    if (state->codec->pix_fmt == AV_PIX_FMT_YUV420P)
    {
        *frameOut = state->frame;
        return true;
    }

    if (!state->convertedFrame)
        return false;

    if (!state->sws)
    {
        state->sws = sws_getContext(
            state->codec->width,
            state->codec->height,
            state->codec->pix_fmt,
            state->codec->width,
            state->codec->height,
            AV_PIX_FMT_YUV420P,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr);
        if (!state->sws)
        {
            Switch_BinkSetError("sws_getContext failed");
            return false;
        }
    }

    sws_scale(
        state->sws,
        state->frame->data,
        state->frame->linesize,
        0,
        state->codec->height,
        state->convertedFrame->data,
        state->convertedFrame->linesize);

    *frameOut = state->convertedFrame;
    return true;
}

static void Switch_BinkCopyPlane(
    BINKPLANE *plane,
    const uint8_t *src,
    int srcPitch,
    uint32_t width,
    uint32_t height)
{
    if (!plane || !plane->Buffer || !src || !width || !height)
        return;

    const uint32_t dstPitch =
        plane->BufferPitch ? plane->BufferPitch : width;

    for (uint32_t y = 0; y < height; ++y)
    {
        std::memcpy(
            static_cast<uint8_t *>(plane->Buffer) + static_cast<size_t>(y) * dstPitch,
            src + static_cast<size_t>(y) * srcPitch,
            width);
    }
}

RADDEFFUNC char PTR4* RADEXPLINK BinkGetError(void)
{
    return g_switchBinkError;
}

RADDEFFUNC HBINK RADEXPLINK BinkOpen(
    const char PTR4 *name,
    U32 flags)
{
    g_switchBinkError[0] = 0;

    const bool fromMemory = (flags & BINKFROMMEMORY) != 0;
    const char *traceName = fromMemory ? "<memory>" : name;
    if (!name || (!fromMemory && !*name))
    {
        Switch_BinkSetError("empty filename");
        return nullptr;
    }

    SwitchBinkState *state = new SwitchBinkState{};
    if (!state)
    {
        Switch_BinkSetError("out of memory");
        return nullptr;
    }

    state->audioStreamIndex = -1;
    state->audioGain = 1.0f;

    // This wrapper implements the Bink API, so force FFmpeg's Bink
    // demuxer instead of allowing format probing to select an unrelated
    // container and start its decoder inside avformat_find_stream_info().
    const AVInputFormat *binkFormat = av_find_input_format("bink");
    if (!binkFormat)
    {
        Switch_BinkSetError("FFmpeg Bink demuxer is unavailable");
        delete state;
        return nullptr;
    }

    int ret = 0;
    if (fromMemory)
    {
        state->memoryData = reinterpret_cast<const uint8_t *>(name);
        if (!Switch_BinkGetMemorySize(state->memoryData, &state->memorySize))
        {
            Switch_BinkFreeState(state);
            return nullptr;
        }

        constexpr int ioBufferSize = 32 * 1024;
        uint8_t *ioBuffer = static_cast<uint8_t *>(av_malloc(ioBufferSize));
        if (!ioBuffer)
        {
            Switch_BinkSetError("FFmpeg memory IO buffer allocation failed");
            Switch_BinkFreeState(state);
            return nullptr;
        }

        state->memoryIo = avio_alloc_context(
            ioBuffer,
            ioBufferSize,
            0,
            state,
            Switch_BinkReadMemory,
            nullptr,
            Switch_BinkSeekMemory);
        if (!state->memoryIo)
        {
            av_free(ioBuffer);
            Switch_BinkSetError("avio_alloc_context failed for memory input");
            Switch_BinkFreeState(state);
            return nullptr;
        }
        state->memoryIo->seekable = AVIO_SEEKABLE_NORMAL;

        state->format = avformat_alloc_context();
        if (!state->format)
        {
            Switch_BinkSetError("avformat_alloc_context failed for memory input");
            Switch_BinkFreeState(state);
            return nullptr;
        }
        state->format->pb = state->memoryIo;
        state->format->flags |= AVFMT_FLAG_CUSTOM_IO;
        ret = avformat_open_input(
            &state->format,
            nullptr,
            binkFormat,
            nullptr);
    }
    else
    {
        // FFmpeg parses "sdmc:/..." as a URL whose protocol is "sdmc".
        // Switch paths are local filesystem paths, so force FFmpeg's file
        // protocol while keeping the mount prefix for libnx's devoptab.
        char fileUrl[1024];
        const char *inputName = name;
        if (std::strchr(name, ':') && std::strncmp(name, "file:", 5) != 0)
        {
            const int urlLength = std::snprintf(
                fileUrl,
                sizeof(fileUrl),
                "file:%s",
                name);
            if (urlLength < 0 || static_cast<size_t>(urlLength) >= sizeof(fileUrl))
            {
                Switch_BinkSetError("FFmpeg local file URL is too long");
                Switch_BinkFreeState(state);
                return nullptr;
            }
            inputName = fileUrl;
        }

        ret = avformat_open_input(
            &state->format,
            inputName,
            binkFormat,
            nullptr);
    }

    if (ret < 0)
    {
        Switch_BinkSetAvError(ret, "avformat_open_input");
        Switch_BinkFreeState(state);
        return nullptr;
    }

    // Validate the video codec before asking FFmpeg to inspect/decode packets.
    // avformat_find_stream_info() may invoke a decoder while probing; calling it
    // first lets a wrongly resolved/non-Bink video reach an unrelated decoder.
    state->videoStreamIndex = av_find_best_stream(
        state->format,
        AVMEDIA_TYPE_VIDEO,
        -1,
        -1,
        nullptr,
        0);
    if (state->videoStreamIndex < 0)
    {
        Switch_BinkSetAvError(
            state->videoStreamIndex,
            "av_find_best_stream");
        Switch_BinkFreeState(state);
        return nullptr;
    }

    state->videoStream =
        state->format->streams[state->videoStreamIndex];
    const AVCodecParameters *params = state->videoStream->codecpar;

    // BinkOpen is a Bink API entry point. Do not let an unrelated video
    // stream select any decoder available in FFmpeg when the resolved media
    // path is wrong; that would invoke a codec outside the Bink contract.
    if (params->codec_id != AV_CODEC_ID_BINKVIDEO)
    {
        char trace[512];
        const char *formatName =
            state->format->iformat ? state->format->iformat->name : "(unknown)";
        const char *codecName = avcodec_get_name(params->codec_id);
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][BINK CODEC] reject name=%s format=%s stream=%d codec=%s id=%d\n",
            traceName,
            formatName,
            state->videoStreamIndex,
            codecName ? codecName : "(unknown)",
            static_cast<int>(params->codec_id));
        Switch_LogWrite(trace);
        Switch_BinkSetError("input video stream is not Bink video");
        Switch_BinkFreeState(state);
        return nullptr;
    }

    // At this point the Bink demuxer's header has identified the stream and we
    // have confirmed its codec ID. Only now may stream probing decode packets.
    ret = avformat_find_stream_info(state->format, nullptr);
    if (ret < 0)
    {
        Switch_BinkSetAvError(ret, "avformat_find_stream_info");
        Switch_BinkFreeState(state);
        return nullptr;
    }

    // Reacquire stream metadata after probing; FFmpeg may update it.
    state->videoStream = state->format->streams[state->videoStreamIndex];
    params = state->videoStream->codecpar;
    if (params->codec_id != AV_CODEC_ID_BINKVIDEO)
    {
        Switch_BinkSetError("video stream changed to a non-Bink codec during stream probing");
        Switch_BinkFreeState(state);
        return nullptr;
    }

    // Bink files may contain one or more audio streams. The first decodable audio stream
    // is the game's selected soundtrack for this port; video-only files remain valid.
    state->audioStreamIndex = av_find_best_stream(
        state->format,
        AVMEDIA_TYPE_AUDIO,
        -1,
        -1,
        nullptr,
        0);
    if (state->audioStreamIndex < 0)
        state->audioStreamIndex = -1;

    const AVCodec *decoder = avcodec_find_decoder(params->codec_id);
    if (!decoder)
    {
        Switch_BinkSetError("FFmpeg has no decoder for Bink video");
        Switch_BinkFreeState(state);
        return nullptr;
    }

    state->codec = avcodec_alloc_context3(decoder);
    if (!state->codec)
    {
        Switch_BinkSetError("avcodec_alloc_context3 failed");
        Switch_BinkFreeState(state);
        return nullptr;
    }

    ret = avcodec_parameters_to_context(state->codec, params);
    if (ret < 0)
    {
        Switch_BinkSetAvError(ret, "avcodec_parameters_to_context");
        Switch_BinkFreeState(state);
        return nullptr;
    }

    ret = avcodec_open2(state->codec, decoder, nullptr);
    if (ret < 0)
    {
        Switch_BinkSetAvError(ret, "avcodec_open2");
        Switch_BinkFreeState(state);
        return nullptr;
    }

    state->frame = av_frame_alloc();
    state->packet = av_packet_alloc();
    if (!state->frame || !state->packet)
    {
        Switch_BinkSetError("FFmpeg frame/packet allocation failed");
        Switch_BinkFreeState(state);
        return nullptr;
    }

    if (!Switch_BinkInitConvertedFrame(state))
    {
        Switch_BinkFreeState(state);
        return nullptr;
    }

    if (state->audioStreamIndex >= 0)
    {
        AVStream *audioStream = state->format->streams[state->audioStreamIndex];
        const AVCodec *audioDecoder = avcodec_find_decoder(audioStream->codecpar->codec_id);
        if (audioDecoder)
        {
            state->audioCodec = avcodec_alloc_context3(audioDecoder);
            if (state->audioCodec &&
                avcodec_parameters_to_context(state->audioCodec, audioStream->codecpar) >= 0 &&
                avcodec_open2(state->audioCodec, audioDecoder, nullptr) >= 0)
            {
                state->audioFrame = av_frame_alloc();
                if (!state->audioFrame)
                    avcodec_free_context(&state->audioCodec);
            }
            else if (state->audioCodec)
            {
                avcodec_free_context(&state->audioCodec);
            }
        }

        if (state->audioCodec)
            Switch_BinkEnsureAudioSource(state);
        else
            state->audioStreamIndex = -1;
    }

    AVRational frameRate = state->videoStream->avg_frame_rate;
    if (frameRate.num <= 0 || frameRate.den <= 0)
        frameRate = state->videoStream->r_frame_rate;
    if (frameRate.num <= 0 || frameRate.den <= 0)
    {
        frameRate.num = 30;
        frameRate.den = 1;
    }

    state->fpsNum = static_cast<uint32_t>(
        std::min<int64_t>(
            std::max<int64_t>(frameRate.num, 1),
            UINT32_MAX));
    state->fpsDen = static_cast<uint32_t>(
        std::min<int64_t>(
            std::max<int64_t>(frameRate.den, 1),
            UINT32_MAX));
    state->decodedFrames = 0;
    state->startTicks = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    HBINK bink = new BINK{};
    if (!bink)
    {
        Switch_BinkSetError("out of memory");
        Switch_BinkFreeState(state);
        return nullptr;
    }

    bink->Width = static_cast<U32>(state->codec->width);
    bink->Height = static_cast<U32>(state->codec->height);

    uint64_t frameCount = state->videoStream->nb_frames;
    if (!frameCount && state->videoStream->duration > 0)
    {
        const AVRational tb = state->videoStream->time_base;
        if (tb.num > 0 && tb.den > 0)
        {
            frameCount =
                (static_cast<uint64_t>(state->videoStream->duration) *
                 state->fpsNum *
                 tb.num +
                 static_cast<uint64_t>(state->fpsDen) * tb.den - 1) /
                (static_cast<uint64_t>(state->fpsDen) * tb.den);
        }
    }

    bink->Frames =
        frameCount > 0 && frameCount < UINT32_MAX
            ? static_cast<U32>(frameCount)
            : UINT32_MAX - 1u;
    bink->FrameNum = 1;
    bink->LastFrameNum = 0;
    bink->FrameRate = state->fpsNum;
    bink->FrameRateDiv = state->fpsDen;
    bink->OpenFlags = flags;
    bink->Size = state->format->pb
        ? static_cast<U32>(
              std::max<int64_t>(
                  0,
                  std::min<int64_t>(
                      UINT32_MAX,
                      avio_size(state->format->pb))))
        : 0;
    bink->Paused = 0;
    bink->videoon = 1;
    bink->soundon = state->audioSourceReady ? 1u : 0u;
    bink->ioptr = reinterpret_cast<U8 PTR4 *>(state);

    char trace[512];
    std::snprintf(
        trace,
        sizeof(trace),
        "[KisakCOD][BINK CODEC] open name=%s format=%s codec=%s id=%d size=%ux%u frames=%u fps=%u/%u\n",
        traceName,
        state->format->iformat ? state->format->iformat->name : "(unknown)",
        avcodec_get_name(params->codec_id),
        static_cast<int>(params->codec_id),
        static_cast<unsigned>(bink->Width),
        static_cast<unsigned>(bink->Height),
        static_cast<unsigned>(bink->Frames),
        static_cast<unsigned>(bink->FrameRate),
        static_cast<unsigned>(bink->FrameRateDiv));
    Switch_LogWrite(trace);

    return bink;
}

RADDEFFUNC void RADEXPLINK BinkClose(HBINK bink)
{
    if (!bink)
        return;

    SwitchBinkState *state = Switch_BinkState(bink);
    bink->ioptr = nullptr;
    Switch_BinkFreeState(state);
    delete bink;
}

RADDEFFUNC S32 RADEXPLINK BinkWait(HBINK bink)
{
    SwitchBinkState *state = Switch_BinkState(bink);
    if (!state)
        return 1;

    if (state->decodedFrames == 1 && !state->firstPostFrameWaitLogged)
    {
        const uint64_t framePeriodMs =
            (1000ull * state->fpsDen + state->fpsNum - 1) /
            state->fpsNum;
        const uint32_t elapsed =
            static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count()) -
            state->startTicks;
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][BINK FLOW] post-first-frame wait frame=%u elapsed=%u target=%llu paused=%u finished=%u\n",
            static_cast<unsigned>(bink->FrameNum),
            static_cast<unsigned>(elapsed),
            static_cast<unsigned long long>(framePeriodMs),
            state->paused ? 1u : 0u,
            state->frameDecodeFinished ? 1u : 0u);
        Switch_LogWrite(trace);
        state->firstPostFrameWaitLogged = true;
    }

    if (state->paused || state->frameDecodeFinished)
        return 1;

    if (!state->decodedFrames)
        return 0;

    const uint64_t framePeriodMs =
        (1000ull * state->fpsDen + state->fpsNum - 1) /
        state->fpsNum;
    const uint64_t targetMs =
        static_cast<uint64_t>(state->decodedFrames) * framePeriodMs;
    const uint32_t elapsed =
        static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()) -
        state->startTicks;

    return static_cast<uint64_t>(elapsed) < targetMs ? 1 : 0;
}

RADDEFFUNC S32 RADEXPLINK BinkDoFrame(HBINK bink)
{
    SwitchBinkState *state = Switch_BinkState(bink);
    if (!state || !bink || !bink->FrameBuffers)
        return 1;

    if (state->frameDecodeFinished || bink->FrameNum > bink->Frames)
        return 1;

    if (!Switch_BinkDecodeNext(state))
    {
        state->frameDecodeFinished = true;
        char trace[512];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][BINK FRAME] stop frame=%u decoded=%u eof=%d decodeFailed=%d error=%s\n",
            static_cast<unsigned>(bink->FrameNum),
            static_cast<unsigned>(state->decodedFrames),
            state->inputEof ? 1 : 0,
            state->decodeFailed ? 1 : 0,
            g_switchBinkError[0] ? g_switchBinkError : "(none)");
        Switch_LogWrite(trace);
        // Runtime codec errors must be logged here rather than propagated via
        // BinkGetError(), because the engine asserts that API errors are empty
        // after each frame operation.
        g_switchBinkError[0] = 0;
        bink->Frames = bink->FrameNum;
        return 1;
    }

    const AVFrame *frame = nullptr;
    if (!Switch_BinkGetFrameForCopy(state, &frame))
    {
        state->frameDecodeFinished = true;
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][CINEMATIC] FFmpeg frame conversion failed: %s\n",
            g_switchBinkError[0] ? g_switchBinkError : "unknown error");
        Switch_LogWrite(trace);
        g_switchBinkError[0] = 0;
        bink->Frames = bink->FrameNum;
        return 1;
    }

    BINKFRAMEBUFFERS *buffers = bink->FrameBuffers;
    const uint32_t frameIndex = std::min<U32>(
        buffers->FrameNum,
        buffers->TotalFrames
            ? buffers->TotalFrames - 1
            : 0);

    BINKFRAMEPLANESET &dst = buffers->Frames[frameIndex];
    const uint32_t width = bink->Width;
    const uint32_t height = bink->Height;
    const uint32_t chromaWidth = (width + 1u) >> 1;
    const uint32_t chromaHeight = (height + 1u) >> 1;

    Switch_BinkCopyPlane(
        &dst.YPlane,
        frame->data[0],
        frame->linesize[0],
        width,
        height);
    // FFmpeg YUV420P planes are Y, U(Cb), V(Cr), while the legacy
    // Bink texture contract names the chroma planes Cr and Cb.
    Switch_BinkCopyPlane(
        &dst.cRPlane,
        frame->data[2],
        frame->linesize[2],
        chromaWidth,
        chromaHeight);
    Switch_BinkCopyPlane(
        &dst.cBPlane,
        frame->data[1],
        frame->linesize[1],
        chromaWidth,
        chromaHeight);

    // Do not charge file opening, FFmpeg stream probing, or Vulkan texture
    // creation against the movie's playback clock.
    if (state->decodedFrames == 0)
    {
        char trace[192];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][BINK FRAME] first frame decoded size=%ux%u\n",
            static_cast<unsigned>(bink->Width),
            static_cast<unsigned>(bink->Height));
        Switch_LogWrite(trace);
        state->startTicks = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    ++state->decodedFrames;
    if (state->decodedFrames == 2 ||
        state->decodedFrames == 3 ||
        (state->decodedFrames % 30u) == 0u)
    {
        char trace[224];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][BINK DECODE] decoded=%u movieFrame=%u bufferFrame=%u dimensions=%ux%u\n",
            static_cast<unsigned>(state->decodedFrames),
            static_cast<unsigned>(bink->FrameNum),
            static_cast<unsigned>(buffers->FrameNum),
            static_cast<unsigned>(width),
            static_cast<unsigned>(height));
        Switch_LogWrite(trace);
    }
    bink->LastFrameNum = bink->FrameNum;
    bink->FrameChangePercent = 100;
    return 0;
}

RADDEFFUNC void RADEXPLINK BinkNextFrame(HBINK bink)
{
    SwitchBinkState *state = Switch_BinkState(bink);
    if (!bink || !state)
        return;

    // BinkNextFrame is called after BinkDoFrame succeeds and the engine has
    // selected the decoded buffer for display. Keep queued audio silent until
    // that hand-off, so cinematic sound cannot run ahead of an unaccepted frame.
    if (!state->firstVideoFrameAccepted)
    {
        state->firstVideoFrameAccepted = true;
        if (state->audioSourceReady && !state->paused && !state->audioMuted)
        {
            ALint queuedAudio = 0;
            ALint audioState = AL_STOPPED;
            alGetSourcei(state->audioSource, AL_BUFFERS_QUEUED, &queuedAudio);
            alGetSourcei(state->audioSource, AL_SOURCE_STATE, &audioState);
            if (queuedAudio > 0 && audioState != AL_PLAYING)
                alSourcePlay(state->audioSource);
        }
    }

    if (bink->FrameNum >= bink->Frames)
        return;

    const U32 previousFrameNum = bink->FrameNum;
    ++bink->FrameNum;
    if (bink->FrameBuffers && bink->FrameBuffers->TotalFrames > 0)
    {
        bink->FrameBuffers->FrameNum =
            (bink->FrameBuffers->FrameNum + 1) %
            bink->FrameBuffers->TotalFrames;
    }
    if (bink->FrameNum <= 4u || (bink->FrameNum % 30u) == 0u)
    {
        char trace[224];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][BINK NEXT] frame=%u->%u decoded=%u nextBuffer=%u/%u\n",
            static_cast<unsigned>(previousFrameNum),
            static_cast<unsigned>(bink->FrameNum),
            static_cast<unsigned>(state->decodedFrames),
            bink->FrameBuffers
                ? static_cast<unsigned>(bink->FrameBuffers->FrameNum) : 0u,
            bink->FrameBuffers
                ? static_cast<unsigned>(bink->FrameBuffers->TotalFrames) : 0u);
        Switch_LogWrite(trace);
    }
}

RADDEFFUNC S32 RADEXPLINK BinkPause(HBINK bink, S32 pause)
{
    SwitchBinkState *state = Switch_BinkState(bink);
    if (state)
    {
        state->paused = pause != 0;
        if (state->audioSourceReady)
        {
            if (state->paused)
                alSourcePause(state->audioSource);
            else
            {
                ALint queued = 0;
                ALint sourceState = AL_STOPPED;
                alGetSourcei(state->audioSource, AL_BUFFERS_QUEUED, &queued);
                alGetSourcei(state->audioSource, AL_SOURCE_STATE, &sourceState);
                if (queued > 0 && sourceState != AL_PLAYING && !state->audioMuted &&
                    state->firstVideoFrameAccepted)
                    alSourcePlay(state->audioSource);
            }
        }
    }
    if (bink)
        bink->Paused = pause ? 1u : 0u;
    return state && state->paused ? 1 : 0;
}

RADDEFFUNC void RADEXPLINK BinkGetRealtime(
    HBINK bink,
    BINKREALTIME PTR4 *realtime,
    U32 frames)
{
    (void)frames;
    if (!realtime)
        return;

    std::memset(realtime, 0, sizeof(*realtime));

    SwitchBinkState *state = Switch_BinkState(bink);
    if (!bink || !state)
        return;

    realtime->FrameNum = bink->FrameNum;
    realtime->FrameRate = state->fpsNum;
    realtime->FrameRateDiv = state->fpsDen;
    realtime->Frames = 1;
    realtime->FramesTime =
        static_cast<U32>(
            (1000ull * state->fpsDen + state->fpsNum - 1) /
            state->fpsNum);
    realtime->ReadBufferSize = 1;
    realtime->ReadBufferUsed = 1;
}

RADDEFFUNC void RADEXPLINK BinkSetMemory(
    BINKMEMALLOC alloc,
    BINKMEMFREE freeFunc)
{
    (void)alloc;
    (void)freeFunc;
}

RADDEFFUNC void RADEXPLINK BinkSetIOSize(U32 ioSize)
{
    (void)ioSize;
}

RADDEFFUNC void RADEXPLINK BinkSetSoundTrack(
    U32 totalTracks,
    U32 PTR4 *tracks)
{
    (void)totalTracks;
    (void)tracks;
}

RADDEFFUNC S32 RADEXPLINK BinkSetSoundOnOff(HBINK bink, S32 onoff)
{
    SwitchBinkState *state = Switch_BinkState(bink);
    if (!bink || !state)
        return 0;

    const bool enable = onoff != 0 && state->audioCodec && Switch_BinkEnsureAudioSource(state);
    state->audioMuted = !enable;
    bink->soundon = enable ? 1u : 0u;
    if (state->audioSourceReady)
    {
        if (!enable)
            alSourcePause(state->audioSource);
        else
        {
            ALint queued = 0;
            alGetSourcei(state->audioSource, AL_BUFFERS_QUEUED, &queued);
            if (queued > 0 && state->firstVideoFrameAccepted)
                alSourcePlay(state->audioSource);
        }
    }
    return bink->soundon ? 1 : 0;
}

RADDEFFUNC S32 RADEXPLINK BinkSetSoundSystem(
    BINKSNDSYSOPEN open,
    UINTa param)
{
    // The Switch Bink implementation decodes the file's first audio stream using FFmpeg
    // and queues PCM to the already initialized OpenAL output. The legacy platform callback
    // supplied by the Windows/Miles implementation is not usable on Switch.
    (void)open;
    (void)param;
    return 1;
}

RADDEFFUNC void RADEXPLINK BinkSetMixBinVolumes(
    HBINK bink,
    U32 trackId,
    U32 PTR4 *volMixBins,
    S32 PTR4 *volumes,
    U32 total)
{
    (void)volMixBins;
    (void)total;

    SwitchBinkState *state = Switch_BinkState(bink);
    if (!state || trackId != 0 || !volumes)
        return;

    const float left = std::max(0, std::min(32768, volumes[0])) / 32768.0f;
    const float right = std::max(0, std::min(32768, volumes[1])) / 32768.0f;
    state->audioGain = std::max(left, right);
    if (state->audioSourceReady)
        alSourcef(state->audioSource, AL_GAIN, state->audioGain);
}

RADDEFFUNC S32 RADEXPLINK BinkControlBackgroundIO(
    HBINK bink,
    U32 control)
{
    (void)bink;
    (void)control;
    return 1;
}

RADDEFFUNC void RADEXPLINK BinkGetFrameBuffersInfo(
    HBINK bink,
    BINKFRAMEBUFFERS *buffers)
{
    if (!buffers)
        return;

    std::memset(buffers, 0, sizeof(*buffers));

    if (!bink || !Switch_BinkState(bink))
        return;

    buffers->TotalFrames = BINKMAXFRAMEBUFFERS;
    buffers->YABufferWidth = bink->Width;
    buffers->YABufferHeight = bink->Height;
    buffers->cRcBBufferWidth = (bink->Width + 1u) >> 1;
    buffers->cRcBBufferHeight = (bink->Height + 1u) >> 1;
    buffers->FrameNum = 0;

    for (U32 i = 0; i < BINKMAXFRAMEBUFFERS; ++i)
    {
        buffers->Frames[i].YPlane.Allocate = 1;
        buffers->Frames[i].cRPlane.Allocate = 1;
        buffers->Frames[i].cBPlane.Allocate = 1;
        buffers->Frames[i].APlane.Allocate = 0;
    }
}

RADDEFFUNC void RADEXPLINK BinkRegisterFrameBuffers(
    HBINK bink,
    BINKFRAMEBUFFERS *buffers)
{
    if (bink)
        bink->FrameBuffers = buffers;
}

#endif
