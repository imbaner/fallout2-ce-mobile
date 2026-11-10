// The file layer's answers from its cache of directories (src/platform_compat.cc):
// a miss without asking the storage once a directory is listed or found
// missing; files and directories made through the layer found at once; under
// SAVEGAME the storage always asked (the save catalog writes it with its own
// calls); the working directory changed for a while (xfile.cc) doesn't mix
// directories up; a file made past the layer elsewhere is a miss until the
// cache is cleared (the rule the answers rest on).

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "platform_compat.h"

using namespace fallout;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: line %d: %s\n", __LINE__, #condition);     \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    } while (false)

namespace {

void writeRaw(const char* path, const char* text)
{
    FILE* stream = fopen(path, "wb");
    CHECK(stream != nullptr);
    fputs(text, stream);
    fclose(stream);
}

bool opens(const char* path)
{
    FILE* stream = compat_fopen(path, "rb");
    if (stream == nullptr) {
        return false;
    }
    fclose(stream);
    return true;
}

} // namespace

int main()
{
    char base[] = "/tmp/platform_compat_test.XXXXXX";
    CHECK(mkdtemp(base) != nullptr);
    CHECK(chdir(base) == 0);
    CHECK(mkdir("data", 0755) == 0);
    writeRaw("master.dat", "dat");
    writeRaw("data/readme.txt", "readme");
    compatDirectoryEntryCacheClear();

    // Found in its directory's case.
    CHECK(opens("MASTER.DAT"));
    CHECK(opens("data\\README.TXT"));

    // A miss in a listed directory, and in one that isn't there.
    errno = 0;
    CHECK(!opens("data\\patch.txt"));
    CHECK(errno == ENOENT);
    errno = 0;
    CHECK(!opens("data\\art\\critters\\hmjmpsaa.frm"));
    CHECK(errno == ENOENT);
    CHECK(!compat_file_exists("data\\art\\intrface\\x.frm"));
    CHECK(!compat_is_dir("data\\art"));
    errno = 0;
    CHECK(compat_remove("data\\proto\\critters\\00000001.pro") == -1);
    CHECK(errno == ENOENT);

    // Made through the layer: found at once.
    CHECK(compat_mkdir("data\\art") == 0);
    CHECK(compat_is_dir("data\\art"));
    CHECK(compat_mkdir_recursive("data\\art\\critters\\") == 0 || compat_is_dir("data\\art\\critters"));
    FILE* made = compat_fopen("data\\art\\critters\\hmjmpsaa.frm", "wb");
    CHECK(made != nullptr);
    fputs("frm", made);
    fclose(made);
    CHECK(opens("data\\art\\critters\\HMJMPSAA.FRM"));
    gzFile packed = compat_gzopen("data\\packed.gz", "wb");
    CHECK(packed != nullptr);
    gzclose(packed);
    CHECK(compat_file_exists("data\\packed.gz"));

    // Removed through the layer: a miss.
    CHECK(compat_remove("data\\art\\critters\\hmjmpsaa.frm") == 0);
    CHECK(!opens("data\\art\\critters\\hmjmpsaa.frm"));

    // Renamed through the layer: both names right.
    writeRaw("data/old.txt", "x");
    compatDirectoryEntryCacheClear();
    CHECK(opens("data\\old.txt"));
    CHECK(compat_rename("data\\old.txt", "data\\new.txt") == 0);
    CHECK(!opens("data\\old.txt"));
    CHECK(opens("data\\new.txt"));

    // Made past the layer: a miss until the cache is cleared (outside
    // SAVEGAME nothing does that while the game runs).
    CHECK(!opens("data\\later.txt"));
    writeRaw("data/later.txt", "later");
    CHECK(!opens("data\\later.txt"));
    compatDirectoryEntryCacheClear();
    CHECK(opens("data\\LATER.TXT"));

    // Under SAVEGAME the storage is always asked: the save catalog's own
    // writes are found without clearing anything.
    CHECK(!opens("data\\SAVEGAME\\SLOT01\\SAVE.DAT"));
    CHECK(mkdir("data/SAVEGAME", 0755) == 0);
    CHECK(mkdir("data/SAVEGAME/SLOT01", 0755) == 0);
    writeRaw("data/SAVEGAME/SLOT01/SAVE.DAT", "save");
    CHECK(opens("data\\SAVEGAME\\SLOT01\\SAVE.DAT"));

    // The working directory changed for a while (xfile.cc makes directories
    // so): what is listed there isn't taken for the game's directory.
    compatDirectoryEntryCacheClear();
    CHECK(chdir("data") == 0);
    CHECK(!opens("master.dat"));
    CHECK(compat_mkdir("maps") == 0);
    CHECK(chdir("..") == 0);
    CHECK(opens("master.dat"));
    CHECK(compat_is_dir("data\\maps"));
    CHECK(!compat_is_dir("maps"));

    // Absolute paths too.
    std::string absolute = std::string(base) + "/data/readme.txt";
    CHECK(opens(absolute.c_str()));
    std::string absoluteMiss = std::string(base) + "/data/none.txt";
    CHECK(!opens(absoluteMiss.c_str()));

    CHECK(chdir("/") == 0);
    std::string clean = std::string("rm -rf '") + base + "'";
    CHECK(system(clean.c_str()) == 0);
    printf("platform_compat_test: ok\n");
    return 0;
}
