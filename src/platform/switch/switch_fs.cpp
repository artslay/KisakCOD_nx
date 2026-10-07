#ifdef __SWITCH__
#include <switch.h>
#include <cstdio>
#include <fcntl.h>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <algorithm>
#include <vector>
#include <string>
#include <unordered_map>

#include <qcommon/unzip.h>

#include <universal/q_shared.h>
#include <universal/com_files.h>
#include <universal/com_memory.h>
#include <qcommon/com_fileaccess.h>
#include <qcommon/qcommon.h>
#include <stringed/stringed_hooks.h>

extern void Sys_Print(const char *text);
extern void Switch_LogWrite(const char *msg);

const dvar_t *fs_remotePCDirectory = nullptr;
const dvar_t *fs_remotePCName = nullptr;
const dvar_t *fs_homepath = nullptr;
const dvar_s *fs_debug = nullptr;
const dvar_s *fs_restrict = nullptr;
const dvar_s *fs_ignoreLocalized = nullptr;
const dvar_s *fs_basepath = nullptr;
const dvar_s *fs_copyfiles = nullptr;
const dvar_s *fs_cdpath = nullptr;
const dvar_s *fs_gameDirVar = nullptr;
const dvar_s *fs_basegame = nullptr;

int fs_fakeChkSum = 0;
int fs_numServerIwds = 0;
int fs_serverIwds[1024] = {};
int com_fileAccessed = 0;
void *g_writeLogEvent = nullptr;
int marker_com_files = 0;
void *g_writeLogCompleteEvent = nullptr;
int fs_loadStack = 0;
const char *fs_serverIwdNames[1024] = {};
int fs_checksumFeed = 0;
char fs_gamedir[256] = "main";
searchpath_s *fs_searchpaths = nullptr;

static fileHandleData_t g_fsh[65] = {};
static const char *const kSwitchRoot = "sdmc:/switch/KisakCOD/game";

struct SwitchIwdArchive
{
    std::string path;
    unzFile file = nullptr;
};

struct SwitchIwdEntry
{
    uint16_t archiveIndex = 0;
    unsigned long infoPosition = 0;
    uint32_t size = 0;
    uint32_t nextDuplicate = UINT32_MAX;
};

struct SwitchZipHandle
{
    unzFile file = nullptr;
    uint32_t size = 0;
};

static std::vector<SwitchIwdArchive> g_iwdArchives;
static std::vector<SwitchIwdEntry> g_iwdEntryRecords;
static std::unordered_map<std::string, uint32_t> g_iwdEntries;
static SwitchZipHandle g_zipHandles[65] = {};

static int AllocHandle();

static std::string SwitchNormalizePath(const char *path)
{
    std::string normalized;
    if (!path)
        return normalized;

    normalized.reserve(std::strlen(path));
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(path); *p; ++p)
    {
        char c = static_cast<char>(*p);
        if (c == '\\')
            c = '/';
        normalized.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    while (normalized.rfind("./", 0) == 0)
        normalized.erase(0, 2);
    return normalized;
}

static void Switch_ClearIwdIndex()
{
    for (SwitchIwdArchive &archive : g_iwdArchives)
    {
        if (archive.file)
            unzClose(archive.file);
    }
    g_iwdArchives.clear();
    g_iwdEntryRecords.clear();
    g_iwdEntries.clear();
}

static void Switch_IndexIwdArchive(const char *archivePath)
{
    unzFile archiveFile = unzOpen(archivePath);
    if (!archiveFile)
        return;

    SwitchIwdArchive archive;
    archive.path = archivePath;
    archive.file = archiveFile;
    const uint16_t archiveIndex = static_cast<uint16_t>(g_iwdArchives.size());
    g_iwdArchives.push_back(std::move(archive));

    unz_global_info globalInfo = {};
    if (unzGetGlobalInfo(archiveFile, &globalInfo) == UNZ_OK)
    {
        g_iwdEntryRecords.reserve(
            g_iwdEntryRecords.size() + globalInfo.number_entry);
        g_iwdEntries.reserve(
            g_iwdEntries.size() + globalInfo.number_entry);
    }

    int entryCount = 0;
    if (unzGoToFirstFile(archiveFile) == UNZ_OK)
    {
        do
        {
            char name[256] = {};
            unz_file_info info = {};
            unsigned long infoPosition = 0;

            if (unzGetCurrentFileInfo(
                    archiveFile,
                    &info,
                    name,
                    sizeof(name),
                    nullptr,
                    0,
                    nullptr,
                    0) != UNZ_OK)
                continue;

            if (unzGetCurrentFileInfoPosition(
                    archiveFile,
                    &infoPosition) != UNZ_OK)
                continue;

            const std::string normalizedName = SwitchNormalizePath(name);
            if (normalizedName.empty() || normalizedName.back() == '/')
                continue;

            SwitchIwdEntry entry;
            entry.archiveIndex = archiveIndex;
            entry.infoPosition = infoPosition;
            entry.size = static_cast<uint32_t>(info.uncompressed_size);

            // Keep every packed copy of a qpath. The original filesystem
            // keeps each IWD as a separate search path, so a duplicate must
            // not overwrite the earlier record. The newest record is the
            // head of the duplicate chain and therefore has precedence.
            const uint32_t entryIndex =
                static_cast<uint32_t>(g_iwdEntryRecords.size());
            const auto existing = g_iwdEntries.find(normalizedName);
            if (existing != g_iwdEntries.end())
                entry.nextDuplicate = existing->second;

            g_iwdEntryRecords.push_back(entry);
            if (existing != g_iwdEntries.end())
                existing->second = entryIndex;
            else
                g_iwdEntries.emplace(normalizedName, entryIndex);

            ++entryCount;
        } while (unzGoToNextFile(archiveFile) == UNZ_OK);
    }

    char trace[192];
    std::snprintf(
        trace,
        sizeof(trace),
        "[SWITCH IWD] indexed %s entries=%d\n",
        archivePath,
        entryCount);
    Switch_LogWrite(trace);
}

static std::string Switch_GetStartupLanguage(const char *game)
{
    const char *base = fs_basepath && fs_basepath->current.string[0]
        ? fs_basepath->current.string
        : kSwitchRoot;

    char path[256];
    std::snprintf(
        path,
        sizeof(path),
        "%s/%s/localization.txt",
        base,
        game && *game ? game : "main");

    FILE *file = FS_FileOpenReadBinary(path);
    if (file)
    {
        char requested[64] = {};
        const size_t count = std::fread(
            requested,
            1,
            sizeof(requested) - 1,
            file);
        std::fclose(file);

        size_t begin = 0;
        while (begin < count &&
            (requested[begin] == ' ' ||
             requested[begin] == '\t'))
            ++begin;

        size_t end = begin;
        while (end < count &&
            requested[end] != '\r' &&
            requested[end] != '\n' &&
            requested[end] != ' ' &&
            requested[end] != '\t')
            ++end;

        requested[end] = 0;

        int languageIndex = 0;
        if (requested[begin] &&
            SEH_GetLanguageIndexForName(requested + begin, &languageIndex))
        {
            const char *languageName = SEH_GetLanguageName(languageIndex);
            if (loc_language)
                Dvar_SetInt((dvar_s *)loc_language, languageIndex);
            return languageName ? languageName : "english";
        }
    }

    return "english";
}

static bool Switch_GetLocalizedIwdLanguage(
    const char *filename,
    char *languageName,
    size_t languageNameSize)
{
    if (!filename || !languageName || languageNameSize == 0)
        return false;

    const std::string normalized = SwitchNormalizePath(filename);
    if (normalized.rfind("localized_", 0) != 0)
        return false;

    size_t pos = 10;
    const size_t start = pos;
    while (pos < normalized.size() &&
        std::isalpha(static_cast<unsigned char>(normalized[pos])))
        ++pos;

    if (pos == start)
        return false;

    const size_t length = pos - start;
    if (length >= languageNameSize)
        return false;

    std::memcpy(languageName, normalized.data() + start, length);
    languageName[length] = 0;
    return true;
}

static void Switch_IndexIwdArchives(const char *game)
{
    Switch_ClearIwdIndex();

    const char *base = fs_basepath && fs_basepath->current.string[0]
        ? fs_basepath->current.string
        : kSwitchRoot;
    const char *gameName = game && *game ? game : "main";
    const std::string selectedLanguage = Switch_GetStartupLanguage(gameName);

    char gamePath[256];
    std::snprintf(
        gamePath,
        sizeof(gamePath),
        "%s/%s",
        base,
        gameName);

    DIR *directory = opendir(gamePath);
    if (!directory)
    {
        char trace[192];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH IWD] game directory missing path=%s\n",
            gamePath);
        Switch_LogWrite(trace);
        return;
    }

    std::vector<std::string> regularArchives;
    std::vector<std::string> localizedArchives;

    while (dirent *entry = readdir(directory))
    {
        if (!entry->d_name || entry->d_name[0] == '.')
            continue;

        const std::string normalizedName = SwitchNormalizePath(entry->d_name);
        if (normalizedName.size() < 4 ||
            normalizedName.compare(
                normalizedName.size() - 4,
                4,
                ".iwd") != 0)
            continue;

        char localizedLanguage[64] = {};
        const bool localized = Switch_GetLocalizedIwdLanguage(
            normalizedName.c_str(),
            localizedLanguage,
            sizeof(localizedLanguage));

        if (localized)
        {
            if (I_stricmp(localizedLanguage, selectedLanguage.c_str()) != 0)
                continue;
            localizedArchives.emplace_back(entry->d_name);
        }
        else
        {
            regularArchives.emplace_back(entry->d_name);
        }
    }

    closedir(directory);

    auto archiveSort = [](const std::string &a, const std::string &b)
    {
        const std::string na = SwitchNormalizePath(a.c_str());
        const std::string nb = SwitchNormalizePath(b.c_str());
        return na < nb;
    };

    std::sort(regularArchives.begin(), regularArchives.end(), archiveSort);
    std::sort(localizedArchives.begin(), localizedArchives.end(), archiveSort);

    int archiveCount = 0;

    auto indexArchives = [&](const std::vector<std::string> &archives)
    {
        for (const std::string &archiveName : archives)
        {
            char archivePath[256];
            std::snprintf(
                archivePath,
                sizeof(archivePath),
                "%s/%s/%s",
                base,
                gameName,
                archiveName.c_str());

            struct stat st = {};
            if (stat(archivePath, &st) != 0 || !S_ISREG(st.st_mode))
                continue;

            Switch_IndexIwdArchive(archivePath);
            ++archiveCount;
        }
    };

    // Keep normal IWDs first so selected localized archives have precedence
    // over ordinary assets, matching the original search-path semantics.
    indexArchives(regularArchives);
    indexArchives(localizedArchives);

    char trace[224];
    std::snprintf(
        trace,
        sizeof(trace),
        "[SWITCH IWD] ready archives=%d files=%zu language=%s game=%s\n",
        archiveCount,
        g_iwdEntries.size(),
        selectedLanguage.c_str(),
        gameName);
    Switch_LogWrite(trace);
}

static bool Switch_OpenIwdFile(
    const char *filename,
    const SwitchIwdEntry &entry,
    int *fileHandle)
{
    if (entry.archiveIndex >= g_iwdArchives.size())
        return false;

    SwitchIwdArchive &archive = g_iwdArchives[entry.archiveIndex];
    if (!archive.file || entry.infoPosition == 0)
        return false;

    unzFile clone = unzReOpen(archive.path.c_str(), archive.file);
    if (!clone)
        return false;

    // Match the original CoD4 fast path:
    // hash/index lookup -> saved ZIP central-directory position -> open.
    // Do not scan the archive or re-read the filename at runtime.
    if (unzSetCurrentFileInfoPosition(clone, entry.infoPosition) != UNZ_OK ||
        unzOpenCurrentFile(clone) != UNZ_OK)
    {
        unzClose(clone);
        return false;
    }

    const int h = AllocHandle();
    if (!h)
    {
        unzCloseCurrentFile(clone);
        unzClose(clone);
        return false;
    }

    g_zipHandles[h].file = clone;
    g_zipHandles[h].size = entry.size;
    g_fsh[h].fileSize = static_cast<int>(entry.size);
    g_fsh[h].streamed = 0;
    g_fsh[h].zipFile = nullptr;
    I_strncpyz(g_fsh[h].name, filename, sizeof(g_fsh[h].name));

    if (fileHandle)
        *fileHandle = h;
    return true;
}


static void SwitchPath(char *dst, size_t dstSize, const char *base, const char *game, const char *qpath)
{
    if (!base || !*base) base = kSwitchRoot;
    if (!game || !*game) game = fs_gamedir;
    while (*qpath == '/' || *qpath == '\\') ++qpath;
    std::snprintf(dst, dstSize, "%s/%s/%s", base, game, qpath);
    for (char *p = dst; *p; ++p) if (*p == '\\') *p = '/';
}

#ifdef __SWITCH__
bool __cdecl FS_SwitchRootFileExists(const char *path)
{
    if (!path || !*path)
        return false;

    char resolved[256];
    std::snprintf(resolved, sizeof(resolved), "%s/%s", kSwitchRoot, path);

    struct stat st{};
    return stat(resolved, &st) == 0 && S_ISREG(st.st_mode);
}

int __cdecl FS_SwitchOpenRootFd(const char *path)
{
    if (!path || !*path)
        return -1;

    char resolved[256];
    std::snprintf(resolved, sizeof(resolved), "%s/%s", kSwitchRoot, path);
    return ::open(resolved, O_RDONLY);
}

bool __cdecl FS_SwitchLanguageHasAssets(int iLanguage)
{
    if (iLanguage < 0 || iLanguage >= 15)
        return false;

    const char *languageName = SEH_GetLanguageName(iLanguage);
    if (!languageName || !*languageName)
        return false;

    char path[256];
    std::snprintf(path, sizeof(path), "%s/zone/%s", kSwitchRoot, languageName);

    struct stat st{};
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}
#endif

static int AllocHandle()
{
    for (int i = 1; i < 65; ++i)
        if (!g_fsh[i].handleFiles.file.o && !g_zipHandles[i].file)
            return i;
    return 0;
}

FILE *FS_SwitchOpenFile(const char *path)
{
    char resolved[256];
    SwitchPath(resolved, sizeof(resolved), fs_basepath ? fs_basepath->current.string : kSwitchRoot, fs_gamedir, path);
    return FS_FileOpenReadBinary(resolved);
}

FILE *FS_SwitchOpenRootFile(const char *path)
{
    if (!path || !*path)
        return nullptr;

    char resolved[256];
    std::snprintf(resolved, sizeof(resolved), "%s/%s", kSwitchRoot, path);

    // Root fastfiles are opened from the fixed Switch game root. Do not
    // dereference fs_basepath here: DB_LoadXFile runs on the database thread,
    // and a Dvar/string object is not part of the filesystem ABI required for
    // this native Switch path.
    return std::fopen(resolved, "rb");
}

bool __cdecl FS_Initialized() { return fs_searchpaths != nullptr; }

void __cdecl FS_CheckFileSystemStarted()
{
    if (!FS_Initialized())
        Com_Error(ERR_FATAL, "Switch filesystem is not initialized");
}

void __cdecl FS_RegisterDvars()
{
    fs_debug = Dvar_RegisterInt("fs_debug", 0, 0, 2, DVAR_NOFLAG, "Filesystem debug");
    fs_copyfiles = Dvar_RegisterBool("fs_copyfiles", 0, DVAR_INIT, "Copy files");
    fs_cdpath = Dvar_RegisterString("fs_cdpath", (char*)kSwitchRoot, DVAR_INIT, "Switch game root");
    fs_basepath = Dvar_RegisterString("fs_basepath", (char*)kSwitchRoot, DVAR_INIT | DVAR_AUTOEXEC, "Switch game root");
    fs_homepath = Dvar_RegisterString("fs_homepath", (char*)kSwitchRoot, DVAR_INIT | DVAR_AUTOEXEC, "Switch game root");
    fs_basegame = Dvar_RegisterString("fs_basegame", (char*)"", DVAR_INIT, "Base game");
    fs_gameDirVar = Dvar_RegisterString("fs_game", (char*)"", DVAR_SERVERINFO | DVAR_SYSTEMINFO | DVAR_INIT, "Game directory");
    fs_ignoreLocalized = Dvar_RegisterBool("fs_ignoreLocalized", 0, DVAR_LATCH | DVAR_CHEAT, "Ignore localized files");
    fs_restrict = Dvar_RegisterBool("fs_restrict", 0, DVAR_INIT, "Restricted mode");
}

void __cdecl FS_AddSearchPath(searchpath_s *search)
{
    search->next = fs_searchpaths;
    fs_searchpaths = search;
}

void __cdecl FS_AddGameDirectory(char *path, char *dir, int localized, int language)
{
    (void)language;
    searchpath_s *s = (searchpath_s*)Z_Malloc(sizeof(searchpath_s), "FS_AddGameDirectory", 3);
    s->dir = (directory_t*)Z_Malloc(sizeof(directory_t), "FS_AddGameDirectory", 3);
    s->iwd = nullptr;
    s->bLocalized = localized;
    s->ignore = 0;
    s->ignorePureCheck = 0;
    s->language = language;
    I_strncpyz(s->dir->path, path, sizeof(s->dir->path));
    I_strncpyz(s->dir->gamedir, dir, sizeof(s->dir->gamedir));
    FS_AddSearchPath(s);
    if (!localized)
        I_strncpyz(fs_gamedir, dir, sizeof(fs_gamedir));
}

void __cdecl FS_AddLocalizedGameDirectory(char *path, char *dir)
{
    FS_AddGameDirectory(path, dir, 0, 0);
}

void __cdecl FS_Startup(char *gameName)
{
    Com_Printf(CON_CHANNEL_FILES, "----- Switch FS_Startup -----\n");
    FS_RegisterDvars();
    Switch_IndexIwdArchives(gameName);
    if (com_logfile)
        Dvar_SetInt((dvar_s*)com_logfile, 0);
    FS_AddLocalizedGameDirectory((char*)kSwitchRoot, gameName);
    if (fs_basegame && fs_basegame->current.string[0])
    {
        FS_AddLocalizedGameDirectory((char*)kSwitchRoot, (char*)fs_basegame->current.string);
    }
    Com_Printf(CON_CHANNEL_FILES, "Switch game root: %s\n", kSwitchRoot);
    Com_Printf(CON_CHANNEL_FILES, "Game directory: %s\n", fs_gamedir);
    Com_Printf(CON_CHANNEL_FILES, "-----------------------------\n");
}

void __cdecl FS_InitFilesystem()
{
    SEH_InitLanguage();
    FS_Startup((char*)"main");
    SEH_Init_StringEd();
    SEH_UpdateLanguageInfo();
}

int __cdecl FS_HashFileName(const char *fname, int hashSize)
{
    if (hashSize <= 0) hashSize = 1024;
    unsigned hash = 0;
    for (int i = 0; fname[i]; ++i) {
        int c = std::tolower((unsigned char)fname[i]);
        if (c == '.') break;
        if (c == '\\') c = '/';
        hash += c * (i + 119);
    }
    return ((hash >> 20) ^ hash ^ (hash >> 10)) & (hashSize - 1);
}

int __cdecl FS_FilenameCompare(const char *a, const char *b)
{
    while (*a || *b) {
        char ca = *a++, cb = *b++;
        if (ca == '\\' || ca == ':') ca = '/';
        if (cb == '\\' || cb == ':') cb = '/';
        ca = (char)std::toupper((unsigned char)ca);
        cb = (char)std::toupper((unsigned char)cb);
        if (ca != cb) return -1;
    }
    return 0;
}

void __cdecl FS_ReplaceSeparators(char *path)
{
    for (; *path; ++path) if (*path == '\\' || *path == ':') *path = '/';
}

void __cdecl FS_BuildOSPathForThread(const char *base, const char *game, const char *qpath, char *out, FsThread)
{
    SwitchPath(out, 256, base, game, qpath);
}

void __cdecl FS_BuildOSPath(const char *base, const char *game, const char *qpath, char *out)
{
    FS_BuildOSPathForThread(base, game, qpath, out, FS_THREAD_MAIN);
}

int __cdecl FS_CreatePath(char *path)
{
    char tmp[256];
    I_strncpyz(tmp, path, sizeof(tmp));
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
    return 0;
}

uint32_t __cdecl FS_FOpenFileReadForThread(const char *filename, int *file, FsThread)
{
    FS_CheckFileSystemStarted();
    if (file)
        *file = 0;
    if (!filename || !*filename)
        return (uint32_t)-1;

    const std::string normalizedName = SwitchNormalizePath(filename);
    const bool traceImage3 =
        normalizedName == "images/3.iwi";

    if (traceImage3)
    {
        const auto iwdIt = g_iwdEntries.find(normalizedName);
        char trace[512];

        if (iwdIt != g_iwdEntries.end() &&
            iwdIt->second < g_iwdEntryRecords.size())
        {
            const SwitchIwdEntry &entry = g_iwdEntryRecords[iwdIt->second];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][IWI ROOT] raw=%s normalized=%s found=1 archive=%s size=%u\n",
                filename,
                normalizedName.c_str(),
                entry.archiveIndex < g_iwdArchives.size()
                    ? g_iwdArchives[entry.archiveIndex].path.c_str()
                    : "invalid",
                static_cast<unsigned>(entry.size));
        }
        else
        {
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][IWI ROOT] raw=%s normalized=%s found=0 archive=none\n",
                filename,
                normalizedName.c_str());
        }

        Switch_LogWrite(trace);
    }

    // Resolve only through the startup-built IWD index. No per-file archive
    // rescans are performed here. Duplicate qpaths are tried in the same
    // search-path order represented by the index, so one bad duplicate cannot
    // hide a valid copy in another IWD.
    {
        const auto iwdIt = g_iwdEntries.find(normalizedName);
        int iwdHandle = 0;
        bool openedIwd = false;

        if (iwdIt != g_iwdEntries.end())
        {
            uint32_t entryIndex = iwdIt->second;
            while (entryIndex != UINT32_MAX &&
                   entryIndex < g_iwdEntryRecords.size())
            {
                const SwitchIwdEntry &entry = g_iwdEntryRecords[entryIndex];
                if (Switch_OpenIwdFile(filename, entry, &iwdHandle))
                {
                    openedIwd = true;
                    break;
                }
                entryIndex = entry.nextDuplicate;
            }
        }

        if (openedIwd)
        {
            if (traceImage3)
            {
                char trace[384];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[KisakCOD][IWI ROOT] raw=%s normalized=%s opened=iwd handle=%d size=%u\n",
                    filename,
                    normalizedName.c_str(),
                    iwdHandle,
                    static_cast<unsigned>(g_fsh[iwdHandle].fileSize));
                Switch_LogWrite(trace);
            }
            if (file)
                *file = iwdHandle;
            return g_fsh[iwdHandle].fileSize;
        }

        if (traceImage3)
        {
            char trace[384];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][IWI ROOT] raw=%s normalized=%s opened=iwd=0\n",
                filename,
                normalizedName.c_str());
            Switch_LogWrite(trace);
        }
    }

    char path[256];
    SwitchPath(
        path,
        sizeof(path),
        fs_basepath ? fs_basepath->current.string : kSwitchRoot,
        fs_gamedir,
        filename);

    FILE *fp = FS_FileOpenReadBinary(path);
    if (!fp)
    {
        if (traceImage3)
        {
            char trace[384];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][IWI ROOT] path=%s iwd=0 loose=0 osPath=%s\n",
                normalizedName.c_str(),
                path);
            Switch_LogWrite(trace);
        }
        return (uint32_t)-1;
    }

    if (traceImage3)
    {
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][IWI ROOT] path=%s iwd=0 loose=1 osPath=%s\n",
            normalizedName.c_str(),
            path);
        Switch_LogWrite(trace);
    }

    const int h = AllocHandle();
    if (!h)
    {
        fclose(fp);
        return (uint32_t)-1;
    }

    g_fsh[h].handleFiles.file.o = fp;
    g_fsh[h].fileSize = FS_FileGetFileSize(fp);
    g_fsh[h].streamed = 0;
    g_fsh[h].zipFile = nullptr;
    I_strncpyz(g_fsh[h].name, filename, sizeof(g_fsh[h].name));

    if (file)
        *file = h;
    return g_fsh[h].fileSize;
}

uint32_t __cdecl FS_FOpenFileRead(const char *filename, int *file)
{
    return FS_FOpenFileReadForThread(filename, file, FS_THREAD_MAIN);
}

uint32_t __cdecl FS_FOpenFileReadStream(const char *filename, int *file)
{
    return FS_FOpenFileReadForThread(filename, file, FS_THREAD_STREAM);
}

int __cdecl FS_FOpenFileReadDatabase(const char *filename, int *file)
{
    return (int)FS_FOpenFileReadForThread(filename, file, FS_THREAD_DATABASE);
}

int __cdecl FS_filelength(int f)
{
    if (f <= 0 || f >= 65)
        return -1;
    if (g_zipHandles[f].file)
        return static_cast<int>(g_zipHandles[f].size);
    if (!g_fsh[f].handleFiles.file.o)
        return -1;
    return g_fsh[f].fileSize;
}

uint32_t __cdecl FS_Read(uint8_t *buffer, uint32_t len, int h)
{
    if (h <= 0 || h >= 65)
        return 0;
    if (g_zipHandles[h].file)
    {
        const int read = unzReadCurrentFile(g_zipHandles[h].file, buffer, len);
        if (read < 0)
        {
            char trace[192];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH IWD] read error handle=%d request=%u result=%d pos=%ld size=%u\n",
                h,
                len,
                read,
                unztell(g_zipHandles[h].file),
                g_zipHandles[h].size);
            Switch_LogWrite(trace);
        }
        return read > 0 ? static_cast<uint32_t>(read) : 0;
    }
    if (!g_fsh[h].handleFiles.file.o)
        return 0;
    return FS_FileRead(buffer, len, g_fsh[h].handleFiles.file.o);
}

int __cdecl FS_Seek(int h, int offset, int origin)
{
    if (h <= 0 || h >= 65)
        return -1;

    if (g_zipHandles[h].file)
    {
        const long currentPosition = unztell(g_zipHandles[h].file);
        if (currentPosition < 0)
            return -1;

        long targetPosition = 0;
        switch (origin)
        {
            case SEEK_SET:
                targetPosition = offset;
                break;
            case SEEK_CUR:
                targetPosition = currentPosition + offset;
                break;
            case SEEK_END:
                targetPosition = static_cast<long>(g_zipHandles[h].size) + offset;
                break;
            default:
                return -1;
        }

        if (targetPosition < 0 || targetPosition > static_cast<long>(g_zipHandles[h].size))
            return -1;

        long current = currentPosition;
        if (targetPosition < current)
        {
            unzCloseCurrentFile(g_zipHandles[h].file);
            if (unzOpenCurrentFile(g_zipHandles[h].file) != UNZ_OK)
                return -1;
            current = 0;
        }

        uint8_t discard[4096];
        while (current < targetPosition)
        {
            const uint32_t remaining = static_cast<uint32_t>(targetPosition - current);
            const unsigned chunk = remaining > sizeof(discard)
                ? static_cast<unsigned>(sizeof(discard))
                : static_cast<unsigned>(remaining);
            const int read = unzReadCurrentFile(g_zipHandles[h].file, discard, chunk);
            if (read != static_cast<int>(chunk))
                return -1;
            current += read;
        }
        return 0;
    }

    if (!g_fsh[h].handleFiles.file.o)
        return -1;

    const int whence = origin == SEEK_SET ? SEEK_SET
        : (origin == SEEK_CUR ? SEEK_CUR : SEEK_END);
    return FS_FileSeek(g_fsh[h].handleFiles.file.o, offset, whence);
}

uint32_t __cdecl FS_FTell(int h)
{
    if (h <= 0 || h >= 65)
        return 0;
    if (g_zipHandles[h].file)
    {
        const long position = unztell(g_zipHandles[h].file);
        return position < 0 ? 0u : static_cast<uint32_t>(position);
    }
    if (!g_fsh[h].handleFiles.file.o)
        return 0;
    return FS_FileTell(g_fsh[h].handleFiles.file.o);
}

void __cdecl FS_FCloseFile(int h)
{
    if (h <= 0 || h >= 65)
        return;

    if (g_zipHandles[h].file)
    {
        unzCloseCurrentFile(g_zipHandles[h].file);
        unzClose(g_zipHandles[h].file);
        std::memset(&g_zipHandles[h], 0, sizeof(g_zipHandles[h]));
    }

    if (g_fsh[h].handleFiles.file.o)
        fclose(g_fsh[h].handleFiles.file.o);

    std::memset(&g_fsh[h], 0, sizeof(g_fsh[h]));
}

uint32_t __cdecl FS_Write(const char *buffer, uint32_t len, int h)
{
    if (h <= 0 || h >= 65 || !g_fsh[h].handleFiles.file.o) return 0;
    return FS_FileWrite(buffer, len, g_fsh[h].handleFiles.file.o);
}

void __cdecl FS_FCloseLogFile(int h) { FS_FCloseFile(h); }

int __cdecl FS_ReadFile(const char *qpath, void **buffer)
{
    if (buffer) *buffer = nullptr;
    int h = 0;
    int len = (int)FS_FOpenFileRead(qpath, &h);
    if (h && len >= 0) {
        if (buffer) {
            ++fs_loadStack;
            uint8_t *buf = (uint8_t*)FS_AllocMem(len + 1);
            *buffer = buf;
            FS_Read(buf, len, h);
            buf[len] = 0;
        }
        FS_FCloseFile(h);
        return len;
    }
    return -1;
}

uint32_t *__cdecl FS_AllocMem(int bytes) { return Hunk_AllocateTempMemory(bytes, "FS_AllocMem"); }
void __cdecl FS_ResetFiles() { fs_loadStack = 0; }
void __cdecl FS_FreeMem(char *buffer) { Hunk_FreeTempMemory(buffer); }
void __cdecl FS_FreeFile(char *buffer) { if (buffer) { --fs_loadStack; FS_FreeMem(buffer); } }

bool __cdecl DB_ModFileExists()
{
    // The Switch bootstrap does not load a separate legacy mod fastfile yet.
    return false;
}

int __cdecl FS_FileExists(char *file)
{
    if (!file || !*file)
        return 0;

    const std::string normalizedName = SwitchNormalizePath(file);
    if (g_iwdEntries.find(normalizedName) != g_iwdEntries.end())
        return 1;

    char path[256];
    SwitchPath(
        path,
        sizeof(path),
        fs_basepath ? fs_basepath->current.string : kSwitchRoot,
        fs_gamedir,
        file);
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return 0;
    fclose(fp);
    return 1;
}

int __cdecl FS_FOpenFileWrite(const char *filename)
{
    return FS_FOpenFileWriteToDirForThread(filename, fs_gamedir, FS_THREAD_MAIN);
}

int __cdecl FS_FOpenFileWriteToDirForThread(const char *filename, const char *dir, FsThread)
{
    char path[256];
    SwitchPath(path, sizeof(path), fs_homepath ? fs_homepath->current.string : kSwitchRoot, dir, filename);
    FS_CreatePath(path);
    FILE *fp = FS_FileOpenWriteBinary(path);
    if (!fp) return 0;
    int h = AllocHandle();
    if (!h) { fclose(fp); return 0; }
    g_fsh[h].handleFiles.file.o = fp;
    I_strncpyz(g_fsh[h].name, filename, sizeof(g_fsh[h].name));
    return h;
}

int __cdecl FS_FOpenFileWriteToDir(const char *filename, const char *dir)
{
    return FS_FOpenFileWriteToDirForThread(filename, dir, FS_THREAD_MAIN);
}

int __cdecl FS_FOpenTextFileWrite(const char *filename)
{
    return FS_FOpenFileWrite(filename);
}

int __cdecl FS_FOpenFileAppend(const char *filename)
{
    char path[256];
    SwitchPath(path, sizeof(path), fs_homepath ? fs_homepath->current.string : kSwitchRoot, fs_gamedir, filename);
    FS_CreatePath(path);
    FILE *fp = FS_FileOpenAppendText(path);
    if (!fp) return 0;
    int h = AllocHandle();
    if (!h) { fclose(fp); return 0; }
    g_fsh[h].handleFiles.file.o = fp;
    I_strncpyz(g_fsh[h].name, filename, sizeof(g_fsh[h].name));
    return h;
}

int __cdecl FS_WriteFile(char *filename, char *buffer, uint32_t size)
{
    int h = FS_FOpenFileWrite(filename);
    if (!h) return 0;
    uint32_t n = FS_Write(buffer, size, h);
    FS_FCloseFile(h);
    return n == size;
}

int __cdecl FS_WriteFileToDir(const char *filename, const char *dir, char *buffer, uint32_t size)
{
    int h = FS_FOpenFileWriteToDir(filename, dir);
    if (!h) return 0;
    uint32_t n = FS_Write(buffer, size, h);
    FS_FCloseFile(h);
    return n == size;
}

bool __cdecl FS_Delete(const char *filename)
{
    char path[256];
    SwitchPath(path, sizeof(path), fs_homepath ? fs_homepath->current.string : kSwitchRoot, fs_gamedir, filename);
    return std::remove(path) == 0;
}

bool __cdecl FS_DeleteInDir(char *filename, char *dir)
{
    char path[256];
    SwitchPath(path, sizeof(path), fs_homepath ? fs_homepath->current.string : kSwitchRoot, dir, filename);
    return std::remove(path) == 0;
}

int __cdecl FS_TouchFile(const char *name)
{
    int h = FS_FOpenFileWrite(name);
    if (!h) return 0;
    FS_FCloseFile(h);
    return 1;
}

void __cdecl FS_Flush(int h)
{
    if (h <= 0 || h >= 65)
        return;
    if (g_zipHandles[h].file)
        return;
    if (g_fsh[h].handleFiles.file.o)
        fflush(g_fsh[h].handleFiles.file.o);
}

uint32_t __cdecl FS_FOpenFileByMode(char *qpath, int *f, fsMode_t mode)
{
    if (!f) return (uint32_t)-1;
    switch (mode) {
        case FS_READ: return FS_FOpenFileRead(qpath, f);
        case FS_WRITE: *f = FS_FOpenFileWrite(qpath); return *f ? 0 : (uint32_t)-1;
        case FS_APPEND:
        case FS_APPEND_SYNC: *f = FS_FOpenFileAppend(qpath); return *f ? 0 : (uint32_t)-1;
        default: *f = 0; return (uint32_t)-1;
    }
}

int __cdecl FS_SV_FOpenFileRead(const char *filename, int *fp) { return (int)FS_FOpenFileRead(filename, fp); }
int __cdecl FS_SV_FOpenFileWrite(const char *filename) { return FS_FOpenFileWrite(filename); }
int __cdecl FS_SV_FileExists(char *file) { return FS_FileExists(file); }

void __cdecl FS_CopyFile(char *from, char *to)
{
    FILE *a = fopen(from, "rb"), *b = fopen(to, "wb");
    if (!a || !b) { if (a) fclose(a); if (b) fclose(b); return; }
    char buf[8192]; size_t n;
    while ((n = fread(buf,1,sizeof(buf),a)) != 0) fwrite(buf,1,n,b);
    fclose(a); fclose(b);
}

void __cdecl FS_Remove(const char *path) { std::remove(path); }
void __cdecl FS_Rename(char *from, char *fromDir, char *to, char *toDir)
{
    char a[256], b[256];
    SwitchPath(a,sizeof(a),fs_homepath ? fs_homepath->current.string : kSwitchRoot,fromDir,from);
    SwitchPath(b,sizeof(b),fs_homepath ? fs_homepath->current.string : kSwitchRoot,toDir,to);
    std::rename(a,b);
}
void __cdecl FS_SV_Rename(char *from, char *to) { std::rename(from,to); }

bool __cdecl FS_IsFileInZip(int h)
{
    return h > 0 && h < 65 && g_zipHandles[h].file != nullptr;
}
int __cdecl FS_ConditionalRestart(int, int) { return 0; }
int __cdecl FS_LoadStack() { return fs_loadStack; }
int __cdecl FS_OpenFileOverwrite(char *qpath) { return FS_FOpenFileWrite(qpath); }
void __cdecl FS_ConvertPath(char *s) { FS_ReplaceSeparators(s); }
char *__cdecl FS_ShiftStr(const char *s, char shift) { static char out[256]; I_strncpyz(out,s,sizeof(out)); for(char *p=out;*p;++p)*p+=shift; return out; }
bool __cdecl FS_NeedRestart(int) { return false; }
void __cdecl FS_Restart(int, int) {}
void __cdecl FS_ClearIwdReferences() {}
void __cdecl FS_DisplayPath(int) {}
void __cdecl FS_Path_f() {}
void __cdecl FS_FullPath_f() {}
void __cdecl FS_Dir_f() {}
void __cdecl FS_TouchFile_f() {}
void __cdecl FS_AddCommands() {}
void __cdecl FS_SetRestrictions() {}
void __cdecl FS_RemoveCommands() {}
void __cdecl FS_ShutdownSearchPaths() { fs_searchpaths = nullptr; }
void __cdecl FS_Shutdown()
{
    for (int i = 1; i < 65; ++i)
        FS_FCloseFile(i);
    Switch_ClearIwdIndex();
    FS_ShutdownSearchPaths();
}
void __cdecl FS_FreeFileList(const char **) {}
int __cdecl FS_GetModList(char *, int) { return 0; }
int __cdecl FS_GetFileList(const char *, const char *, FsListBehavior_e, char *buf, int size) { if(size) *buf=0; return 0; }
const char **__cdecl FS_ListFiles(const char *, const char *, FsListBehavior_e, int *num) { if(num)*num=0; return nullptr; }
const char **__cdecl FS_ListFilesInLocation(const char *, const char *, FsListBehavior_e, int *num, int) { if(num)*num=0; return nullptr; }
char *__cdecl FS_ReferencedIwdPureChecksums() { static char s[4] = ""; return s; }
char *__cdecl FS_ReferencedIwdNames() { static char s[4] = ""; return s; }
char *__cdecl FS_ReferencedIwdChecksums() { static char s[4] = ""; return s; }
char *__cdecl FS_LoadedIwdNames() { static char s[4] = ""; return s; }
char *__cdecl FS_LoadedIwdChecksums() { static char s[4] = ""; return s; }
char *__cdecl FS_LoadedIwdPureChecksums() { static char s[4] = ""; return s; }
void __cdecl FS_Printf(int h, const char *fmt, ...) { if(h<=0||h>=65||!g_fsh[h].handleFiles.file.o)return; va_list ap; va_start(ap,fmt); vfprintf(g_fsh[h].handleFiles.file.o,fmt,ap); va_end(ap); }
uint32_t __cdecl FS_WriteLog(const char *b, uint32_t n, int h){return FS_Write(b, n, h);}
int __cdecl FS_FOpenFileWriteToDirForThread(const char*,const char*,FsThread);


void __cdecl Com_GetBspFilename(char *filename, uint32_t size, const char *mapname)
{
    if (!filename || !size)
        return;

    Com_sprintf(filename, size, "maps/%s.d3dbsp", mapname ? mapname : "");
}

#endif
