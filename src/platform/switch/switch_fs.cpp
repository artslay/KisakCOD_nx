#ifdef __SWITCH__
#include <switch.h>
#include <cstdio>
#include <fcntl.h>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <algorithm>
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

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
    uint32_t infoPosition = 0;
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
static std::string g_switchSelectedLanguage = "english";
static std::string g_switchPrimaryGame = "main";
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
    {
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][IWI ROOT] archive open failed path=%s\n",
            archivePath ? archivePath : "<null>");
        Switch_LogWrite(trace);
        return;
    }

    SwitchIwdArchive archive;
    archive.path = archivePath;
    archive.file = archiveFile;
    const uint16_t archiveIndex = static_cast<uint16_t>(g_iwdArchives.size());
    g_iwdArchives.push_back(std::move(archive));

    unz_global_info globalInfo = {};
    const bool gotGlobalInfo =
        unzGetGlobalInfo(archiveFile, &globalInfo) == UNZ_OK;
    if (gotGlobalInfo)
    {
        g_iwdEntryRecords.reserve(
            g_iwdEntryRecords.size() + globalInfo.number_entry);
        g_iwdEntries.reserve(
            g_iwdEntries.size() + globalInfo.number_entry);
    }

    int entryCount = 0;
    int iwiEntryCount = 0;
    int directoryEntryCount = 0;
    int nameReadFailures = 0;
    int positionReadFailures = 0;
    char firstIwi[256] = {};
    if (unzGoToFirstFile(archiveFile) == UNZ_OK)
    {
        do
        {
            char name[256] = {};
            unz_file_info info = {};
            uint32_t infoPosition = 0;

            if (unzGetCurrentFileInfo(
                    archiveFile,
                    &info,
                    name,
                    sizeof(name),
                    nullptr,
                    0,
                    nullptr,
                    0) != UNZ_OK)
            {
                ++nameReadFailures;
                continue;
            }

            const std::string normalizedName = SwitchNormalizePath(name);
            if (normalizedName.empty())
                continue;
            if (normalizedName.back() == '/')
            {
                ++directoryEntryCount;
                continue;
            }

            if (normalizedName.size() >= 4 &&
                normalizedName.compare(
                    normalizedName.size() - 4, 4, ".iwi") == 0)
            {
                ++iwiEntryCount;
                if (!firstIwi[0])
                    I_strncpyz(
                        firstIwi,
                        normalizedName.c_str(),
                        sizeof(firstIwi));
            }

            if (unzGetCurrentFileInfoPosition(
                    archiveFile,
                    &infoPosition) != UNZ_OK)
            {
                ++positionReadFailures;
                continue;
            }

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

    const bool enumerationFailed =
        gotGlobalInfo &&
        globalInfo.number_entry > static_cast<unsigned long>(directoryEntryCount) &&
        entryCount == 0;
    char trace[640];
    std::snprintf(
        trace,
        sizeof(trace),
        "[KisakCOD][IWI ROOT] archive=%s zipEntries=%lu indexed=%d dirs=%d nameFailures=%d positionFailures=%d globalInfo=%s enumeration=%s iwiEntries=%d firstIwi=%s\n",
        archivePath,
        static_cast<unsigned long>(
            gotGlobalInfo ? globalInfo.number_entry : 0),
        entryCount,
        directoryEntryCount,
        nameReadFailures,
        positionReadFailures,
        gotGlobalInfo ? "ok" : "failed",
        enumerationFailed ? "failed" : "ok",
        iwiEntryCount,
        firstIwi[0] ? firstIwi : "<none>");
    if (!gotGlobalInfo ||
        nameReadFailures ||
        positionReadFailures ||
        enumerationFailed)
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
        "%s/localization.txt",
        base);

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

static std::vector<std::string> Switch_GetGameSearchDirectories(
    const char *base,
    const char *gameName,
    const std::string &selectedLanguage)
{
    std::vector<std::string> directories;

    auto normalizeRoot = [](const char *root)
    {
        std::string normalized = root ? root : "";
        while (normalized.size() > 1 && normalized.back() == '/')
            normalized.pop_back();
        return normalized;
    };

    std::vector<std::string> roots;
    auto addRoot = [&](const char *root)
    {
        if (!root || !*root)
            return;

        const std::string normalizedRoot = normalizeRoot(root);
        if (normalizedRoot.empty())
            return;

        for (const std::string &existing : roots)
        {
            if (!I_stricmp(existing.c_str(), normalizedRoot.c_str()))
                return;
        }
        roots.push_back(normalizedRoot);
    };

    // Keep the install/base path first. fs_homepath and fs_cdpath are valid
    // independent roots in the retail filesystem and must be searched too.
    addRoot(base);
    if (fs_homepath && fs_homepath->current.string[0])
        addRoot(fs_homepath->current.string);
    if (fs_cdpath && fs_cdpath->current.string[0])
        addRoot(fs_cdpath->current.string);

    auto addDirectoryAtRoot = [&](const std::string &root,
                                  const std::string &relativePath)
    {
        if (relativePath.empty())
            return;

        const std::string fullPath = root + "/" + relativePath;
        if (std::find(directories.begin(), directories.end(), fullPath) ==
            directories.end())
            directories.push_back(fullPath);

        if (!selectedLanguage.empty())
        {
            const std::string localizedPath =
                fullPath + "/" + selectedLanguage;
            if (std::find(
                    directories.begin(),
                    directories.end(),
                    localizedPath) == directories.end())
                directories.push_back(localizedPath);
        }
    };

    const char *modName =
        fs_gameDirVar && fs_gameDirVar->current.string[0]
            ? fs_gameDirVar->current.string
            : nullptr;
    const char *baseGameName =
        fs_basegame && fs_basegame->current.string[0]
            ? fs_basegame->current.string
            : nullptr;
    const std::string primaryGame =
        gameName && *gameName ? gameName : "main";

    for (const std::string &root : roots)
    {
        // The relative search-path families are shared by every configured
        // root. Keeping root order stable gives the base path priority while
        // allowing assets and IWDs installed only in home/cd paths to resolve.
        if (modName && I_stricmp(modName, primaryGame.c_str()))
            addDirectoryAtRoot(root, modName);
        if (baseGameName &&
            I_stricmp(baseGameName, primaryGame.c_str()) &&
            (!modName || I_stricmp(baseGameName, modName)))
            addDirectoryAtRoot(root, baseGameName);

        addDirectoryAtRoot(root, primaryGame);
        addDirectoryAtRoot(root, primaryGame + "_shared");
        addDirectoryAtRoot(root, "players");
        addDirectoryAtRoot(root, "raw");
        addDirectoryAtRoot(root, "raw_shared");
        addDirectoryAtRoot(root, "devraw");
        addDirectoryAtRoot(root, "devraw_shared");
    }

    return directories;
}

static void Switch_IndexIwdDirectory(
    const std::string &directoryPath,
    const std::string &selectedLanguage)
{
    DIR *directory = opendir(directoryPath.c_str());
    if (!directory)
        return;

    std::vector<std::string> regularArchives;
    std::vector<std::string> localizedArchives;

    while (dirent *entry = readdir(directory))
    {
        if (!entry->d_name || entry->d_name[0] == '.')
            continue;

        const std::string normalizedName =
            SwitchNormalizePath(entry->d_name);
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
            if (I_stricmp(
                    localizedLanguage,
                    selectedLanguage.c_str()) != 0)
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

    std::sort(
        regularArchives.begin(),
        regularArchives.end(),
        archiveSort);
    std::sort(
        localizedArchives.begin(),
        localizedArchives.end(),
        archiveSort);

    auto indexArchives = [&](const std::vector<std::string> &archives)
    {
        for (const std::string &archiveName : archives)
        {
            const std::string archivePath =
                directoryPath + "/" + archiveName;

            struct stat st = {};
            if (stat(archivePath.c_str(), &st) != 0 ||
                !S_ISREG(st.st_mode))
                continue;

            Switch_IndexIwdArchive(archivePath.c_str());
        }
    };

    // Retain the existing per-directory behavior: ordinary IWDs are indexed
    // first and selected-language IWDs afterwards.
    indexArchives(regularArchives);
    indexArchives(localizedArchives);
}

static void Switch_IndexIwdArchives(const char *game)
{
    Switch_ClearIwdIndex();

    const char *base = fs_basepath && fs_basepath->current.string[0]
        ? fs_basepath->current.string
        : kSwitchRoot;
    const char *gameName = game && *game ? game : "main";
    g_switchPrimaryGame = gameName;
    g_switchSelectedLanguage = Switch_GetStartupLanguage(gameName);

    const std::vector<std::string> searchDirectories =
        Switch_GetGameSearchDirectories(
            base,
            gameName,
            g_switchSelectedLanguage);

    // Index from the lowest-priority directory to the highest-priority one.
    // Switch_IndexIwdArchive() chains duplicate qpaths, so the last indexed
    // copy becomes the first candidate exactly as in the engine search path.
    for (auto directory = searchDirectories.rbegin();
         directory != searchDirectories.rend();
         ++directory)
    {
        Switch_IndexIwdDirectory(
            *directory,
            g_switchSelectedLanguage);
    }

    char trace[768];
    std::snprintf(
        trace,
        sizeof(trace),
        "[KisakCOD][IWI ROOT] index ready install=%s base=%s home=%s cd=%s archives=%zu records=%zu uniquePaths=%zu language=%s game=%s searchDirs=%zu\n",
        kSwitchRoot,
        base,
        fs_homepath && fs_homepath->current.string[0]
            ? fs_homepath->current.string : "<unset>",
        fs_cdpath && fs_cdpath->current.string[0]
            ? fs_cdpath->current.string : "<unset>",
        g_iwdArchives.size(),
        g_iwdEntryRecords.size(),
        g_iwdEntries.size(),
        g_switchSelectedLanguage.c_str(),
        gameName,
        searchDirectories.size());
    Switch_LogWrite(trace);
}

static bool Switch_ShouldTraceIwiLookup(const std::string &normalizedName)
{
    static constexpr const char *const targets[] =
    {
        "images/3.iwi",
        "images/codemesharg.iwi",
        "images/hud_burningcaricon.iwi",
        "images/hud_checkbox_active.iwi",
        "images/hud_objective_tank.iwi",
        "images/sm2/effect.iwi",
        "images/decode_characters_glow.iwi"
    };

    for (const char *target : targets)
    {
        if (normalizedName == target)
            return true;
    }

    return false;
}

static bool Switch_OpenIwdFile(
    const char *filename,
    const SwitchIwdEntry &entry,
    int *fileHandle)
{
    const bool trace = Switch_ShouldTraceIwiLookup(SwitchNormalizePath(filename));
    if (entry.archiveIndex >= g_iwdArchives.size())
    {
        if (trace)
            Switch_LogWrite("[KisakCOD][IWI ROOT] indexed entry has invalid archive index\n");
        return false;
    }

    SwitchIwdArchive &archive = g_iwdArchives[entry.archiveIndex];
    if (!archive.file || entry.infoPosition == 0)
    {
        if (trace)
        {
            char detail[320];
            std::snprintf(
                detail,
                sizeof(detail),
                "[KisakCOD][IWI ROOT] entry rejected name=%s archive=%s pos=%u archiveOpen=%u\n",
                filename ? filename : "<null>",
                archive.path.c_str(),
                static_cast<unsigned>(entry.infoPosition),
                archive.file ? 1u : 0u);
            Switch_LogWrite(detail);
        }
        return false;
    }

    unzFile clone = unzReOpen(archive.path.c_str(), archive.file);
    if (!clone)
    {
        if (trace)
        {
            char detail[320];
            std::snprintf(
                detail,
                sizeof(detail),
                "[KisakCOD][IWI ROOT] reopen failed name=%s archive=%s pos=%u\n",
                filename ? filename : "<null>",
                archive.path.c_str(),
                static_cast<unsigned>(entry.infoPosition));
            Switch_LogWrite(detail);
        }
        return false;
    }

    // Match the original CoD4 fast path:
    // hash/index lookup -> saved ZIP central-directory position -> open.
    // Do not scan the archive or re-read the filename at runtime.
    const int positionResult =
        unzSetCurrentFileInfoPosition(clone, entry.infoPosition);
    const int openResult = positionResult == UNZ_OK
        ? unzOpenCurrentFile(clone)
        : positionResult;
    if (positionResult != UNZ_OK || openResult != UNZ_OK)
    {
        if (trace)
        {
            char detail[384];
            std::snprintf(
                detail,
                sizeof(detail),
                "[KisakCOD][IWI ROOT] open failed name=%s archive=%s pos=%u setPosition=%d open=%d expectedSize=%u\n",
                filename ? filename : "<null>",
                archive.path.c_str(),
                static_cast<unsigned>(entry.infoPosition),
                positionResult,
                openResult,
                static_cast<unsigned>(entry.size));
            Switch_LogWrite(detail);
        }
        unzClose(clone);
        return false;
    }

    if (trace)
    {
        char detail[320];
        std::snprintf(
            detail,
            sizeof(detail),
            "[KisakCOD][IWI ROOT] opened name=%s archive=%s pos=%u size=%u\n",
            filename ? filename : "<null>",
            archive.path.c_str(),
            static_cast<unsigned>(entry.infoPosition),
            static_cast<unsigned>(entry.size));
        Switch_LogWrite(detail);
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


static std::string SwitchNormalizeProfilePath(const char *path)
{
    std::string normalized;
    if (!path)
        return normalized;

    normalized.reserve(std::strlen(path));
    bool previousSeparator = false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(path); *p; ++p)
    {
        char c = static_cast<char>(*p);
        if (c == '\\')
            c = '/';

        if (c == '/')
        {
            if (previousSeparator)
                continue;
            previousSeparator = true;
        }
        else
        {
            previousSeparator = false;
        }

        normalized.push_back(c);
    }

    while (!normalized.empty() && normalized.front() == '/')
        normalized.erase(normalized.begin());
    while (!normalized.empty() && normalized.back() == '/')
        normalized.pop_back();

    return normalized;
}

static bool Switch_IsSafeProfilePath(const std::string &path)
{
    if (path.empty())
        return false;

    const size_t prefixLength = 9;
    if (path.size() < prefixLength ||
        I_strnicmp(path.c_str(), "profiles/", prefixLength) != 0)
    {
        if (I_stricmp(path.c_str(), "profiles") != 0)
            return false;
    }

    size_t start = 0;
    while (start < path.size())
    {
        size_t end = path.find('/', start);
        if (end == std::string::npos)
            end = path.size();

        if (end == start)
            return false;

        const std::string component = path.substr(start, end - start);
        if (component == "." || component == ".." ||
            component.find(':') != std::string::npos)
        {
            return false;
        }

        start = end + 1;
    }

    return true;
}

static bool Switch_IsProfilePath(const char *qpath)
{
    const std::string normalized = SwitchNormalizeProfilePath(qpath);
    if (normalized.empty())
        return false;

    return I_stricmp(normalized.c_str(), "profiles") == 0 ||
           (normalized.size() > 9 &&
            I_strnicmp(normalized.c_str(), "profiles/", 9) == 0);
}

static bool SwitchProfilePath(char *dst, size_t dstSize, const char *qpath)
{
    if (!dst || !dstSize)
        return false;

    const std::string normalized = SwitchNormalizeProfilePath(qpath);
    if (!Switch_IsSafeProfilePath(normalized))
    {
        dst[0] = '\0';
        return false;
    }

    const int written = std::snprintf(
        dst,
        dstSize,
        "%s/players/%s",
        kSwitchRoot,
        normalized.c_str());

    return written >= 0 && static_cast<size_t>(written) < dstSize;
}

static bool SwitchBuildPath(
    char *dst,
    size_t dstSize,
    const char *base,
    const char *game,
    const char *qpath);

static void SwitchPath(char *dst, size_t dstSize, const char *base, const char *game, const char *qpath)
{
    if (!base || !*base) base = kSwitchRoot;
    if (!game || !*game) game = fs_gamedir;
    while (*qpath == '/' || *qpath == '\\') ++qpath;
    std::snprintf(dst, dstSize, "%s/%s/%s", base, game, qpath);
    for (char *p = dst; *p; ++p) if (*p == '\\') *p = '/';
}

static bool SwitchBuildPath(
    char *dst,
    size_t dstSize,
    const char *base,
    const char *game,
    const char *qpath)
{
    if (Switch_IsProfilePath(qpath))
        return SwitchProfilePath(dst, dstSize, qpath);

    SwitchPath(dst, dstSize, base, game, qpath);
    return std::strlen(dst) < dstSize;
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
    // Match the retail startup-variable pass before FS_Startup builds the
    // filesystem search paths and indexes IWD archives.
    Com_StartupVariable("fs_cdpath");
    Com_StartupVariable("fs_basepath");
    Com_StartupVariable("fs_homepath");
    Com_StartupVariable("fs_game");
    Com_StartupVariable("fs_copyfiles");
    Com_StartupVariable("fs_restrict");
    Com_StartupVariable("loc_language");

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
    if (!path || !*path)
        return 1;

    if (std::strstr(path, "..") || std::strstr(path, "::"))
        return 1;

    char tmp[256];
    I_strncpyz(tmp, path, sizeof(tmp));

    for (char *p = tmp; *p; ++p)
        if (*p == '\\')
            *p = '/';

    size_t length = std::strlen(tmp);
    if (!length)
        return 1;

    // A trailing separator denotes a directory path (used by
    // Com_NewPlayerProfile); otherwise only create the parent directory.
    const bool wantFullDirectory = tmp[length - 1] == '/';
    size_t end = wantFullDirectory
        ? length
        : (std::strrchr(tmp, '/') ? static_cast<size_t>(std::strrchr(tmp, '/') - tmp) : 0);

    if (end == 0)
        return 0;

    // Skip the "sdmc:/" prefix when iterating path components.
    size_t start = 0;
    const char *scheme = std::strchr(tmp, ':');
    if (scheme)
    {
        const char *slash = std::strchr(scheme + 1, '/');
        if (slash)
            start = static_cast<size_t>(slash - tmp) + 1;
    }

    if (end <= start)
        return 0;

    for (size_t pos = start; pos < end; ++pos)
    {
        if (tmp[pos] != '/')
            continue;

        tmp[pos] = '\0';
        if (tmp[start] != '\0')
        {
            if (::mkdir(tmp, 0777) != 0 && errno != EEXIST)
                return 1;

            struct stat st {};
            if (::stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode))
                return 1;
        }
        tmp[pos] = '/';
    }

    tmp[end] = '\0';
    if (tmp[start] != '\0')
    {
        if (::mkdir(tmp, 0777) != 0 && errno != EEXIST)
            return 1;

        struct stat st {};
        if (::stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode))
            return 1;
    }

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

    // Player profiles are persistent writable data stored outside the
    // game/main search path at players/profiles. Preserve profile-name case:
    // SwitchNormalizePath() intentionally lowercases packed asset qpaths.
    if (Switch_IsProfilePath(filename))
    {
        char profilePath[256];
        if (!SwitchProfilePath(profilePath, sizeof(profilePath), filename))
        {
            if (file)
                *file = 0;
            return (uint32_t)-1;
        }

        FILE *profileFile = FS_FileOpenReadBinary(profilePath);
        if (!profileFile)
        {
            if (file)
                *file = 0;
            return (uint32_t)-1;
        }

        const int h = AllocHandle();
        if (!h)
        {
            std::fclose(profileFile);
            if (file)
                *file = 0;
            return (uint32_t)-1;
        }

        g_fsh[h].handleFiles.file.o = profileFile;
        std::fseek(profileFile, 0, SEEK_END);
        const long size = std::ftell(profileFile);
        std::fseek(profileFile, 0, SEEK_SET);
        g_fsh[h].fileSize = size >= 0 ? static_cast<int>(size) : 0;
        g_fsh[h].streamed = 0;
        g_fsh[h].zipFile = nullptr;
        I_strncpyz(g_fsh[h].name, normalizedName.c_str(), sizeof(g_fsh[h].name));

        if (file)
            *file = h;
        return g_fsh[h].fileSize;
    }

    const bool traceImage3 = Switch_ShouldTraceIwiLookup(normalizedName);

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
                "[KisakCOD][IWI ROOT] raw=%s normalized=%s found=1 archive=%s infoPosition=%u size=%u\n",
                filename,
                normalizedName.c_str(),
                entry.archiveIndex < g_iwdArchives.size()
                    ? g_iwdArchives[entry.archiveIndex].path.c_str()
                    : "invalid",
                static_cast<unsigned>(entry.infoPosition),
                static_cast<unsigned>(entry.size));
        }
        else
        {
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][IWI ROOT] raw=%s normalized=%s found=0 archive=none entries=%zu archives=%zu\n",
                filename,
                normalizedName.c_str(),
                g_iwdEntryRecords.size(),
                g_iwdArchives.size());

            // A basename match is diagnostic only. Never silently substitute
            // a texture from a different qpath; the exact engine asset path
            // must remain authoritative.
            const size_t slash = normalizedName.find_last_of('/');
            const std::string wantedBasename =
                slash == std::string::npos
                    ? normalizedName
                    : normalizedName.substr(slash + 1);
            size_t candidates = 0;
            for (const auto &indexed : g_iwdEntries)
            {
                const size_t candidateSlash =
                    indexed.first.find_last_of('/');
                const std::string candidateBasename =
                    candidateSlash == std::string::npos
                        ? indexed.first
                        : indexed.first.substr(candidateSlash + 1);
                if (candidateBasename != wantedBasename)
                    continue;

                const uint32_t entryIndex = indexed.second;
                const SwitchIwdEntry *entry =
                    entryIndex < g_iwdEntryRecords.size()
                        ? &g_iwdEntryRecords[entryIndex]
                        : nullptr;
                const char *archiveName =
                    entry && entry->archiveIndex < g_iwdArchives.size()
                        ? g_iwdArchives[entry->archiveIndex].path.c_str()
                        : "<invalid>";
                char candidateTrace[448];
                std::snprintf(
                    candidateTrace,
                    sizeof(candidateTrace),
                    "[KisakCOD][IWI CANDIDATE] requested=%s candidate=%s archive=%s size=%u\n",
                    normalizedName.c_str(),
                    indexed.first.c_str(),
                    archiveName,
                    entry ? static_cast<unsigned>(entry->size) : 0u);
                Switch_LogWrite(candidateTrace);
                if (++candidates >= 8)
                    break;
            }

            if (!candidates)
                Switch_LogWrite("[KisakCOD][IWI CANDIDATE] no basename matches in indexed archives\n");
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

    const bool isIwi =
        normalizedName.size() >= 4 &&
        normalizedName.compare(
            normalizedName.size() - 4,
            4,
            ".iwi") == 0;

    if (isIwi && traceImage3)
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][IWI ROOT] indexed lookup failed raw=%s normalized=%s entries=%zu archives=%zu; trying loose search path\n",
            filename,
            normalizedName.c_str(),
            g_iwdEntryRecords.size(),
            g_iwdArchives.size());
        Switch_LogWrite(trace);
    }

    // Search the same game-directory families registered by the original
    // FS_Startup: active game/mod, basegame, main, main_shared, players, raw,
    // and raw_shared, including the selected-language subdirectories.
    char path[512] = {};
    FILE *fp = nullptr;

    const char *base = fs_basepath && fs_basepath->current.string[0]
        ? fs_basepath->current.string
        : kSwitchRoot;
    const char *gameName = g_switchPrimaryGame.empty()
        ? "main"
        : g_switchPrimaryGame.c_str();
    const std::vector<std::string> searchDirectories =
        Switch_GetGameSearchDirectories(
            base,
            gameName,
            g_switchSelectedLanguage);

    size_t pathsChecked = 0;
    for (const std::string &directory : searchDirectories)
    {
        const int pathLength = std::snprintf(
            path,
            sizeof(path),
            "%s/%s",
            directory.c_str(),
            filename);
        if (pathLength < 0 ||
            static_cast<size_t>(pathLength) >= sizeof(path))
            continue;

        ++pathsChecked;
        fp = FS_FileOpenReadBinary(path);
        if (fp)
            break;
    }

    if (!fp)
    {
        if (traceImage3)
        {
            char trace[384];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][IWI ROOT] path=%s iwd=0 loose=0 pathsChecked=%zu lastOSPath=%s\n",
                normalizedName.c_str(),
                pathsChecked,
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
    if (Switch_IsProfilePath(file))
    {
        char profilePath[256];
        if (!SwitchProfilePath(profilePath, sizeof(profilePath), file))
            return 0;
        struct stat st {};
        return stat(profilePath, &st) == 0 && S_ISREG(st.st_mode);
    }

    if (g_iwdEntries.find(normalizedName) != g_iwdEntries.end())
        return 1;

    const char *base = fs_basepath && fs_basepath->current.string[0]
        ? fs_basepath->current.string
        : kSwitchRoot;
    const char *gameName = g_switchPrimaryGame.empty()
        ? "main"
        : g_switchPrimaryGame.c_str();
    const std::vector<std::string> searchDirectories =
        Switch_GetGameSearchDirectories(
            base,
            gameName,
            g_switchSelectedLanguage);

    char path[512];
    for (const std::string &directory : searchDirectories)
    {
        const int pathLength = std::snprintf(
            path,
            sizeof(path),
            "%s/%s",
            directory.c_str(),
            file);
        if (pathLength < 0 ||
            static_cast<size_t>(pathLength) >= sizeof(path))
            continue;

        FILE *fp = fopen(path, "rb");
        if (!fp)
            continue;
        fclose(fp);
        return 1;
    }

    return 0;
}

static bool Switch_PathComponentEquals(const char *component, size_t length, const char *expected)
{
    if (std::strlen(expected) != length)
        return false;

    for (size_t i = 0; i < length; ++i)
    {
        if (std::tolower(static_cast<unsigned char>(component[i])) !=
            std::tolower(static_cast<unsigned char>(expected[i])))
            return false;
    }
    return true;
}

// Suppress only known script-debugger artifacts before FS_CreatePath.
// Other scriptdata paths can be legitimate script-generated output (for example,
// map-source export files) and must not be blocked solely by their directory.
static bool Switch_ShouldSuppressScriptDebugWrite(const char *path)
{
    if (!path || !*path)
        return false;

    const char *baseName = path;
    for (const char *p = path;; ++p)
    {
        if (*p != '/' && *p != '\\' && *p != '\0')
            continue;

        if (*p == '\0')
        {
            baseName = p;
            while (baseName > path && baseName[-1] != '/' && baseName[-1] != '\\')
                --baseName;
            break;
        }
    }

    static const char *const suppressedNames[] = {
        "markHistory.cache",
        "open_scripts.txt",
        "script_pos.txt",
        "timeHistory.cache",
        "watch_window.txt"
    };

    const size_t baseNameLength = std::strlen(baseName);
    for (size_t i = 0; i < sizeof(suppressedNames) / sizeof(suppressedNames[0]); ++i)
    {
        if (Switch_PathComponentEquals(baseName, baseNameLength, suppressedNames[i]))
            return true;
    }

    return false;
}

int __cdecl FS_FOpenFileWrite(const char *filename)
{
    return FS_FOpenFileWriteToDirForThread(filename, fs_gamedir, FS_THREAD_MAIN);
}

int __cdecl FS_FOpenFileWriteToDirForThread(const char *filename, const char *dir, FsThread)
{
    if (Switch_ShouldSuppressScriptDebugWrite(filename) || Switch_ShouldSuppressScriptDebugWrite(dir))
        return 0;

    char path[256];
    if (!SwitchBuildPath(
            path,
            sizeof(path),
            fs_homepath ? fs_homepath->current.string : kSwitchRoot,
            dir,
            filename) ||
        FS_CreatePath(path))
        return 0;
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
    if (Switch_ShouldSuppressScriptDebugWrite(filename) || Switch_ShouldSuppressScriptDebugWrite(fs_gamedir))
        return 0;

    char path[256];
    if (!SwitchBuildPath(
            path,
            sizeof(path),
            fs_homepath ? fs_homepath->current.string : kSwitchRoot,
            fs_gamedir,
            filename) ||
        FS_CreatePath(path))
        return 0;
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
    if (!SwitchBuildPath(
            path,
            sizeof(path),
            fs_homepath ? fs_homepath->current.string : kSwitchRoot,
            fs_gamedir,
            filename))
        return false;
    return std::remove(path) == 0;
}

bool __cdecl FS_DeleteInDir(char *filename, char *dir)
{
    char path[256];
    if (!SwitchBuildPath(
            path,
            sizeof(path),
            fs_homepath ? fs_homepath->current.string : kSwitchRoot,
            dir,
            filename))
        return false;
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
bool __cdecl FS_RenameChecked(char *from, char *fromDir, char *to, char *toDir)
{
    char a[256], b[256];
    if (!SwitchBuildPath(
            a,
            sizeof(a),
            fs_homepath ? fs_homepath->current.string : kSwitchRoot,
            fromDir,
            from) ||
        !SwitchBuildPath(
            b,
            sizeof(b),
            fs_homepath ? fs_homepath->current.string : kSwitchRoot,
            toDir,
            to))
    {
        return false;
    }

    if (std::rename(a, b) != 0)
    {
        char trace[640];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][PROFILE FS] rename failed: %s -> %s errno=%d\n",
            a,
            b,
            errno);
        Switch_LogWrite(trace);
        return false;
    }

    return true;
}

void __cdecl FS_Rename(char *from, char *fromDir, char *to, char *toDir)
{
    (void)FS_RenameChecked(from, fromDir, to, toDir);
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
static std::vector<const char *> g_switchProfileFileListPointers;
static std::vector<std::string> g_switchProfileFileListNames;

struct SwitchLocationFileList
{
    std::vector<std::string> names;
    std::vector<const char *> pointers;
};

// Location lists can be retained by the caller while it builds several UI
// entries. Keep a separate allocation for every returned list until
// FS_FreeFileList releases it.
static std::unordered_map<const char **, std::unique_ptr<SwitchLocationFileList>>
    g_switchLocationFileLists;

static void Switch_AddUniqueLocationPath(
    std::vector<std::string> &paths,
    const std::string &path)
{
    for (const std::string &existing : paths)
    {
        if (!I_stricmp(existing.c_str(), path.c_str()))
            return;
    }
    paths.push_back(path);
}

static bool Switch_MatchesFileLocation(const char *gameDir, int lookInFlags)
{
    if (!gameDir || !*gameDir)
        return false;

    // Keep the original engine's location-bit/prefix semantics.
    if (lookInFlags == 63)
        return true;
    if ((lookInFlags & 1) && !I_strnicmp(gameDir, "main", 4))
        return true;
    if ((lookInFlags & 2) && !I_strnicmp(gameDir, "dev", 3))
        return true;
    if ((lookInFlags & 4) && !I_strnicmp(gameDir, "temp", 4))
        return true;
    if ((lookInFlags & 8) && !I_strnicmp(gameDir, "raw", 3))
        return true;
    if ((lookInFlags & 0x10) && !I_strnicmp(gameDir, "raw_shared", 10))
        return true;
    return (lookInFlags & 0x20) && !I_strnicmp(gameDir, "devraw", 6);
}

static bool Switch_NormalizeFileListPath(
    const char *path,
    std::string *normalized)
{
    if (!path || !normalized)
        return false;

    normalized->clear();
    normalized->reserve(std::strlen(path));
    bool previousSeparator = false;

    for (const unsigned char *p =
             reinterpret_cast<const unsigned char *>(path);
         *p;
         ++p)
    {
        char c = static_cast<char>(*p);
        if (c == '\\')
            c = '/';

        if (c == '/')
        {
            // File-list paths are relative qpaths, never OS absolute paths.
            if (normalized->empty())
                return false;
            if (previousSeparator)
                continue;
            previousSeparator = true;
        }
        else
        {
            previousSeparator = false;
        }

        normalized->push_back(c);
    }

    while (!normalized->empty() && normalized->back() == '/')
        normalized->pop_back();

    size_t start = 0;
    while (start < normalized->size())
    {
        size_t end = normalized->find('/', start);
        if (end == std::string::npos)
            end = normalized->size();

        const std::string component = normalized->substr(start, end - start);
        if (component.empty() ||
            component == "." ||
            component == ".." ||
            component.find(':') != std::string::npos)
        {
            return false;
        }

        start = end + 1;
    }

    return true;
}

static std::vector<std::string> Switch_GetFileLocationRoots()
{
    std::vector<std::string> roots;
    auto addRoot = [&](const char *root)
    {
        if (!root || !*root)
            return;

        std::string normalized(root);
        while (normalized.size() > 1 && normalized.back() == '/')
            normalized.pop_back();
        Switch_AddUniqueLocationPath(roots, normalized);
    };

    addRoot(fs_basepath && fs_basepath->current.string[0]
        ? fs_basepath->current.string : kSwitchRoot);
    if (fs_homepath && fs_homepath->current.string[0])
        addRoot(fs_homepath->current.string);
    if (fs_cdpath && fs_cdpath->current.string[0])
        addRoot(fs_cdpath->current.string);

    return roots;
}

static bool Switch_IsFileLocationDirectory(
    const std::string &directory,
    const std::vector<std::string> &roots,
    int lookInFlags)
{
    for (const std::string &root : roots)
    {
        if (directory.size() <= root.size() ||
            I_strnicmp(directory.c_str(), root.c_str(), root.size()) ||
            directory[root.size()] != '/')
        {
            continue;
        }

        const std::string relative = directory.substr(root.size() + 1);
        const size_t separator = relative.find('/');
        const std::string gameDir = relative.substr(0, separator);
        if (Switch_MatchesFileLocation(gameDir.c_str(), lookInFlags))
            return true;
    }

    return false;
}

static std::vector<std::string> Switch_GetFileLocationDirectories(
    int lookInFlags)
{
    const char *base = fs_basepath && fs_basepath->current.string[0]
        ? fs_basepath->current.string
        : kSwitchRoot;
    const char *gameName = g_switchPrimaryGame.empty()
        ? "main"
        : g_switchPrimaryGame.c_str();
    const std::vector<std::string> roots = Switch_GetFileLocationRoots();

    std::vector<std::string> candidates =
        Switch_GetGameSearchDirectories(
            base,
            gameName,
            g_switchSelectedLanguage);

    std::vector<std::string> selected;
    for (const std::string &directory : candidates)
    {
        if (Switch_IsFileLocationDirectory(directory, roots, lookInFlags))
            Switch_AddUniqueLocationPath(selected, directory);
    }
    return selected;
}

static bool Switch_ListExtensionMatches(const char *name, const char *extension)
{
    if (!name)
        return false;

    if (!extension || !*extension)
        return true;

    if (!std::strcmp(extension, "/"))
        return true;

    const size_t nameLen = std::strlen(name);
    const size_t extLen = std::strlen(extension);

    // Match the stock FS_ListFilteredFiles semantics: "svg" means a
    // filename whose final extension is ".svg", not just a suffix "svg".
    return nameLen > extLen + 1 &&
           name[nameLen - extLen - 1] == '.' &&
           !I_stricmp(name + nameLen - extLen, extension);
}

void __cdecl FS_FreeFileList(const char **list)
{
    if (!list)
        return;

    const auto locationList = g_switchLocationFileLists.find(list);
    if (locationList != g_switchLocationFileLists.end())
    {
        g_switchLocationFileLists.erase(locationList);
        return;
    }

    if (list == g_switchProfileFileListPointers.data())
    {
        g_switchProfileFileListPointers.clear();
        g_switchProfileFileListNames.clear();
    }
}

int __cdecl FS_GetModList(char *, int) { return 0; }

int __cdecl FS_GetFileList(
    const char *path,
    const char *extension,
    FsListBehavior_e behavior,
    char *buf,
    int size)
{
    (void)behavior;
    if (!buf || size <= 0)
        return 0;

    const char **files = FS_ListFiles(path, extension, behavior, nullptr);
    int used = 0;
    int count = 0;
    if (files)
    {
        for (const char **p = files; *p; ++p)
        {
            const int len = static_cast<int>(std::strlen(*p)) + 1;
            if (used + len > size)
                break;
            std::memcpy(buf + used, *p, len);
            used += len;
            ++count;
        }
    }

    if (used < size)
        buf[used] = '\0';

    return count;
}

const char **__cdecl FS_ListFiles(
    const char *path,
    const char *extension,
    FsListBehavior_e behavior,
    int *num)
{
    (void)behavior;

    if (num)
        *num = 0;

    if (!path || !*path)
        return nullptr;

    const std::string normalizedPath = SwitchNormalizeProfilePath(path);
    if (!Switch_IsProfilePath(normalizedPath.c_str()))
        return nullptr;

    char profileDir[256];
    if (!SwitchProfilePath(profileDir, sizeof(profileDir), normalizedPath.c_str()))
        return nullptr;

    DIR *directory = ::opendir(profileDir);
    if (!directory)
        return nullptr;

    const bool isDirSearch =
        extension && !std::strcmp(extension, "/");

    std::vector<std::string> names;
    while (dirent *entry = ::readdir(directory))
    {
        if (!entry || !entry->d_name[0] ||
            !std::strcmp(entry->d_name, ".") ||
            !std::strcmp(entry->d_name, ".."))
            continue;

        char childPath[512];
        std::snprintf(
            childPath,
            sizeof(childPath),
            "%s/%s",
            profileDir,
            entry->d_name);

        struct stat st {};
        if (::stat(childPath, &st) != 0)
            continue;

        if (isDirSearch)
        {
            // The stock FS uses extension="/" to enumerate directories.
            if (!S_ISDIR(st.st_mode))
                continue;
        }
        else
        {
            if (!S_ISREG(st.st_mode))
                continue;

            if (!Switch_ListExtensionMatches(entry->d_name, extension))
                continue;
        }

        names.emplace_back(entry->d_name);
    }
    ::closedir(directory);

    std::sort(
        names.begin(),
        names.end(),
        [](const std::string &a, const std::string &b)
        {
            return I_stricmp(a.c_str(), b.c_str()) < 0;
        });

    g_switchProfileFileListNames = std::move(names);
    g_switchProfileFileListPointers.clear();
    g_switchProfileFileListPointers.reserve(
        g_switchProfileFileListNames.size() + 1);

    for (const std::string &name : g_switchProfileFileListNames)
        g_switchProfileFileListPointers.push_back(name.c_str());
    g_switchProfileFileListPointers.push_back(nullptr);

    if (num)
        *num = static_cast<int>(g_switchProfileFileListNames.size());

    return g_switchProfileFileListPointers.data();
}
const char **__cdecl FS_ListFilesInLocation(
    const char *path,
    const char *extension,
    FsListBehavior_e behavior,
    int *num,
    int lookInFlags)
{
    if (num)
        *num = 0;

    std::string relativePath;
    if (!Switch_NormalizeFileListPath(path, &relativePath))
        return nullptr;

    const bool isDirSearch =
        extension && !I_stricmp(extension, "/");
    const std::string normalizedPath =
        SwitchNormalizePath(relativePath.c_str());
    const std::string pathPrefix =
        normalizedPath.empty() ? std::string() : normalizedPath + "/";

    const std::vector<std::string> locationDirectories =
        Switch_GetFileLocationDirectories(lookInFlags);

    std::vector<std::string> names;
    std::unordered_map<std::string, bool> seenNames;
    auto addName = [&](const std::string &name)
    {
        if (name.empty())
            return;
        const std::string key = SwitchNormalizePath(name.c_str());
        if (seenNames.emplace(key, true).second)
            names.push_back(name);
    };

    // As in the stock filesystem, loose files are hidden from a pure-only
    // listing while filesystem restrictions or a server-IWD list is active.
    const bool listLooseFiles =
        ((!fs_restrict || !fs_restrict->current.enabled) &&
         !fs_numServerIwds) ||
        behavior == FS_LIST_ALL;

    if (listLooseFiles)
    {
        for (const std::string &locationDirectory : locationDirectories)
        {
            std::string searchDirectory = locationDirectory;
            if (!relativePath.empty())
                searchDirectory += "/" + relativePath;

            DIR *directory = ::opendir(searchDirectory.c_str());
            if (!directory)
                continue;

            while (dirent *entry = ::readdir(directory))
            {
                if (!entry || !entry->d_name[0] ||
                    !std::strcmp(entry->d_name, ".") ||
                    !std::strcmp(entry->d_name, ".."))
                {
                    continue;
                }

                const std::string childPath =
                    searchDirectory + "/" + entry->d_name;
                struct stat st {};
                if (::stat(childPath.c_str(), &st) != 0)
                    continue;

                if (isDirSearch)
                {
                    if (S_ISDIR(st.st_mode))
                        addName(entry->d_name);
                }
                else
                {
                    if (!S_ISREG(st.st_mode) ||
                        !Switch_ListExtensionMatches(entry->d_name, extension))
                    {
                        continue;
                    }
                    addName(entry->d_name);
                }
            }

            ::closedir(directory);
        }
    }

    auto archiveInSelectedLocation = [&](const std::string &archivePath)
    {
        for (const std::string &directory : locationDirectories)
        {
            if (archivePath.size() > directory.size() &&
                !I_strnicmp(
                    archivePath.c_str(),
                    directory.c_str(),
                    directory.size()) &&
                archivePath[directory.size()] == '/')
            {
                return true;
            }
        }
        return false;
    };

    // IWD entries are indexed by qpath and retain duplicate records for every
    // archive. Walk the duplicate chain so a requested location still finds
    // a file even when a higher-priority copy lives in another location.
    for (const auto &indexedFile : g_iwdEntries)
    {
        const std::string &assetName = indexedFile.first;
        uint32_t recordIndex = indexedFile.second;
        bool foundInLocation = false;

        while (recordIndex != UINT32_MAX &&
               recordIndex < g_iwdEntryRecords.size())
        {
            const SwitchIwdEntry &record = g_iwdEntryRecords[recordIndex];
            if (record.archiveIndex < g_iwdArchives.size() &&
                archiveInSelectedLocation(
                    g_iwdArchives[record.archiveIndex].path))
            {
                foundInLocation = true;
                break;
            }
            recordIndex = record.nextDuplicate;
        }

        if (!foundInLocation)
            continue;

        std::string listedName;
        if (!pathPrefix.empty())
        {
            if (assetName.size() <= pathPrefix.size() ||
                assetName.compare(0, pathPrefix.size(), pathPrefix) != 0)
            {
                continue;
            }
            listedName = assetName.substr(pathPrefix.size());
        }
        else
        {
            listedName = assetName;
        }

        if (isDirSearch)
        {
            const size_t separator = listedName.find('/');
            if (separator == std::string::npos || separator == 0)
                continue;
            addName(listedName.substr(0, separator));
        }
        else
        {
            if (listedName.find('/') != std::string::npos ||
                !Switch_ListExtensionMatches(listedName.c_str(), extension))
            {
                continue;
            }
            addName(listedName);
        }
    }

    if (names.empty())
        return nullptr;

    std::sort(
        names.begin(),
        names.end(),
        [](const std::string &a, const std::string &b)
        {
            const int comparison = I_stricmp(a.c_str(), b.c_str());
            return comparison ? comparison < 0 : a < b;
        });

    auto ownedList = std::make_unique<SwitchLocationFileList>();
    ownedList->names = std::move(names);
    ownedList->pointers.reserve(ownedList->names.size() + 1);
    for (std::string &name : ownedList->names)
        ownedList->pointers.push_back(name.data());
    ownedList->pointers.push_back(nullptr);

    const char **result = ownedList->pointers.data();
    if (num)
        *num = static_cast<int>(ownedList->names.size());

    g_switchLocationFileLists.emplace(result, std::move(ownedList));
    return result;
}
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
