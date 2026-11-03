#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <emscripten.h>
#include <unistd.h>

static int exercise(int argc, char** argv)
{
    bool bad = argc > 1 && std::strcmp(argv[1], "bad") == 0;
    bool reopened = argc > 1 && std::strcmp(argv[1], "reopen") == 0;
    const size_t size = 256 * 1024 * 3 + 17;
    unsigned char* data = static_cast<unsigned char*>(std::malloc(size));
    FILE* input = std::fopen("MASTER.DAT", "rb");
    if (input == nullptr) return 1;
    size_t count = std::fread(data, 1, size, input);
    if (bad) {
        bool failed = count == 0 && std::ferror(input);
        std::fclose(input); std::free(data);
        return failed ? 0 : 2;
    }
    if (count != size) return 3;
    for (size_t i = 0; i < size; ++i) if (data[i] != i % 251) return 4;
    std::fclose(input); std::free(data);
    if (reopened) {
        // The fresh mount has none of the slot's nodes cached: access() must
        // wait for them like open() (the game checks ce.sav this way on load).
        if (access("DATA/SAVEGAME/SLOT01/SAVE.DAT", F_OK) != 0) return 13;
        FILE* save = std::fopen("DATA/SAVEGAME/SLOT01/SAVE.DAT", "rb");
        if (!save) return 5;
        char bytes[5] = {};
        bool good = std::fread(bytes, 1, 4, save) == 4 && std::strcmp(bytes, "save") == 0;
        std::fclose(save);
        if (!good) return 6;
        if (std::rename("DATA/SAVEGAME/SLOT01", "SAVEGAME/.stage-slot") != 0) return 11;
        if (std::rename("SAVEGAME/.stage-slot", "data/SAVEGAME/SLOT01") != 0) return 12;
        return 0;
    }
    for (const char* path : {"data/proto/items/test.pro", "data/config/game.cfg", "master.dat"}) {
        FILE* output = std::fopen(path, "wb");
        if (!output) return 7;
        if (std::fwrite("edit", 1, 4, output) != 4 || std::fclose(output)) return 8;
    }
    FILE* map = std::fopen("data/maps/automap.db", "wb");
    if (!map) return 9;
    std::fwrite("map", 1, 3, map); std::fclose(map);
    map = std::fopen("data/maps/automap.db", "r+b");
    if (!map) return 10;
    std::fseek(map, 1, SEEK_SET); std::fwrite("X", 1, 1, map); std::fclose(map);
    return 0;
}

int main(int argc, char** argv)
{
    int result = exercise(argc, argv);
    // Like the game's event loop, finish through an Emscripten timer callback;
    // a naked ExitStatus thrown from a Node Promise continuation is unhandled.
    emscripten_sleep(1);
    emscripten_force_exit(result);
    return result;
}
