#ifdef __SWITCH__

#include <SDL2/SDL_audio.h>
#include <switch.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdio>
#include <malloc.h>
#include <mutex>
#include <thread>

namespace
{
struct SwitchSdlAudioDevice
{
    SDL_AudioSpec spec{};
    AudioDriver driver{};
    AudioDriverWaveBuf buffers[2]{};
    void *pool = nullptr;
    Uint8 *callbackBuffer = nullptr;
    size_t poolSize = 0;
    std::thread thread{};
    std::atomic<bool> running{false};
    std::atomic<bool> paused{true};
    bool audrenInitialized = false;
    bool audrvInitialized = false;
};

SwitchSdlAudioDevice *g_device = nullptr;
std::mutex g_deviceMutex;
char g_error[256] = {};

constexpr AudioRendererConfig kAudioRendererConfig =
{
    .output_rate = AudioRendererOutputRate_48kHz,
    .num_voices = 24,
    .num_effects = 0,
    .num_sinks = 1,
    .num_mix_objs = 1,
    .num_mix_buffers = 2,
};

void SetError(const char *message)
{
    std::lock_guard<std::mutex> lock(g_deviceMutex);
    std::snprintf(g_error, sizeof(g_error), "%s", message ? message : "unknown error");
}

void ClearError()
{
    std::lock_guard<std::mutex> lock(g_deviceMutex);
    g_error[0] = '\0';
}

void AudioThreadMain(SwitchSdlAudioDevice *device)
{
    int nextBuffer = 0;

    while (device->running.load(std::memory_order_acquire))
    {
        if (device->paused.load(std::memory_order_acquire))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        audrvUpdate(&device->driver);

        int freeBuffer = -1;
        for (int i = 0; i < 2; ++i)
        {
            const int index = (nextBuffer + i) & 1;
            const auto state = device->buffers[index].state;
            if (state == AudioDriverWaveBufState_Free ||
                state == AudioDriverWaveBufState_Done)
            {
                freeBuffer = index;
                break;
            }
        }

        if (freeBuffer < 0)
        {
            audrenWaitFrame();
            continue;
        }

        nextBuffer = (freeBuffer + 1) & 1;

        if (device->spec.callback)
        {
            device->spec.callback(
                device->spec.userdata,
                device->callbackBuffer,
                static_cast<int>(device->spec.size));
        }
        else
        {
            std::memset(
                device->callbackBuffer,
                device->spec.silence,
                device->spec.size);
        }

        Uint8 *destination =
            static_cast<Uint8 *>(device->pool) +
            static_cast<size_t>(freeBuffer) * device->spec.size;

        std::memcpy(
            destination,
            device->callbackBuffer,
            device->spec.size);

        armDCacheFlush(destination, device->spec.size);

        AudioDriverWaveBuf &buffer = device->buffers[freeBuffer];
        audrvVoiceAddWaveBuf(&device->driver, 0, &buffer);
        audrvUpdate(&device->driver);
    }
}
} // namespace

extern "C"
int SDL_InitSubSystem(Uint32 flags)
{
    constexpr Uint32 kSDLInitAudio = 0x00000010u;

    if ((flags & kSDLInitAudio) == 0)
    {
        ClearError();
        return 0;
    }

    ClearError();
    return 0;
}

extern "C"
void SDL_QuitSubSystem(Uint32)
{
}

extern "C"
const char *SDL_GetError(void)
{
    return g_error;
}

extern "C"
int SDL_GetNumAudioDevices(int iscapture)
{
    return iscapture ? 0 : 1;
}

extern "C"
const char *SDL_GetAudioDeviceName(int index, int iscapture)
{
    static const char kDefaultName[] = "Default Device";

    if (iscapture || index != 0)
        return nullptr;

    return kDefaultName;
}

extern "C"
SDL_AudioDeviceID SDL_OpenAudioDevice(
    const char *,
    int iscapture,
    const SDL_AudioSpec *desired,
    SDL_AudioSpec *obtained,
    int)
{
    if (iscapture || !desired)
    {
        SetError("Switch SDL audio capture is unsupported");
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_deviceMutex);

    if (g_device)
    {
        std::snprintf(
            g_error,
            sizeof(g_error),
            "%s",
            "Only one Switch SDL audio device is supported");
        return 0;
    }

    auto *device = new SwitchSdlAudioDevice{};

    device->spec = *desired;
    device->spec.format = AUDIO_S16SYS;

    if (device->spec.channels != 1 && device->spec.channels != 2)
        device->spec.channels = 2;

    if (device->spec.freq <= 0)
        device->spec.freq = 48000;

    if (device->spec.samples == 0)
        device->spec.samples = 1024;

    device->spec.silence = 0;
    device->spec.padding = 0;
    device->spec.size =
        static_cast<Uint32>(device->spec.samples) *
        static_cast<Uint32>(device->spec.channels) *
        (SDL_AUDIO_BITSIZE(device->spec.format) / 8);

    device->poolSize =
        (static_cast<size_t>(device->spec.size) * 2u + 0xfffu) & ~size_t(0xfff);

    device->pool = memalign(0x1000, device->poolSize);
    device->callbackBuffer = static_cast<Uint8 *>(std::malloc(device->spec.size));

    if (!device->pool || !device->callbackBuffer)
    {
        std::free(device->callbackBuffer);
        std::free(device->pool);
        delete device;
        std::snprintf(g_error, sizeof(g_error), "%s", "Switch SDL audio allocation failed");
        return 0;
    }

    std::memset(device->pool, 0, device->poolSize);
    std::memset(device->callbackBuffer, 0, device->spec.size);

    Result rc = audrenInitialize(&kAudioRendererConfig);
    if (R_FAILED(rc))
    {
        std::free(device->callbackBuffer);
        std::free(device->pool);
        delete device;
        std::snprintf(g_error, sizeof(g_error), "audrenInitialize failed (0x%x)", rc);
        return 0;
    }
    device->audrenInitialized = true;

    rc = audrvCreate(&device->driver, &kAudioRendererConfig, 2);
    if (R_FAILED(rc))
    {
        audrenExit();
        std::free(device->callbackBuffer);
        std::free(device->pool);
        delete device;
        std::snprintf(g_error, sizeof(g_error), "audrvCreate failed (0x%x)", rc);
        return 0;
    }
    device->audrvInitialized = true;

    const int poolId =
        audrvMemPoolAdd(&device->driver, device->pool, device->poolSize);
    if (poolId < 0)
    {
        audrvClose(&device->driver);
        audrenExit();
        std::free(device->callbackBuffer);
        std::free(device->pool);
        delete device;
        std::snprintf(g_error, sizeof(g_error), "%s", "audrvMemPoolAdd failed");
        return 0;
    }

    rc = audrvMemPoolAttach(&device->driver, poolId);
    if (R_FAILED(rc))
    {
        audrvClose(&device->driver);
        audrenExit();
        std::free(device->callbackBuffer);
        std::free(device->pool);
        delete device;
        std::snprintf(g_error, sizeof(g_error), "audrvMemPoolAttach failed (0x%x)", rc);
        return 0;
    }

    static const u8 sinkChannels[] = { 0, 1 };
    rc = audrvDeviceSinkAdd(
        &device->driver,
        AUDREN_DEFAULT_DEVICE_NAME,
        2,
        sinkChannels);
    if (R_FAILED(rc))
    {
        audrvClose(&device->driver);
        audrenExit();
        std::free(device->callbackBuffer);
        std::free(device->pool);
        delete device;
        std::snprintf(g_error, sizeof(g_error), "audrvDeviceSinkAdd failed (0x%x)", rc);
        return 0;
    }

    rc = audrenStartAudioRenderer();
    if (R_FAILED(rc))
    {
        audrvClose(&device->driver);
        audrenExit();
        std::free(device->callbackBuffer);
        std::free(device->pool);
        delete device;
        std::snprintf(g_error, sizeof(g_error), "audrenStartAudioRenderer failed (0x%x)", rc);
        return 0;
    }

    rc = audrvVoiceInit(
        &device->driver,
        0,
        device->spec.channels,
        PcmFormat_Int16,
        device->spec.freq);
    if (R_FAILED(rc))
    {
        audrvClose(&device->driver);
        audrenExit();
        std::free(device->callbackBuffer);
        std::free(device->pool);
        delete device;
        std::snprintf(g_error, sizeof(g_error), "audrvVoiceInit failed (0x%x)", rc);
        return 0;
    }

    audrvVoiceSetDestinationMix(
        &device->driver,
        0,
        AUDREN_FINAL_MIX_ID);

    if (device->spec.channels == 1)
    {
        audrvVoiceSetMixFactor(
            &device->driver, 0, 1.0f, 0, 0);
        audrvVoiceSetMixFactor(
            &device->driver, 0, 1.0f, 0, 1);
    }
    else
    {
        audrvVoiceSetMixFactor(
            &device->driver, 0, 1.0f, 0, 0);
        audrvVoiceSetMixFactor(
            &device->driver, 0, 0.0f, 0, 1);
        audrvVoiceSetMixFactor(
            &device->driver, 0, 0.0f, 1, 0);
        audrvVoiceSetMixFactor(
            &device->driver, 0, 1.0f, 1, 1);
    }

    for (int i = 0; i < 2; ++i)
    {
        device->buffers[i].data_raw = device->pool;
        device->buffers[i].size = device->spec.size * 2;
        device->buffers[i].start_sample_offset = i * device->spec.samples;
        device->buffers[i].end_sample_offset =
            device->buffers[i].start_sample_offset + device->spec.samples;
    }

    g_device = device;

    if (obtained)
        *obtained = device->spec;

    return 1;
}

extern "C"
void SDL_PauseAudioDevice(SDL_AudioDeviceID deviceId, int pauseOn)
{
    std::lock_guard<std::mutex> lock(g_deviceMutex);

    if (deviceId != 1 || !g_device)
        return;

    g_device->paused.store(
        pauseOn != 0,
        std::memory_order_release);

    if (pauseOn == 0)
    {
        if (!g_device->running.load(std::memory_order_acquire))
        {
            g_device->running.store(true, std::memory_order_release);
            g_device->thread = std::thread(
                AudioThreadMain,
                g_device);
        }
    }

    return 0;
}

extern "C"
void SDL_CloseAudioDevice(SDL_AudioDeviceID deviceId)
{
    std::lock_guard<std::mutex> lock(g_deviceMutex);

    if (deviceId != 1 || !g_device)
        return;

    SwitchSdlAudioDevice *device = g_device;
    g_device = nullptr;

    device->running.store(false, std::memory_order_release);

    if (device->thread.joinable())
        device->thread.join();

    audrvClose(&device->driver);
    audrenExit();

    std::free(device->callbackBuffer);
    std::free(device->pool);
    delete device;
}

#endif
