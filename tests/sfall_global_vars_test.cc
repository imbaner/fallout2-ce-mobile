// sfall's global variables (src/sfall_global_vars.cc): a name of 8 characters
// found at any address (script strings, literals: an 8-byte load from an odd
// address faults on 32-bit ARM; built with -fsanitize=alignment here, which
// reports it on any machine); names and numbers kept apart; 0 removes a
// variable; save and load keep them all.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include "sfall_global_vars.h"

using namespace fallout;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: line %d: %s\n", __LINE__, #condition);     \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    } while (false)

// The save file: a byte string instead of the game's files (db.h).
namespace {
std::string gSaved;
size_t gReadAt = 0;
} // namespace

namespace fallout {

size_t fileRead(void* buf, size_t size, size_t count, File* stream)
{
    size_t bytes = size * count;
    if (gReadAt + bytes > gSaved.size()) {
        return 0;
    }
    memcpy(buf, gSaved.data() + gReadAt, bytes);
    gReadAt += bytes;
    return count;
}

size_t fileWrite(const void* buf, size_t size, size_t count, File* stream)
{
    gSaved.append(static_cast<const char*>(buf), size * count);
    return count;
}

} // namespace fallout

int main()
{
    CHECK(sfall_gl_vars_init());

    // The same name at an odd address and an even one.
    char buffer[32];
    for (int offset = 0; offset < 8; offset++) {
        char* name = buffer + offset;
        strcpy(name, "HAp_Race");
        CHECK(sfall_gl_vars_store(name, 10 + offset));
        int value = 0;
        CHECK(sfall_gl_vars_fetch("HAp_Race", value));
        CHECK(value == 10 + offset);
        value = 0;
        CHECK(sfall_gl_vars_fetch(name, value));
        CHECK(value == 10 + offset);
    }

    // Only names of 8 characters.
    int value = 7;
    CHECK(!sfall_gl_vars_store("short", 1));
    CHECK(!sfall_gl_vars_fetch("short", value));
    CHECK(value == 7);

    // A number is a variable of its own; 0 removes one.
    CHECK(sfall_gl_vars_store(42, 5));
    CHECK(sfall_gl_vars_store("HApStyle", 3));
    CHECK(sfall_gl_vars_store("HApStyle", 0));
    value = -1;
    CHECK(!sfall_gl_vars_fetch("HApStyle", value));
    CHECK(value == -1);

    // Saved and loaded.
    gSaved.clear();
    CHECK(sfall_gl_vars_save(nullptr));
    sfall_gl_vars_reset();
    CHECK(!sfall_gl_vars_fetch(42, value));
    gReadAt = 0;
    CHECK(sfall_gl_vars_load(nullptr));
    CHECK(sfall_gl_vars_fetch(42, value) && value == 5);
    CHECK(sfall_gl_vars_fetch("HAp_Race", value) && value == 17);

    sfall_gl_vars_exit();
    printf("sfall_global_vars_test: ok\n");
    return 0;
}
