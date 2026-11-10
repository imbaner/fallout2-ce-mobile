#ifndef FALLOUT_FILE_TREE_H_
#define FALLOUT_FILE_TREE_H_

#include <stdint.h>

#include <functional>
#include <string>

namespace fallout {

// A game's folder walked with its files' names as the bytes they are (the
// Android app's moves and deletions: Java reads names as UTF-8 and can't open
// a name in another encoding, such as cp1251 of a game copied from Windows).
// Links are taken as links, never followed.

// What failed: errno and the path (its bytes) it failed at.
struct FileTreeError {
    int code = 0;
    std::string operation;
    std::string path;

    // "<operation> <path>: <reason>", the name's bytes other than printable
    // ASCII as \xNN.
    std::string message() const;
};

// The bytes of the files under [path] (a file: its size).
bool fileTreeSize(const std::string& path, uint64_t* size, FileTreeError* error);

// [from] (a folder or a file) copied to [to]: the files' data written to the
// storage (fsync), each checked for its size, their times kept. [progress]
// gets the bytes copied so far; false from it stops the copy (ECANCELED).
// What was copied before a failure stays (the caller removes it).
bool fileTreeCopy(const std::string& from, const std::string& to, const std::function<bool(uint64_t)>& progress, FileTreeError* error);

// [path] and everything under it removed (none there: done).
bool fileTreeDelete(const std::string& path, FileTreeError* error);

} // namespace fallout

#endif /* FALLOUT_FILE_TREE_H_ */
