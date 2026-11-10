#ifndef FALLOUT_SAVE_STORAGE_H_
#define FALLOUT_SAVE_STORAGE_H_

#include <cstdint>
#include <string>
#include <vector>

// File operations of the save catalog (save_catalog.cc) on native paths:
// POSIX / Win32 calls (the macOS 10.13 target has no std::filesystem).
// Links are never followed: a link is reported as neither a directory nor a
// regular file and removed as a link.
//
// Past the engine's file layer (platform_compat.cc): its cache of directories
// doesn't see these changes. It answers misses from that cache everywhere but
// under SAVEGAME, so what these calls write the engine reads through its
// layer only there (or after `compatDirectoryEntryCacheClear`).
namespace fallout::saveStorage {

struct Info {
    bool exists = false;
    bool directory = false;
    bool regular = false;
    std::uint64_t size = 0;
    std::int64_t modified = 0;
    std::int64_t nanos = 0;
};

// False only on an error (a missing path is `exists == false`).
bool inspect(const std::string& path, Info& info);
// True when the directory exists afterwards.
bool makeDirectory(const std::string& path);
// [path] and the directories under it as `makeDirectory` makes them (0755).
// Builds before 2026-10-01 made save slots 0700: the player couldn't back
// them up or move them (to another app id) with adb.
void openDirectories(const std::string& path);
bool children(const std::string& path, std::vector<std::string>& names);
// True when nothing is left (also when there was nothing).
bool removeTree(const std::string& path);
// Rename; fails when [to] exists.
bool move(const std::string& from, const std::string& to);
bool copyTree(const std::string& from, const std::string& to);
// Writes and flushes [value] to disk.
bool writeFile(const std::string& path, const std::string& value);
// Flushes every file under [path] to disk.
bool syncTree(const std::string& path);
bool syncDirectory(const std::string& path);

} // namespace fallout::saveStorage

#endif /* FALLOUT_SAVE_STORAGE_H_ */
