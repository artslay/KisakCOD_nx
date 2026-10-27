#ifdef __SWITCH__
#include <universal/profile.h>

ProfileScript profileScript = {};
int g_profileStack[256] = {};
int prof_parity[2] = {};
ProfileStack g_prof_stack[7] = {};
const dvar_t *profile = nullptr;
const dvar_t *profile_thread = nullptr;
const dvar_t *profile_rowcount = nullptr;

namespace
{
const char *s_switchProfilePageNames[] =
{
    "",
    "self",
    "total",
    "avgself",
    "avgtotal",
    "max",
    "maxself",
    "main",
    "updateScreen",
    "thread",
    "generic",
    "generic2",
    "probes",
    "probe",
    "gprobe",
    "frontend",
    "renderer",
    "renderer2",
    "renderer3",
    "cinematics",
    "phys",
    "shadowcookie",
    "water",
    "snd",
    "xanim",
    "xanim2",
    "script",
    "fx",
    "prediction",
    "trace",
    "cgtrace",
    "server",
    "trigger",
    "dynent",
    "aimassist",
    nullptr
};

const char *s_switchProfileThreadNames[] =
{
    "main",
    "backend",
    "worker0",
    "worker1",
    "cinematic",
    "titleserver",
    "database",
    "all",
    nullptr
};
}

void Profile_Init()
{
    // Scr_ProfileUpdate() reads the "profile" enum on every full server frame,
    // even when script profiling is disabled. The Switch profiling backend
    // must register the same public dvars as the shared profiling backend.
    profile = Dvar_RegisterEnum(
        "profile",
        s_switchProfilePageNames,
        0,
        DVAR_NOFLAG,
        "Type of profiling");
    profile_thread = Dvar_RegisterEnum(
        "profile_thread",
        s_switchProfileThreadNames,
        7,
        DVAR_NOFLAG,
        "Thread being profiled");
    profile_rowcount = Dvar_RegisterInt(
        "profile_rowcount",
        20,
        (DvarLimits)0x2800000000LL,
        DVAR_NOFLAG,
        "Profile row count");
}
void __cdecl Profile_Guard(int) {}
void __cdecl Profile_Unguard(int) {}
void __cdecl Profile_SetTotal(int, int) {}
void Profile_ResetScriptCounters() {}
void __cdecl Profile_ResetCounters(int) {}
void __cdecl Profile_Recover(int) {}
void __cdecl Profile_InitContext(int) {}
ProfileStack *__cdecl Profile_GetStackForContext(int context) { return context >= 0 && context < 7 ? &g_prof_stack[context] : nullptr; }
ProfileScript *__cdecl Profile_GetScript() { return &profileScript; }
int __cdecl Profile_GetEnumParity(uint32_t) { return 0; }
int __cdecl Profile_GetDisplayThread() { return 0; }
void __cdecl Profile_EndScripts(uint32_t) {}
void __cdecl Profile_EndScript(int) {}
void __cdecl Profile_BeginScripts(uint32_t) {}
void __cdecl Profile_BeginScript(int) {}
int __cdecl Profile_AddScriptName(const char *) { return 0; }
void __cdecl Profile_ResetCountersForContext(int, int) {}
const char *__cdecl Profile_MissingEnd() { return ""; }
void __cdecl Profile_Begin(int) {}
int __cdecl Profile_EndInternal(long double *) { return 0; }
#endif
