#ifdef __SWITCH__

#include <cstring>
#include "binklib/bink.h"

// The original Bink runtime is a proprietary platform-specific binary and is not
// available for the Switch ARM64 port. Keep the public Bink ABI available so the
// renderer can initialize and gracefully skip movies until a real decoder is added.
//
// Important: BinkGetError() must stay empty. The engine calls R_Cinematic_CheckBinkError()
// after many Bink calls and treats a non-empty error string as an assertion failure.

RADDEFFUNC char PTR4* RADEXPLINK BinkGetError(void)
{
    static char noError[] = "";
    return noError;
}

RADDEFFUNC HBINK RADEXPLINK BinkOpen(const char PTR4 *name, U32 flags)
{
    (void)name;
    (void)flags;
    return 0;
}

RADDEFFUNC void RADEXPLINK BinkClose(HBINK bink)
{
    (void)bink;
}

RADDEFFUNC S32 RADEXPLINK BinkWait(HBINK bink)
{
    (void)bink;
    return 0;
}

RADDEFFUNC S32 RADEXPLINK BinkDoFrame(HBINK bink)
{
    (void)bink;
    return 0;
}

RADDEFFUNC void RADEXPLINK BinkNextFrame(HBINK bink)
{
    (void)bink;
}

RADDEFFUNC S32 RADEXPLINK BinkPause(HBINK bink, S32 pause)
{
    (void)bink;
    return pause;
}

RADDEFFUNC void RADEXPLINK BinkGetRealtime(HBINK bink, BINKREALTIME PTR4 *realtime, U32 frames)
{
    (void)bink;
    (void)frames;
    if (realtime)
        std::memset(realtime, 0, sizeof(*realtime));
}

RADDEFFUNC void RADEXPLINK BinkSetMemory(BINKMEMALLOC alloc, BINKMEMFREE freeFunc)
{
    (void)alloc;
    (void)freeFunc;
}

RADDEFFUNC void RADEXPLINK BinkSetIOSize(U32 ioSize)
{
    (void)ioSize;
}

RADDEFFUNC void RADEXPLINK BinkSetSoundTrack(U32 totalTracks, U32 PTR4 *tracks)
{
    (void)totalTracks;
    (void)tracks;
}

RADDEFFUNC S32 RADEXPLINK BinkSetSoundSystem(BINKSNDSYSOPEN open, UINTa param)
{
    (void)open;
    (void)param;
    return 0;
}

RADDEFFUNC void RADEXPLINK BinkSetMixBinVolumes(
    HBINK bink,
    U32 trackId,
    U32 PTR4 *volMixBins,
    S32 PTR4 *volumes,
    U32 total)
{
    (void)bink;
    (void)trackId;
    (void)volMixBins;
    (void)volumes;
    (void)total;
}

RADDEFFUNC S32 RADEXPLINK BinkControlBackgroundIO(HBINK bink, U32 control)
{
    (void)bink;
    (void)control;
    return 0;
}

RADDEFFUNC void RADEXPLINK BinkGetFrameBuffersInfo(HBINK bink, BINKFRAMEBUFFERS *buffers)
{
    (void)bink;
    if (buffers)
        std::memset(buffers, 0, sizeof(*buffers));
}

RADDEFFUNC void RADEXPLINK BinkRegisterFrameBuffers(HBINK bink, BINKFRAMEBUFFERS *buffers)
{
    (void)bink;
    (void)buffers;
}

#endif
