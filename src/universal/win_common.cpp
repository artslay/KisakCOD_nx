#include <universal/q_shared.h>
#include <win32/win_local.h>
#include <win32/win_net.h>

#include <universal/assertive.h>

#include <qcommon/qcommon.h>
#include <qcommon/threads.h>

#include <direct.h>
#include <io.h>
#include "com_memory.h"
#include "profile.h"

#if defined(_WIN32)
_RTL_CRITICAL_SECTION s_criticalSections[CRITSECT_COUNT];
#else
#include <mutex>
std::mutex s_criticalSections[CRITSECT_COUNT];
uint32_t s_criticalSectionsCount[CRITSECT_COUNT] = { 0 };
#endif

void Sys_InitializeCriticalSections()
{
#if defined(_WIN32)
	for (int critSect = 0; critSect < CRITSECT_COUNT; critSect++) {
		InitializeCriticalSection(&s_criticalSections[critSect]);
	}
#endif
}

int Sys_InterlockedIncrement(uint *addend)
{
    return InterlockedIncrement(addend);
}

int Sys_InterlockedDecrement(uint *addend)
{
    return InterlockedDecrement(addend);
}

uint32_t Win_InitThreads()
{
    HANDLE CurrentProcess;
    unsigned long result; 
    unsigned long cpuCount; 
    DWORD_PTR systemAffinityMask;
    unsigned long cpuOffset; 
    DWORD_PTR threadAffinityMask;
    DWORD_PTR affinityMaskBits[33];
    DWORD_PTR processAffinityMask; 

    CurrentProcess = GetCurrentProcess();
    result = GetProcessAffinityMask(CurrentProcess, &processAffinityMask, &systemAffinityMask);
    s_affinityMaskForProcess = processAffinityMask;
    cpuCount = 0;
    for (threadAffinityMask = 1; (processAffinityMask & ((DWORD_PTR)0 - threadAffinityMask)) != 0; threadAffinityMask *= 2)
    {
        if ((processAffinityMask & threadAffinityMask) != 0)
        {
            result = cpuCount;
            affinityMaskBits[cpuCount++] = threadAffinityMask;
            if (cpuCount == 32)
                break;
        }
        result = 2 * threadAffinityMask;
    }
    if (cpuCount > 1)
    {
        s_cpuCount = cpuCount;
        s_affinityMaskForCpu[0] = affinityMaskBits[0];
        result = affinityMaskBits[cpuCount - 1];
        s_affinityMaskForCpu[1] = result;
        if (cpuCount != 2)
        {
            if (cpuCount == 3)
            {
                s_affinityMaskForCpu[2] = affinityMaskBits[1];
            }
            else if (cpuCount == 4)
            {
                s_affinityMaskForCpu[2] = affinityMaskBits[1];
                result = affinityMaskBits[2];
                s_affinityMaskForCpu[3] = affinityMaskBits[2];
            }
            else
            {
                cpuOffset = (cpuCount - 2) / 3;
                iassert(1 + cpuOffset < (cpuCount - 1) - cpuOffset);
                s_affinityMaskForCpu[2] = affinityMaskBits[cpuOffset + 1];
                result = affinityMaskBits[cpuCount - 1 - cpuOffset];
                s_affinityMaskForCpu[3] = result;
                s_cpuCount = 4;
            }
        }
    }
    else
    {
        s_cpuCount = 1;
        s_affinityMaskForCpu[0] = -1;
    }
    return result;
}

// *(_DWORD *)(*(_DWORD *)(*((_DWORD *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 4)

void __cdecl Sys_Mkdir(const char *path)
{
    _mkdir(path);
}

void __cdecl Sys_ListFilteredFiles(
    HunkUser *user,
    const char *basedir,
    const char *subdirs,
    const char *filter,
    char **list,
    int *numfiles)
{
    char filename[256]; // [esp+10h] [ebp-338h] BYREF
    _finddata64i32_t findinfo; // [esp+110h] [ebp-238h] BYREF
    int findhandle; // [esp+23Ch] [ebp-10Ch]
    char search[260]; // [esp+240h] [ebp-108h] BYREF

    if (*numfiles < 0x1FFF)
    {
        if (strlen(subdirs))
            Com_sprintf(search, 0x100u, "%s\\%s\\*", basedir, subdirs);
        else
            Com_sprintf(search, 0x100u, "%s\\*", basedir);
        findhandle = _findfirst64i32(search, &findinfo);
        if (findhandle != -1)
        {
            do
            {
                if ((findinfo.attrib & 0x10) == 0
                    || I_stricmp(findinfo.name, ".") && I_stricmp(findinfo.name, "..") && I_stricmp(findinfo.name, "CVS"))
                {
                    if (*numfiles >= 0x1FFF)
                        break;
                    if (subdirs)
                        Com_sprintf(filename, 0x100u, "%s\\%s", subdirs, findinfo.name);
                    else
                        Com_sprintf(filename, 0x100u, "%s", findinfo.name);
                    if (Com_FilterPath(filter, filename, 0))
                        list[(*numfiles)++] = Hunk_CopyString(user, filename);
                }
            } while (_findnext64i32(findhandle, &findinfo) != -1);
            _findclose(findhandle);
        }
    }
}

BOOL __cdecl HasFileExtension(const char *name, const char *extension)
{
    char search[260]; // [esp+0h] [ebp-108h] BYREF

    Com_sprintf(search, 0x100u, "*.%s", extension);
    return I_stricmpwild(search, name) == 0;
}

int __cdecl Sys_CountFileList(char **list)
{
    int i; // [esp+0h] [ebp-4h]

    i = 0;
    if (list)
    {
        while (*list)
        {
            ++list;
            ++i;
        }
    }
    return i;
}

char **__cdecl Sys_ListFiles(
    const char *directory,
    const char *extension,
    const char *filter,
    int *numfiles,
    int wantsubs)
{
    char *v6; // eax
    char **v7; // [esp+4h] [ebp-264h]
    _finddata64i32_t findinfo; // [esp+18h] [ebp-250h] BYREF
    int flag; // [esp+140h] [ebp-128h]
    char **listCopy; // [esp+144h] [ebp-124h]
    int findhandle; // [esp+148h] [ebp-120h]
    char *(*list)[8192]; // [esp+14Ch] [ebp-11Ch]
    int nfiles; // [esp+150h] [ebp-118h] BYREF
    HunkUser *user; // [esp+154h] [ebp-114h]
    char search[256]; // [esp+160h] [ebp-108h] BYREF
    int i; // [esp+264h] [ebp-4h]

    LargeLocal list_large_local(0x8000); // [esp+158h] [ebp-110h] BYREF
    //LargeLocal::LargeLocal(&list_large_local, 0x8000);
    //list = (char *(*)[8192])LargeLocal::GetBuf(&list_large_local);
    list = (char *(*)[8192])list_large_local.GetBuf();
    if (filter)
    {
        user = Hunk_UserCreate(0x20000, "Sys_ListFiles", 0, 0, 3);
        nfiles = 0;
        Sys_ListFilteredFiles(user, directory, "", filter, (char **)list, &nfiles);
        (*list)[nfiles] = 0;
        *numfiles = nfiles;
        if (nfiles)
        {
            listCopy = (char **)Hunk_UserAlloc(user, 4 * nfiles + 8, 4);
            *listCopy++ = (char *)user;
            for (i = 0; i < nfiles; ++i)
                listCopy[i] = (*list)[i];
            listCopy[i] = 0;
            //LargeLocal::~LargeLocal(&list_large_local);
            return listCopy;
        }
        else
        {
            Hunk_UserDestroy(user);
            //LargeLocal::~LargeLocal(&list_large_local);
            return 0;
        }
    }
    else
    {
        if (!extension)
            extension = "";
        if (*extension != 47 || extension[1])
        {
            flag = 16;
        }
        else
        {
            extension = "";
            flag = 0;
        }
        if (*extension)
            Com_sprintf(search, 0x100u, "%s\\*.%s", directory, extension);
        else
            Com_sprintf(search, 0x100u, "%s\\*", directory);
        nfiles = 0;
        findhandle = _findfirst64i32(search, &findinfo);
        if (findhandle == -1)
        {
            *numfiles = 0;
            //LargeLocal::~LargeLocal(&list_large_local);
            return 0;
        }
        else
        {
            user = Hunk_UserCreate(0x20000, "Sys_ListFiles", 0, 0, 3);
            do
            {
                if ((!wantsubs && flag != (findinfo.attrib & 0x10) || wantsubs && (findinfo.attrib & 0x10) != 0)
                    && ((findinfo.attrib & 0x10) == 0
                        || I_stricmp(findinfo.name, ".") && I_stricmp(findinfo.name, "..") && I_stricmp(findinfo.name, "CVS"))
                    && (!*extension || HasFileExtension(findinfo.name, extension)))
                {
                    v6 = Hunk_CopyString(user, findinfo.name);
                    (*list)[nfiles++] = v6;
                    if (nfiles == 0x1FFF)
                        break;
                }
            } while (_findnext64i32(findhandle, &findinfo) != -1);
            (*list)[nfiles] = 0;
            _findclose(findhandle);
            *numfiles = nfiles;
            if (nfiles)
            {
                listCopy = (char **)Hunk_UserAlloc(user, 4 * nfiles + 8, 4);
                *listCopy++ = (char *)user;
                for (i = 0; i < nfiles; ++i)
                    listCopy[i] = (*list)[i];
                listCopy[i] = 0;
                v7 = listCopy;
                //LargeLocal::~LargeLocal(&list_large_local);
                return v7;
            }
            else
            {
                Hunk_UserDestroy(user);
                //LargeLocal::~LargeLocal(&list_large_local);
                return 0;
            }
        }
    }
}


char cwd[256];
const char *__cdecl Sys_DefaultCDPath()
{
    return "";
}

char exePath[256];
