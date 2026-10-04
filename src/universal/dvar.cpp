#include <universal/q_shared.h>
#include <qcommon/qcommon.h>

#include <win32/win_local.h>
#include <qcommon/cmd.h>
#include "com_files.h"
#include "com_memory.h"
#include <stringed/stringed_hooks.h>
#include "q_parse.h"
#include <gfx_d3d/r_dvars.h>
#include <win32/win_net.h>
#include <devgui/devgui.h>
#include "com_math.h"
#include "memfile.h"        // Dvar_Save/LoadDvars

#include <algorithm>

#ifdef KISAK_MP
#include <client_mp/client_mp.h>
#endif

const dvar_s *dvar_cheats;
int dvar_modifiedFlags;

LONG isSortingDvars;

static FastCriticalSection g_dvarCritSect;

static dvar_s* dvarHashTable[0x100];
    
static dvar_s dvarPool[0x1000];
static dvar_s* sortedDvars[0x1000];
static bool areDvarsSorted;
static LONG isSortedDvars;
static int dvarCount;

bool isDvarSystemActive;
bool isLoadingAutoExecGlobalFlag;

static int generateHashValue(const char* fname)
{
    if (!fname)
    {
        Com_Error(ERR_DROP, "null name in generateHashValue");
    }
    int hash = 0;
    for (int i = 0; fname[i]; ++i)
        hash += tolower(fname[i]) * (i + 119);
    return (uint8_t)hash;
}

int __cdecl Dvar_Command()
{
    const char *v0; // eax
    const char *v2; // eax
    const char *v3; // eax
    const char *v4; // eax
    const char *v5; // [esp-4h] [ebp-100Ch]
    char combined[4096]; // [esp+0h] [ebp-1008h] BYREF
    dvar_s *dvar; // [esp+1004h] [ebp-4h]

    v0 = Cmd_Argv(0);
    dvar = (dvar_s *)Dvar_FindVar(v0);
    if (!dvar)
        return 0;
    if (Cmd_Argc() == 1)
    {
        v5 = Dvar_DisplayableResetValue(dvar);
        v2 = Dvar_DisplayableValue(dvar);
        Com_Printf(CON_CHANNEL_DONT_FILTER, "\"%s\" is: \"%s^7\" default: \"%s^7\"\n", dvar->name, v2, v5);
        if (Dvar_HasLatchedValue(dvar))
        {
            v3 = Dvar_DisplayableLatchedValue(dvar);
            Com_Printf(CON_CHANNEL_DONT_FILTER, "latched: \"%s\"\n", v3);
        }
        Dvar_PrintDomain(dvar->type, dvar->domain);
        return 1;
    }
    else
    {
        Dvar_GetCombinedString(combined, 1);
        v4 = Cmd_Argv(0);
        Dvar_SetCommand(v4, combined);
        return 1;
    }
}

void __cdecl Dvar_GetCombinedString(char *combined, int first)
{
    char *v2; // eax
    int c; // [esp+10h] [ebp-10h]
    int l; // [esp+14h] [ebp-Ch]
    int len; // [esp+18h] [ebp-8h]

    c = Cmd_Argc();
    *combined = 0;
    l = 0;
    while (first < c)
    {
        len = strlen(Cmd_Argv(first)) + 1;
        if (len + l >= 4094)
            break;
        v2 = (char *)Cmd_Argv(first);
        I_strncat(combined, 4096, v2);
        if (first != c - 1)
            I_strncat(combined, 4096, " ");
        l += len;
        ++first;
    }
}

void __cdecl Dvar_WriteVariables(int f)
{
    Dvar_ForEach((void(__cdecl *)(const dvar_s *, void *))Dvar_WriteSingleVariable, &f);
}

void __cdecl Dvar_WriteSingleVariable(const dvar_s *dvar, int *userData)
{
    const char *v2; // eax
    int f; // [esp+0h] [ebp-4h]

    if (I_stricmp(dvar->name, "cl_cdkey"))
    {
        if ((dvar->flags & 1) != 0)
        {
            f = *userData;
            v2 = Dvar_DisplayableLatchedValue(dvar);
            FS_Printf(f, "seta %s \"%s\"\n", dvar->name, v2);
        }
    }
}

void __cdecl Dvar_WriteDefaults(int f)
{
    Dvar_ForEach((void(__cdecl *)(const dvar_s *, void *))Dvar_WriteSingleDefault, &f);
}

void __cdecl Dvar_WriteSingleDefault(const dvar_s *dvar, int *userData)
{
    const char *v2; // eax
    int f; // [esp+0h] [ebp-4h]

    if (I_stricmp(dvar->name, "cl_cdkey"))
    {
        if ((dvar->flags & 0x40C0) == 0)
        {
            f = *userData;
            v2 = Dvar_DisplayableResetValue(dvar);
            FS_Printf(f, "set %s \"%s\"\n", dvar->name, v2);
        }
    }
}

void __cdecl PBdvar_set(const char *var_name, char *value)
{
    if (Dvar_FindVar(var_name))
        Dvar_SetFromStringByName(var_name, value);
}

char *__cdecl Dvar_InfoString(int localClientNum, char bit)
{
    const char *UsernameForLocalClient; // eax

    info1[0] = 0;
    Dvar_ForEach((void(__cdecl *)(const dvar_s *, void *))Dvar_InfoStringSingle, &bit);
#ifdef KISAK_MP
    if ((bit & 2) != 0)
    {
        UsernameForLocalClient = CL_GetUsernameForLocalClient();
        Info_SetValueForKey(info1, "name", UsernameForLocalClient);
    }
#endif
    return info1;
}

void __cdecl Dvar_InfoStringSingle(const dvar_s *dvar, uint32_t *userData)
{
    const char *v2; // eax

    if ((*userData & dvar->flags) != 0)
    {
        v2 = Dvar_DisplayableValue(dvar);
        Info_SetValueForKey(info1, (char *)dvar->name, v2);
    }
}

char *__cdecl Dvar_InfoString_Big(int bit)
{
    info2[0] = 0;
    Dvar_ForEach((void(__cdecl *)(const dvar_s *, void *))Dvar_InfoStringSingle_Big, &bit);
    return info2;
}

void __cdecl Dvar_InfoStringSingle_Big(const dvar_s *dvar, uint32_t *userData)
{
    const char *v2; // eax

    if ((*userData & dvar->flags) != 0)
    {
        v2 = Dvar_DisplayableValue(dvar);
        Info_SetValueForKey_Big(info2, (char *)dvar->name, v2);
    }
}

void __cdecl Dvar_ForEach(void(__cdecl *callback)(const dvar_s *, void *), void *userData)
{
    int dvarIter; // [esp+4h] [ebp-4h]

    InterlockedIncrement(&g_dvarCritSect.readCount);
    while (g_dvarCritSect.writeCount)
        NET_Sleep(0);
    if (!areDvarsSorted)
        Dvar_Sort();
    for (dvarIter = 0; dvarIter < dvarCount; ++dvarIter)
        callback(sortedDvars[dvarIter], userData);
    if (g_dvarCritSect.readCount <= 0)
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\universal\\../qcommon/threads_interlock.h",
            76,
            0,
            "%s",
            "critSect->readCount > 0");
    InterlockedDecrement(&g_dvarCritSect.readCount);
}

bool __cdecl CompareDvars(const dvar_t *cached0, const dvar_t *cached1)
{
    return I_stricmp(cached0->name, cached1->name) < 0;
}

void Dvar_Sort()
{
    if (InterlockedCompareExchange(&isSortingDvars, 1, 0))
    {
        while (isSortingDvars)
            NET_Sleep(1);
    }
    else
    {
        std::sort(sortedDvars, sortedDvars + dvarCount, CompareDvars);
        areDvarsSorted = 1;
        isSortingDvars = 0;
    }
}

void __cdecl Dvar_ForEachName(void(__cdecl *callback)(const char *))
{
    int dvarIter; // [esp+4h] [ebp-4h]

    InterlockedIncrement(&g_dvarCritSect.readCount);
    while (g_dvarCritSect.writeCount)
        NET_Sleep(0);
    if (!areDvarsSorted)
        Dvar_Sort();
    for (dvarIter = 0; dvarIter < dvarCount; ++dvarIter)
        callback(sortedDvars[dvarIter]->name);
    if (g_dvarCritSect.readCount <= 0)
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\universal\\../qcommon/threads_interlock.h",
            76,
            0,
            "%s",
            "critSect->readCount > 0");
    InterlockedDecrement(&g_dvarCritSect.readCount);
}

const dvar_s *__cdecl Dvar_GetAtIndex(uint32_t index)
{
    if (index >= dvarCount)
        MyAssertHandler(
            ".\\universal\\dvar.cpp",
            125,
            0,
            "index doesn't index dvarCount\n\t%i not in [0, %i)",
            index,
            dvarCount);
    return &dvarPool[index];
}

void __cdecl Dvar_SetInAutoExec(bool inAutoExec)
{
    isLoadingAutoExecGlobalFlag = inAutoExec;
}

bool __cdecl Dvar_IsSystemActive()
{
    return isDvarSystemActive;
}

char __cdecl Dvar_IsValidName(const char *dvarName)
{
    char nameChar; // [esp+3h] [ebp-5h]
    int index; // [esp+4h] [ebp-4h]

    if (!dvarName)
        return 0;
    for (index = 0; dvarName[index]; ++index)
    {
        nameChar = dvarName[index];
        if (!isalnum(nameChar) && nameChar != 95)
            return 0;
    }
    return 1;
}

const char *__cdecl Dvar_EnumToString(const dvar_s *dvar)
{
    if (!dvar)
        MyAssertHandler(".\\universal\\dvar.cpp", 278, 0, "%s", "dvar");
    if (!dvar->name)
        MyAssertHandler(".\\universal\\dvar.cpp", 279, 0, "%s", "dvar->name");
    if (dvar->type != DVAR_TYPE_ENUM)
        MyAssertHandler(
            ".\\universal\\dvar.cpp",
            280,
            0,
            "%s\n\t(dvar->name) = %s",
            "(dvar->type == DVAR_TYPE_ENUM)",
            dvar->name);
    if (!dvar->domain.enumeration.strings)
        MyAssertHandler(
            ".\\universal\\dvar.cpp",
            281,
            0,
            "%s\n\t(dvar->name) = %s",
            "(dvar->domain.enumeration.strings)",
            dvar->name);
    if ((dvar->current.integer < 0 || dvar->current.integer >= dvar->domain.enumeration.stringCount)
        && dvar->current.integer)
    {
        MyAssertHandler(
            ".\\universal\\dvar.cpp",
            282,
            0,
            "%s\n\t(dvar->current.integer) = %i",
            "(dvar->current.integer >= 0 && dvar->current.integer < dvar->domain.enumeration.stringCount || dvar->current.integer == 0)",
            dvar->current.integer);
    }
    if (dvar->domain.enumeration.stringCount)
        return dvar->domain.enumeration.strings[dvar->current.integer];
    else
        return "";
}

const char *__cdecl Dvar_IndexStringToEnumString(const dvar_s *dvar, const char *indexString)
{
    signed int v3; // [esp+0h] [ebp-1Ch]
    int enumIndex; // [esp+14h] [ebp-8h]
    int indexStringIndex; // [esp+18h] [ebp-4h]

    if (!dvar)
        MyAssertHandler(".\\universal\\dvar.cpp", 296, 0, "%s", "dvar");
    if (!dvar->name)
        MyAssertHandler(".\\universal\\dvar.cpp", 297, 0, "%s", "dvar->name");
    if (dvar->type != DVAR_TYPE_ENUM)
        MyAssertHandler(
            ".\\universal\\dvar.cpp",
            298,
            0,
            "%s\n\t(dvar->name) = %s",
            "(dvar->type == DVAR_TYPE_ENUM)",
            dvar->name);
    if (!dvar->domain.enumeration.strings)
        MyAssertHandler(
            ".\\universal\\dvar.cpp",
            299,
            0,
            "%s\n\t(dvar->name) = %s",
            "(dvar->domain.enumeration.strings)",
            dvar->name);
    if (!indexString)
        MyAssertHandler(".\\universal\\dvar.cpp", 300, 0, "%s\n\t(dvar->name) = %s", "(indexString)", dvar->name);
    if (!dvar->domain.enumeration.stringCount)
        return "";
    v3 = strlen(indexString);
    for (indexStringIndex = 0; indexStringIndex < v3; ++indexStringIndex)
    {
        if (!isdigit(indexString[indexStringIndex]))
            return "";
    }
    enumIndex = atoi(indexString);
    if (enumIndex >= 0 && enumIndex < dvar->domain.enumeration.stringCount)
        return dvar->domain.enumeration.strings[enumIndex];
    else
        return "";
}

const char *__cdecl Dvar_DisplayableValue(const dvar_s *dvar)
{
    if (!dvar)
        MyAssertHandler(".\\universal\\dvar.cpp", 519, 0, "%s", "dvar");
    return Dvar_ValueToString(dvar, dvar->current);
}

const char *__cdecl Dvar_ValueToString(const dvar_s *dvar, DvarValue value)
{
    const char *result; // eax
    const char *v3; // eax
    const char *v4; // [esp+30h] [ebp-Ch]

    switch (dvar->type)
    {
    case 0u:
        if (value.enabled)
            v4 = "1";
        else
            v4 = "0";
        result = v4;
        break;
    case 1u:
        result = va("%g", value.value);
        break;
    case 2u:
        result = va("%g %g", value.value, value.vector[1]);
        break;
    case 3u:
        result = va("%g %g %g", value.value, value.vector[1], value.vector[2]);
        break;
    case 4u:
        result = va("%g %g %g %g", value.value, value.vector[1], value.vector[2], value.vector[3]);
        break;
    case 5u:
        result = va("%i", value.integer);
        break;
    case 6u:
        if ((value.integer < 0 || value.integer >= dvar->domain.enumeration.stringCount) && value.integer)
            MyAssertHandler(
                ".\\universal\\dvar.cpp",
                346,
                0,
                "%s\n\t(value.integer) = %i",
                "(value.integer >= 0 && value.integer < dvar->domain.enumeration.stringCount || value.integer == 0)",
                value.integer);
        if (dvar->domain.enumeration.stringCount)
            result = dvar->domain.enumeration.strings[value.integer];
        else
            result = "";
        break;
    case 7u:
        if (!value.integer)
            MyAssertHandler(".\\universal\\dvar.cpp", 352, 0, "%s\n\t(dvar->name) = %s", "(value.string)", dvar->name);
        result = va("%s", value.string);
        break;
    case 8u:
        result = va(
            "%g %g %g %g",
            (double)value.color[0] * 0.003921568859368563,
            (double)value.color[1] * 0.003921568859368563,
            (double)value.color[2] * 0.003921568859368563,
            (double)value.color[3] * 0.003921568859368563);
        break;
    default:
        if (!alwaysfails)
        {
            v3 = va("unhandled dvar type '%i'", dvar->type);
            MyAssertHandler(".\\universal\\dvar.cpp", 357, 1, v3);
        }
        result = "";
        break;
    }
    return result;
}

const char *__cdecl Dvar_DisplayableResetValue(const dvar_s *dvar)
{
    if (!dvar)
        MyAssertHandler(".\\universal\\dvar.cpp", 527, 0, "%s", "dvar");
    return Dvar_ValueToString(dvar, dvar->reset);
}

const char *__cdecl Dvar_DisplayableLatchedValue(const dvar_s *dvar)
{
    if (!dvar)
        MyAssertHandler(".\\universal\\dvar.cpp", 535, 0, "%s", "dvar");
    return Dvar_ValueToString(dvar, dvar->latched);
}

char __cdecl Dvar_ValueInDomain(uint8_t type, DvarValue value, DvarLimits domain)
{
    char result; // al
    const char *v4; // eax
    bool v5; // [esp+8h] [ebp-Ch]

    switch (type)
    {
    case 0u:
        if (value.color[0] != 1 && value.enabled)
            MyAssertHandler(".\\universal\\dvar.cpp", 636, 0, "%s", "value.enabled == true || value.enabled == false");
        result = 1;
        break;
    case 1u:
        result = domain.value.min <= (double)value.value && domain.value.max >= (double)value.value;
        break;
    case 2u:
        result = Dvar_VectorInDomain(&value.value, 2, domain.value.min, domain.value.max);
        break;
    case 3u:
        result = Dvar_VectorInDomain(&value.value, 3, domain.value.min, domain.value.max);
        break;
    case 4u:
        result = Dvar_VectorInDomain(&value.value, 4, domain.value.min, domain.value.max);
        break;
    case 5u:
        if (domain.enumeration.stringCount > domain.integer.max)
            MyAssertHandler(".\\universal\\dvar.cpp", 640, 0, "%s", "domain.integer.min <= domain.integer.max");
        result = value.integer >= domain.enumeration.stringCount && value.integer <= domain.integer.max;
        break;
    case 6u:
        v5 = value.integer >= 0 && value.integer < domain.enumeration.stringCount || !value.integer;
        result = v5;
        break;
    case 7u:
        result = 1;
        break;
    case 8u:
        result = 1;
        break;
    default:
        if (!alwaysfails)
        {
            v4 = va("unhandled dvar type '%i'", type);
            MyAssertHandler(".\\universal\\dvar.cpp", 676, 1, v4);
        }
        result = 0;
        break;
    }
    return result;
}

char __cdecl Dvar_VectorInDomain(const float *vector, int components, float min, float max)
{
    int channel; // [esp+0h] [ebp-4h]

    for (channel = 0; channel < components; ++channel)
    {
        if (min > (double)vector[channel])
            return 0;
        if (max < (double)vector[channel])
            return 0;
    }
    return 1;
}

const char *__cdecl Dvar_DomainToString_Internal(
    uint8_t type,
    DvarLimits domain,
    char *outBuffer,
    uint32_t outBufferLen,
    int *outLineCount)
{
    const char *v4; // eax
    char *outBufferEnd; // [esp+14h] [ebp-10h]
    char *outBufferWalk; // [esp+18h] [ebp-Ch]
    int charsWritten; // [esp+1Ch] [ebp-8h]
    int charsWrittena; // [esp+1Ch] [ebp-8h]
    int stringIndex; // [esp+20h] [ebp-4h]

    iassert(outBuffer);
    iassert(outBufferLen);

    outBufferEnd = (char*)(outBuffer + outBufferLen);
    if (outLineCount)
        *outLineCount = 0;

    switch (type)
    {
    case 0u:
        _snprintf((char *)outBuffer, outBufferLen, "Domain is 0 or 1");
        break;
    case 1u:
        if (domain.value.min == -FLT_MAX)
        {
            if (domain.value.max == FLT_MAX)
                _snprintf((char *)outBuffer, outBufferLen, "Domain is any number");
            else
                _snprintf((char *)outBuffer, outBufferLen, "Domain is any number %g or smaller", domain.value.max);
        }
        else if (domain.value.max == FLT_MAX)
        {
            _snprintf((char *)outBuffer, outBufferLen, "Domain is any number %g or bigger", domain.value.min);
        }
        else
        {
            _snprintf(
                (char *)outBuffer,
                outBufferLen,
                "Domain is any number from %g to %g",
                domain.value.min,
                domain.value.max);
        }
        break;
    case 2u:
        Dvar_VectorDomainToString(2, domain, outBuffer, outBufferLen);
        break;
    case 3u:
        Dvar_VectorDomainToString(3, domain, outBuffer, outBufferLen);
        break;
    case 4u:
        Dvar_VectorDomainToString(4, domain, outBuffer, outBufferLen);
        break;
    case 5u:
        if (domain.enumeration.stringCount == 0x80000000)
        {
            if (domain.integer.max == 0x7FFFFFFF)
                _snprintf((char *)outBuffer, outBufferLen, "Domain is any integer");
            else
                _snprintf((char *)outBuffer, outBufferLen, "Domain is any integer %i or smaller", domain.integer.max);
        }
        else if (domain.integer.max == 0x7FFFFFFF)
        {
            _snprintf(
                (char *)outBuffer,
                outBufferLen,
                "Domain is any integer %i or bigger",
                domain.enumeration.stringCount);
        }
        else
        {
            _snprintf(
                (char *)outBuffer,
                outBufferLen,
                "Domain is any integer from %i to %i",
                domain.enumeration.stringCount,
                domain.integer.max);
        }
        break;
    case 6u:
        charsWritten = _snprintf((char *)outBuffer, outBufferLen, "Domain is one of the following:");
        if (charsWritten >= 0)
        {
            outBufferWalk = (char *)(charsWritten + outBuffer);
            for (stringIndex = 0; stringIndex < domain.enumeration.stringCount; ++stringIndex)
            {
                charsWrittena = _snprintf(
                    outBufferWalk,
                    outBufferEnd - outBufferWalk,
                    "\n  %2i: %s",
                    stringIndex,
                    domain.enumeration.strings[stringIndex]);
                if (charsWrittena < 0)
                    break;
                if (outLineCount)
                    ++*outLineCount;
                outBufferWalk += charsWrittena;
            }
        }
        break;
    case 7u:
        _snprintf((char *)outBuffer, outBufferLen, "Domain is any text");
        break;
    case 8u:
        _snprintf((char *)outBuffer, outBufferLen, "Domain is any 4-component color, in RGBA format");
        break;
    default:
        if (!alwaysfails)
        {
            v4 = va("unhandled dvar type '%i'", type);
            MyAssertHandler(".\\universal\\dvar.cpp", 794, 1, v4);
        }
        *(_BYTE *)outBuffer = 0;
        break;
    }
    *(outBufferEnd - 1) = 0;
    return (const char *)outBuffer;
}

const char *__cdecl Dvar_DomainToString(
    uint8_t type,
    DvarLimits *domain,
    char *outBuffer,
    uint32_t outBufferLen)
{
    return Dvar_DomainToString_Internal(type, *domain, outBuffer, outBufferLen, 0);
}

void __cdecl Dvar_VectorDomainToString(int components, DvarLimits domain, char *outBuffer, uint32_t outBufferLen)
{
    if (domain.value.min == -FLT_MAX)
    {
        if (domain.value.max == FLT_MAX)
            _snprintf(outBuffer, outBufferLen, "Domain is any %iD vector", components);
        else
            _snprintf(
                outBuffer,
                outBufferLen,
                "Domain is any %iD vector with components %g or smaller",
                components,
                domain.value.max);
    }
    else if (domain.value.max == FLT_MAX)
    {
        _snprintf(
            outBuffer,
            outBufferLen,
            "Domain is any %iD vector with components %g or bigger",
            components,
            domain.value.min);
    }
    else
    {
        _snprintf(
            outBuffer,
            outBufferLen,
            "Domain is any %iD vector with components from %g to %g",
            components,
            domain.value.min,
            domain.value.max);
    }
}

const char *Dvar_DomainToString_GetLines(
    uint8_t type,
    DvarLimits *domain,
    char *outBuffer,
    uint32_t outBufferLen,
    int *outLineCount)
{
    if (!outLineCount)
        MyAssertHandler(".\\universal\\dvar.cpp", 812, 0, "%s", "outLineCount");
    return Dvar_DomainToString_Internal(type, *domain, outBuffer, outBufferLen, outLineCount);
}

void __cdecl Dvar_PrintDomain(uint8_t type, DvarLimits domain)
{
    //const char *v2; // eax
    //__int64 v3; // [esp-8h] [ebp-410h]
    //char domainBuffer[1024]; // [esp+0h] [ebp-408h] BYREF
    //
    //HIDWORD(v3) = 1024;
    //LODWORD(v3) = domainBuffer;
    //v2 = Dvar_DomainToString(type, domain, v3);
    //Com_Printf(CON_CHANNEL_SYSTEM, "  %s\n", v2);
    char domainBuffer[1024];
    Com_Printf(CON_CHANNEL_SYSTEM, "  %s\n", Dvar_DomainToString(type, &domain, domainBuffer, sizeof(domainBuffer)));
}

bool __cdecl Dvar_HasLatchedValue(const dvar_s *dvar)
{
    const int equal = Dvar_ValuesEqual(dvar->type, dvar->current, dvar->latched);
    return equal == 0;
}