// Game folder walks test (src/file_tree.cc): a copy has every file whole,
// its times and links; the size counts files only; progress grows to the
// total and false from it stops the copy; a deletion leaves nothing; names
// not in UTF-8 (cp1251 of a game from Windows) are copied, counted and
// deleted where the file system takes them (APFS doesn't).

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "file_tree.h"

using fallout::FileTreeError;
using fallout::fileTreeCopy;
using fallout::fileTreeDelete;
using fallout::fileTreeSize;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: line %d: %s\n", __LINE__, #condition);     \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    } while (false)

namespace {

std::string bytes(size_t length, unsigned seed)
{
    std::string data(length, '\0');
    for (size_t index = 0; index < length; index++) {
        data[index] = static_cast<char>((index * 31 + seed) & 0xFF);
    }
    return data;
}

bool writeFile(const std::string& path, const std::string& data)
{
    FILE* stream = fopen(path.c_str(), "wb");
    if (stream == nullptr) {
        return false;
    }
    bool ok = fwrite(data.data(), 1, data.size(), stream) == data.size();
    return fclose(stream) == 0 && ok;
}

std::string readFile(const std::string& path)
{
    std::string data;
    FILE* stream = fopen(path.c_str(), "rb");
    if (stream == nullptr) {
        return "<missing>";
    }
    char buffer[65536];
    size_t read;
    while ((read = fread(buffer, 1, sizeof(buffer), stream)) > 0) {
        data.append(buffer, read);
    }
    fclose(stream);
    return data;
}

bool exists(const std::string& path)
{
    struct stat info;
    return lstat(path.c_str(), &info) == 0;
}

time_t modified(const std::string& path)
{
    struct stat info;
    return stat(path.c_str(), &info) == 0 ? info.st_mtime : 0;
}

} // namespace

int main()
{
    char base[] = "/tmp/file_tree_test.XXXXXX";
    CHECK(mkdtemp(base) != nullptr);
    std::string root = base;
    std::string from = root + "/game";
    std::string to = root + "/copy";

    // A game: nested folders, an empty file, one larger than the copy's
    // buffer, a link, an empty folder.
    CHECK(mkdir(from.c_str(), 0770) == 0);
    CHECK(mkdir((from + "/data").c_str(), 0770) == 0);
    CHECK(mkdir((from + "/data/SAVEGAME").c_str(), 0770) == 0);
    CHECK(mkdir((from + "/data/SAVEGAME/SLOT01").c_str(), 0770) == 0);
    CHECK(mkdir((from + "/mods").c_str(), 0770) == 0);
    std::string big = bytes(3 * 1024 * 1024 + 17, 1);
    std::string save = bytes(4096, 2);
    CHECK(writeFile(from + "/master.dat", big));
    CHECK(writeFile(from + "/fallout2.cfg", ""));
    CHECK(writeFile(from + "/data/SAVEGAME/SLOT01/SAVE.DAT", save));
    CHECK(symlink("master.dat", (from + "/link.dat").c_str()) == 0);
    struct timespec old[2] = { { 1000000000, 0 }, { 1000000000, 0 } };
    CHECK(utimensat(AT_FDCWD, (from + "/data/SAVEGAME/SLOT01/SAVE.DAT").c_str(), old, 0) == 0);

    // A name in cp1251 ("ПЕЧАТЬjo.TXT"), where the file system takes it.
    std::string odd = from + "/data/\xcf\xc5\xd7\xc0\xd2\xdcjo.TXT";
    bool oddNames = writeFile(odd, "readme");
    if (!oddNames) {
        fprintf(stderr, "note: this file system takes no names outside UTF-8 (%s)\n", strerror(errno));
    }

    uint64_t total = 0;
    FileTreeError error;
    CHECK(fileTreeSize(from, &total, &error));
    CHECK(total == big.size() + save.size() + (oddNames ? 6 : 0));
    uint64_t single = 0;
    CHECK(fileTreeSize(from + "/master.dat", &single, &error));
    CHECK(single == big.size());

    // The copy, its progress growing to the total.
    uint64_t last = 0;
    bool grows = true;
    CHECK(fileTreeCopy(from, to, [&](uint64_t done) {
        grows = grows && done >= last;
        last = done;
        return true;
    }, &error));
    CHECK(grows);
    CHECK(last == total);
    CHECK(readFile(to + "/master.dat") == big);
    CHECK(readFile(to + "/fallout2.cfg").empty());
    CHECK(readFile(to + "/data/SAVEGAME/SLOT01/SAVE.DAT") == save);
    CHECK(modified(to + "/data/SAVEGAME/SLOT01/SAVE.DAT") == 1000000000);
    CHECK(exists(to + "/mods"));
    char link[64] = {};
    CHECK(readlink((to + "/link.dat").c_str(), link, sizeof(link) - 1) > 0);
    CHECK(strcmp(link, "master.dat") == 0);
    if (oddNames) {
        CHECK(readFile(to + "/data/\xcf\xc5\xd7\xc0\xd2\xdcjo.TXT") == "readme");
    }
    uint64_t copiedTotal = 0;
    CHECK(fileTreeSize(to, &copiedTotal, &error));
    CHECK(copiedTotal == total);

    // Stopped by its progress: ECANCELED, the copy so far left.
    std::string stopped = root + "/stopped";
    CHECK(!fileTreeCopy(from, stopped, [](uint64_t done) { return done < 1024 * 1024; }, &error));
    CHECK(error.code == ECANCELED);

    // A source that isn't there.
    CHECK(!fileTreeCopy(root + "/none", root + "/none-copy", nullptr, &error));
    CHECK(error.code == ENOENT);

    // Messages show other bytes as \xNN.
    FileTreeError named;
    named.code = ENOENT;
    named.operation = "open";
    named.path = "data/\xcf\xc5jo.TXT";
    CHECK(named.message() == std::string("open data/\\xcf\\xc5jo.TXT: ") + strerror(ENOENT));

    // Deleted whole, the link not followed (its target stays until its own
    // turn); a missing path is no failure.
    CHECK(fileTreeDelete(stopped, &error));
    CHECK(!exists(stopped));
    CHECK(fileTreeDelete(to + "/link.dat", &error));
    CHECK(exists(to + "/master.dat"));
    CHECK(fileTreeDelete(to, &error));
    CHECK(!exists(to));
    CHECK(fileTreeDelete(to, &error));
    CHECK(fileTreeDelete(root, &error));
    CHECK(!exists(root));

    printf("file_tree_test: ok%s\n", oddNames ? "" : " (no cp1251 names here)");
    return 0;
}
