#include "platform_compat.h"

#include <errno.h>
#include <string.h>

#include <string>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#ifdef _WIN64
#include <timeapi.h>
#else
#include <mmsystem.h>
#endif
#else
#include <chrono>
#endif

#include <SDL.h>

namespace fallout {

static bool compatIsPathSeparator(char ch)
{
    return ch == '/' || ch == '\\';
}

static bool compatFileModeMayWrite(const char* mode)
{
    return mode != nullptr && (strchr(mode, 'w') != nullptr || strchr(mode, 'a') != nullptr || strchr(mode, '+') != nullptr);
}

#ifndef _WIN32
typedef struct CompatDirectoryCacheEntry {
    std::unordered_map<std::string, std::string> entries;
} CompatDirectoryCacheEntry;

static constexpr size_t kCompatDirectoryEntryCacheMaxSize = 1024;
static std::unordered_map<std::string, CompatDirectoryCacheEntry> compatDirectoryEntryCache;
// CE: Directories found missing (a game's data/art, data/proto... when all of
// it is in its archives): not looked for again until one is made here.
static std::unordered_set<std::string> compatMissingDirectories;

static std::string compatLowercase(std::string value)
{
    for (char& ch : value) {
        ch = static_cast<char>(SDL_tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

void compatDirectoryEntryCacheClear()
{
    compatDirectoryEntryCache.clear();
    compatMissingDirectories.clear();
}

// CE: [path] from the root: the cache's keys (the engine changes the working
// directory for a while to make one, xfile.cc `xbaseMakeDirectory`).
static std::string compatAbsolutePath(const std::string& path)
{
    if (!path.empty() && path[0] == '/') {
        return path;
    }
    char workingDirectory[COMPAT_MAX_PATH];
    if (getcwd(workingDirectory, sizeof(workingDirectory)) == nullptr) {
        return path;
    }
    std::string absolute = workingDirectory;
    if (path.empty() || path == ".") {
        return absolute;
    }
    if (absolute.empty() || absolute.back() != '/') {
        absolute += '/';
    }
    return absolute + (path.compare(0, 2, "./") == 0 ? path.substr(2) : path);
}

static const CompatDirectoryCacheEntry* compatDirectoryEntryCacheGet(const std::string& directoryPath)
{
    auto existing = compatDirectoryEntryCache.find(directoryPath);
    if (existing != compatDirectoryEntryCache.end()) {
        return &(existing->second);
    }
    if (compatMissingDirectories.count(directoryPath) != 0) {
        return nullptr;
    }

    DIR* dir = opendir(directoryPath.c_str());
    if (dir == nullptr) {
        if (errno == ENOENT || errno == ENOTDIR) {
            if (compatMissingDirectories.size() >= kCompatDirectoryEntryCacheMaxSize) {
                compatMissingDirectories.clear();
            }
            compatMissingDirectories.insert(directoryPath);
        }
        return nullptr;
    }

    if (compatDirectoryEntryCache.size() >= kCompatDirectoryEntryCacheMaxSize) {
        compatDirectoryEntryCache.clear();
    }

    auto inserted = compatDirectoryEntryCache.emplace(directoryPath, CompatDirectoryCacheEntry());
    CompatDirectoryCacheEntry& cacheEntry = inserted.first->second;

    struct dirent* entry = readdir(dir);
    while (entry != nullptr) {
        cacheEntry.entries.emplace(compatLowercase(entry->d_name), entry->d_name);
        entry = readdir(dir);
    }

    closedir(dir);

    return &cacheEntry;
}

// CE: A file or directory created or removed through these functions is
// added to or removed from its directory's cached listing, and a failed remove
// changes nothing. Loading a save tries to remove every proto the game lists,
// mostly ones that exist only in the archives: clearing the whole cache each
// time made the next path re-list every directory from the root.
static void compatDirectoryEntryCacheUpdate(const char* nativePath, bool exists)
{
    std::string path = compatAbsolutePath(nativePath);
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }

    size_t separator = path.find_last_of('/');
    std::string parent = separator == std::string::npos ? "." : path.substr(0, separator);
    while (parent.size() > 1 && parent.back() == '/') {
        parent.pop_back();
    }
    if (parent.empty()) {
        parent = "/";
    }
    std::string name = separator == std::string::npos ? path : path.substr(separator + 1);

    if (!exists) {
        compatDirectoryEntryCache.erase(path);
    } else if (!compatMissingDirectories.empty()) {
        // It and the directories above it are there now.
        std::string made = path;
        while (!made.empty()) {
            compatMissingDirectories.erase(made);
            size_t above = made.find_last_of('/');
            if (above == std::string::npos || above == 0) {
                break;
            }
            made.resize(above);
        }
    }

    auto directory = compatDirectoryEntryCache.find(parent);
    if (directory == compatDirectoryEntryCache.end()) {
        return;
    }

    if (exists) {
        directory->second.entries.emplace(compatLowercase(name), name);
    } else {
        directory->second.entries.erase(compatLowercase(name));
    }
}
#else
void compatDirectoryEntryCacheClear()
{
}

static void compatDirectoryEntryCacheUpdate(const char*, bool)
{
}
#endif

static bool compatResolvePath(char* path);

// [path] made native and resolved (`compat_resolve_path`). True: it is known
// not to be there, the storage needn't be asked (a miss costs a request where
// it isn't local: Android's shared storage). Not so under SAVEGAME: the save
// catalog changes it with its own calls (save_storage.cc).
static bool compat_prepare_native_path(char* nativePath, const char* path)
{
    strncpy(nativePath, path, COMPAT_MAX_PATH - 1);
    nativePath[COMPAT_MAX_PATH - 1] = '\0';
    compat_windows_path_to_native(nativePath);
    if (compatResolvePath(nativePath)) {
        return false;
    }
    return SDL_strcasestr(nativePath, "savegame") == nullptr;
}

int compat_stricmp(const char* string1, const char* string2)
{
    return SDL_strcasecmp(string1, string2);
}

int compat_strnicmp(const char* string1, const char* string2, size_t size)
{
    return SDL_strncasecmp(string1, string2, size);
}

char* compat_strupr(char* string)
{
    return SDL_strupr(string);
}

char* compat_strlwr(char* string)
{
    return SDL_strlwr(string);
}

char* compat_itoa(int value, char* buffer, int radix)
{
    return SDL_itoa(value, buffer, radix);
}

void compat_splitpath(const char* path, char* drive, char* dir, char* fname, char* ext)
{
#ifdef _WIN32
    _splitpath(path, drive, dir, fname, ext);
#else
    const char* driveStart = path;
    const char* pathAfterDrive = path;
    if (pathAfterDrive[0] != '\0'
        && pathAfterDrive[1] != '\0'
        && compatIsPathSeparator(pathAfterDrive[0])
        && compatIsPathSeparator(pathAfterDrive[1])) {
        pathAfterDrive += 2;
        while (*pathAfterDrive != '\0' && !compatIsPathSeparator(*pathAfterDrive) && *pathAfterDrive != '.') {
            pathAfterDrive++;
        }
    }

    if (drive != nullptr) {
        size_t driveSize = pathAfterDrive - driveStart;
        if (driveSize > COMPAT_MAX_DRIVE - 1) {
            driveSize = COMPAT_MAX_DRIVE - 1;
        }
        strncpy(drive, driveStart, driveSize);
        drive[driveSize] = '\0';
    }

    const char* fnameStart = pathAfterDrive;
    for (const char* pch = pathAfterDrive; *pch != '\0'; pch++) {
        if (compatIsPathSeparator(*pch)) {
            fnameStart = pch + 1;
        }
    }

    const char* end = pathAfterDrive + strlen(pathAfterDrive);
    const char* extStart = end;
    for (const char* pch = end; pch != fnameStart; pch--) {
        if (pch[-1] == '.') {
            extStart = pch - 1;
            break;
        }
    }

    if (dir != nullptr) {
        size_t dirSize = fnameStart - pathAfterDrive;
        if (dirSize > COMPAT_MAX_DIR - 1) {
            dirSize = COMPAT_MAX_DIR - 1;
        }
        strncpy(dir, pathAfterDrive, dirSize);
        dir[dirSize] = '\0';
    }

    if (fname != nullptr) {
        size_t fileNameSize = extStart - fnameStart;
        if (fileNameSize > COMPAT_MAX_FNAME - 1) {
            fileNameSize = COMPAT_MAX_FNAME - 1;
        }
        strncpy(fname, fnameStart, fileNameSize);
        fname[fileNameSize] = '\0';
    }

    if (ext != nullptr) {
        size_t extSize = end - extStart;
        if (extSize > COMPAT_MAX_EXT - 1) {
            extSize = COMPAT_MAX_EXT - 1;
        }
        strncpy(ext, extStart, extSize);
        ext[extSize] = '\0';
    }
#endif
}

void compat_makepath(char* path, const char* drive, const char* dir, const char* fname, const char* ext)
{
#ifdef _WIN32
    _makepath(path, drive, dir, fname, ext);
#else
    path[0] = '\0';

    if (drive != nullptr) {
        if (*drive != '\0') {
            strcpy(path, drive);
            path = strchr(path, '\0');

            if (compatIsPathSeparator(path[-1])) {
                path--;
            } else {
                *path = '/';
            }
        }
    }

    if (dir != nullptr) {
        if (*dir != '\0') {
            if (!compatIsPathSeparator(*dir) && compatIsPathSeparator(*path)) {
                path++;
            }

            strcpy(path, dir);
            path = strchr(path, '\0');

            if (compatIsPathSeparator(path[-1])) {
                path--;
            } else {
                *path = '/';
            }
        }
    }

    if (fname != nullptr && *fname != '\0') {
        if (!compatIsPathSeparator(*fname) && compatIsPathSeparator(*path)) {
            path++;
        }

        strcpy(path, fname);
        path = strchr(path, '\0');
    } else {
        if (compatIsPathSeparator(*path)) {
            path++;
        }
    }

    if (ext != nullptr) {
        if (*ext != '\0') {
            if (*ext != '.') {
                *path++ = '.';
            }

            strcpy(path, ext);
            path = strchr(path, '\0');
        }
    }

    *path = '\0';
#endif
}

long compat_tell(int fd)
{
    return lseek(fd, 0, SEEK_CUR);
}

long compat_filelength(int fd)
{
    long originalOffset = lseek(fd, 0, SEEK_CUR);
    lseek(fd, 0, SEEK_SET);
    long filesize = lseek(fd, 0, SEEK_END);
    lseek(fd, originalOffset, SEEK_SET);
    return filesize;
}

int compat_mkdir(const char* path)
{
    char nativePath[COMPAT_MAX_PATH];
    compat_prepare_native_path(nativePath, path);

#ifdef _WIN32
    int rc = mkdir(nativePath);
#else
    int rc = mkdir(nativePath, 0755);
#endif
    if (rc == 0) {
        compatDirectoryEntryCacheUpdate(nativePath, true);
    }
    return rc;
}

int compat_mkdir_recursive(const char* path)
{
    char drive[COMPAT_MAX_DRIVE];
    compat_splitpath(path, drive, nullptr, nullptr, nullptr);

    char pathCopy[COMPAT_MAX_PATH];
    strcpy(pathCopy, path);

    // Skip drive root (e.g. "C:\\" or leading "/") to avoid mkdir("") or mkdir("C:").
    char* sep = pathCopy + strlen(drive);
    if (*sep == '\\' || *sep == '/') sep++;
    for (; *sep != '\0'; sep++) {
        if (*sep == '\\' || *sep == '/') {
            char saved = *sep;
            *sep = '\0';
            if (compat_mkdir(pathCopy) < 0) {
                break;
            }
            *sep = saved;
        }
    }
    return compat_mkdir(path);
}

bool compat_is_dir(const char* path)
{
    char nativePath[COMPAT_MAX_PATH];
    if (compat_prepare_native_path(nativePath, path)) {
        return false;
    }

#ifdef _WIN32
    struct _stat info;
    if (_stat(nativePath, &info) != 0) {
        return false;
    }
    return (info.st_mode & _S_IFDIR) != 0;
#else
    struct stat info;
    if (stat(nativePath, &info) != 0) {
        return false;
    }
    return S_ISDIR(info.st_mode);
#endif
}

bool compat_file_exists(const char* filePath)
{
    char nativePath[COMPAT_MAX_PATH];
    if (compat_prepare_native_path(nativePath, filePath)) {
        return false;
    }

#ifdef _WIN32
    struct _stat info;
    if (_stat(nativePath, &info) != 0) {
        return false;
    }
    return (info.st_mode & _S_IFDIR) == 0;
#else
    struct stat info;
    if (stat(nativePath, &info) != 0) {
        return false;
    }
    return !S_ISDIR(info.st_mode);
#endif
}

unsigned int compat_timeGetTime()
{
#ifdef _WIN32
    return timeGetTime();
#else
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return static_cast<unsigned int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
#endif
}

FILE* compat_fopen(const char* path, const char* mode)
{
    char nativePath[COMPAT_MAX_PATH];
    if (compat_prepare_native_path(nativePath, path) && !compatFileModeMayWrite(mode)) {
        errno = ENOENT;
        return nullptr;
    }

    FILE* stream = fopen(nativePath, mode);
    if (compatFileModeMayWrite(mode) && stream != nullptr) {
        compatDirectoryEntryCacheUpdate(nativePath, true);
    }
    return stream;
}

gzFile compat_gzopen(const char* path, const char* mode)
{
    char nativePath[COMPAT_MAX_PATH];
    if (compat_prepare_native_path(nativePath, path) && !compatFileModeMayWrite(mode)) {
        errno = ENOENT;
        return nullptr;
    }

    gzFile stream = gzopen(nativePath, mode);
    if (compatFileModeMayWrite(mode) && stream != nullptr) {
        compatDirectoryEntryCacheUpdate(nativePath, true);
    }
    return stream;
}

char* compat_fgets(char* buffer, int maxCount, FILE* stream)
{
    buffer = fgets(buffer, maxCount, stream);

    if (buffer != nullptr) {
        size_t len = strlen(buffer);
        if (len >= 2 && buffer[len - 1] == '\n' && buffer[len - 2] == '\r') {
            buffer[len - 2] = '\n';
            buffer[len - 1] = '\0';
        }
    }

    return buffer;
}

char* compat_gzgets(gzFile stream, char* buffer, int maxCount)
{
    buffer = gzgets(stream, buffer, maxCount);

    if (buffer != nullptr) {
        size_t len = strlen(buffer);
        if (len >= 2 && buffer[len - 1] == '\n' && buffer[len - 2] == '\r') {
            buffer[len - 2] = '\n';
            buffer[len - 1] = '\0';
        }
    }

    return buffer;
}

int compat_remove(const char* path)
{
    char nativePath[COMPAT_MAX_PATH];
    if (compat_prepare_native_path(nativePath, path)) {
        errno = ENOENT;
        return -1;
    }
    int rc = remove(nativePath);
    if (rc == 0) {
        compatDirectoryEntryCacheUpdate(nativePath, false);
    }
    return rc;
}

int compat_rename(const char* oldFileName, const char* newFileName)
{
    char nativeOldFileName[COMPAT_MAX_PATH];
    compat_prepare_native_path(nativeOldFileName, oldFileName);

    char nativeNewFileName[COMPAT_MAX_PATH];
    compat_prepare_native_path(nativeNewFileName, newFileName);

    int rc = rename(nativeOldFileName, nativeNewFileName);
    if (rc == 0) {
        // A renamed directory takes its subtree's listings with it.
        compatDirectoryEntryCacheClear();
    }
    return rc;
}

void compat_windows_path_to_native(char* path)
{
#ifndef _WIN32
    char* pch = path;
    while (*pch != '\0') {
        if (*pch == '\\') {
            *pch = '/';
        }
        pch++;
    }
#endif
}

void compat_resolve_path(char* path)
{
    compatResolvePath(path);
}

#ifndef _WIN32
// CE: [path]'s components in their directories' case. False: one of them is
// known not to be there (its directory's listing hasn't it, or that
// directory is missing) - the path isn't (nothing outside these functions
// changes the game's directories while it runs, except the saves').
static bool compatResolvePath(char* path)
{
    char* pch = path;
    // Keys from the root: a relative path from the working directory.
    std::string base;
    std::string directoryPath;
    if (pch[0] == '/') {
        directoryPath = "/";
        pch++;
    } else {
        base = compatAbsolutePath(".");
        directoryPath = base;
    }

    while (true) {
        while (*pch == '/') {
            pch++;
        }

        if (*pch == '\0') {
            break;
        }

        char* sep = strchr(pch, '/');
        size_t length;
        if (sep != nullptr) {
            length = sep - pch;
        } else {
            length = strlen(pch);
        }

        const CompatDirectoryCacheEntry* directoryCacheEntry = compatDirectoryEntryCacheGet(directoryPath);
        if (directoryCacheEntry == nullptr) {
            return compatMissingDirectories.count(directoryPath) == 0;
        }

        std::string component(pch, length);
        auto cacheEntry = directoryCacheEntry->entries.find(compatLowercase(component));
        if (cacheEntry == directoryCacheEntry->entries.end()) {
            return false;
        }

        strncpy(pch, cacheEntry->second.c_str(), length);

        if (sep == nullptr) {
            break;
        }

        *sep = '\0';
        directoryPath = base.empty() ? std::string(path) : base + "/" + path;
        *sep = '/';

        pch = sep + 1;
    }
    return true;
}
#else
static bool compatResolvePath(char*)
{
    return true;
}
#endif

int compat_access(const char* path, int mode)
{
    char nativePath[COMPAT_MAX_PATH];
    if (compat_prepare_native_path(nativePath, path)) {
        errno = ENOENT;
        return -1;
    }
    return access(nativePath, mode);
}

char* compat_strdup(const char* string)
{
    return SDL_strdup(string);
}

// It's a replacement for compat_filelength(fileno(stream)) on platforms without
// fileno defined.
long getFileSize(FILE* stream)
{
    long originalOffset = ftell(stream);
    fseek(stream, 0, SEEK_END);
    long filesize = ftell(stream);
    fseek(stream, originalOffset, SEEK_SET);
    return filesize;
}

} // namespace fallout
