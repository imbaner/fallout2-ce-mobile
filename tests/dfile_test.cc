// Archive entries (src/dfile.cc) read right with the archive's streams kept
// for the next entries (DBase.idleStreams): several entries open at once
// and read by turns, plain and compressed; entries closed and opened again
// many times (more than the kept streams); seeks; an entry not there; the
// archive closed with streams kept.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <zlib.h>

#include "dfile.h"

using namespace fallout;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: line %d: %s\n", __LINE__, #condition);     \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    } while (false)

namespace {

struct Entry {
    std::string path;
    std::string data;
    bool compressed;
};

std::string pattern(size_t length, unsigned seed)
{
    std::string data(length, '\0');
    for (size_t index = 0; index < length; index++) {
        data[index] = static_cast<char>((index * 7 + index / 251 + seed) & 0xFF);
    }
    return data;
}

void put32(std::string& out, int32_t value)
{
    out.append(reinterpret_cast<const char*>(&value), 4);
}

// A Fallout 2 .DAT: the entries' data, then the tree (sorted paths), then
// the tree's size and the file's.
std::string makeDat(const std::vector<Entry>& entries)
{
    std::string out;
    std::string tree;
    put32(tree, static_cast<int32_t>(entries.size()));
    for (const Entry& entry : entries) {
        std::string stored = entry.data;
        if (entry.compressed) {
            uLongf size = compressBound(static_cast<uLong>(entry.data.size()));
            std::vector<Bytef> packed(size);
            CHECK(compress2(packed.data(), &size, reinterpret_cast<const Bytef*>(entry.data.data()), static_cast<uLong>(entry.data.size()), 9) == Z_OK);
            stored.assign(reinterpret_cast<const char*>(packed.data()), size);
        }
        int32_t offset = static_cast<int32_t>(out.size());
        out += stored;
        put32(tree, static_cast<int32_t>(entry.path.size()));
        tree += entry.path;
        tree += static_cast<char>(entry.compressed ? 1 : 0);
        put32(tree, static_cast<int32_t>(entry.data.size()));
        put32(tree, static_cast<int32_t>(stored.size()));
        put32(tree, offset);
    }
    out += tree;
    put32(out, static_cast<int32_t>(tree.size()));
    put32(out, static_cast<int32_t>(out.size() + 4));
    return out;
}

std::string readAll(DFile* stream)
{
    std::string data;
    char buffer[3001];
    size_t read;
    while ((read = dfileRead(buffer, 1, sizeof(buffer), stream)) > 0) {
        data.append(buffer, read);
    }
    return data;
}

} // namespace

int main()
{
    std::vector<Entry> entries = {
        { "ART\\A.FRM", pattern(100000, 1), false },
        { "ART\\B.FRM", pattern(50000, 2), true },
        { "PROTO\\ITEMS\\00000001.PRO", pattern(417, 3), false },
        { "TEXT\\C.MSG", pattern(9000, 4), true },
    };
    std::string dat = makeDat(entries);
    char path[] = "/tmp/dfile_test.XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    CHECK(write(fd, dat.data(), dat.size()) == static_cast<ssize_t>(dat.size()));
    close(fd);

    DBase* dbase = dbaseOpen(path);
    CHECK(dbase != nullptr);
    CHECK(dbase->idleStreamsLength == 0);

    // All open at once, read by turns in small pieces.
    DFile* streams[4];
    for (int index = 0; index < 4; index++) {
        streams[index] = dfileOpen(dbase, entries[index].path.c_str(), "rb");
        CHECK(streams[index] != nullptr);
    }
    std::string read[4];
    bool more = true;
    while (more) {
        more = false;
        for (int index = 0; index < 4; index++) {
            char buffer[777];
            size_t got = dfileRead(buffer, 1, sizeof(buffer), streams[index]);
            read[index].append(buffer, got);
            more = more || got > 0;
        }
    }
    for (int index = 0; index < 4; index++) {
        CHECK(read[index] == entries[index].data);
        dfileClose(streams[index]);
    }
    CHECK(dbase->idleStreamsLength == 4);

    // Closed and opened again, more times than streams are kept, two at once
    // (one kept stream taken while another entry reads), case as the game
    // asks.
    for (int round = 0; round < 50; round++) {
        const Entry& first = entries[round % 4];
        const Entry& second = entries[(round + 1) % 4];
        DFile* a = dfileOpen(dbase, first.path.c_str(), "rb");
        CHECK(a != nullptr);
        char head[100];
        size_t got = dfileRead(head, 1, sizeof(head), a);
        CHECK(std::string(head, got) == first.data.substr(0, got));
        DFile* b = dfileOpen(dbase, "art\\b.frm", "rb");
        CHECK(b != nullptr);
        CHECK(readAll(b) == entries[1].data);
        std::string rest = readAll(a);
        CHECK(std::string(head, got) + rest == first.data);
        dfileClose(b);
        dfileClose(a);
        CHECK(dbase->idleStreamsLength <= DBASE_IDLE_STREAMS);
        (void)second;
    }

    // Seeks, in a plain entry and a compressed one, on a kept stream.
    DFile* plain = dfileOpen(dbase, "ART\\A.FRM", "rb");
    CHECK(dfileSeek(plain, 60000, SEEK_SET) == 0);
    char piece[64];
    CHECK(dfileRead(piece, 1, sizeof(piece), plain) == sizeof(piece));
    CHECK(memcmp(piece, entries[0].data.data() + 60000, sizeof(piece)) == 0);
    // Seeks one after another in a plain entry (dfileSeek once moved the
    // stream, not `position`: the second went wrong), back and forth, from
    // the current position and the end; tell; a read stops at the end.
    CHECK(dfileSeek(plain, 10, SEEK_SET) == 0);
    CHECK(dfileTell(plain) == 10);
    CHECK(dfileRead(piece, 1, sizeof(piece), plain) == sizeof(piece));
    CHECK(memcmp(piece, entries[0].data.data() + 10, sizeof(piece)) == 0);
    CHECK(dfileSeek(plain, 1000, SEEK_CUR) == 0);
    CHECK(dfileTell(plain) == 10 + 64 + 1000);
    CHECK(dfileRead(piece, 1, sizeof(piece), plain) == sizeof(piece));
    CHECK(memcmp(piece, entries[0].data.data() + 1074, sizeof(piece)) == 0);
    CHECK(dfileSeek(plain, -100, SEEK_END) == 0);
    std::string tail = readAll(plain);
    CHECK(tail == entries[0].data.substr(entries[0].data.size() - 100));
    CHECK(dfileSeek(plain, 0, SEEK_SET) == 0);
    CHECK(dfileRead(piece, 1, sizeof(piece), plain) == sizeof(piece));
    CHECK(memcmp(piece, entries[0].data.data(), sizeof(piece)) == 0);
    DFile* packed = dfileOpen(dbase, "TEXT\\C.MSG", "rb");
    CHECK(dfileSeek(packed, 5000, SEEK_SET) == 0);
    CHECK(dfileRead(piece, 1, sizeof(piece), packed) == sizeof(piece));
    CHECK(memcmp(piece, entries[3].data.data() + 5000, sizeof(piece)) == 0);
    dfileClose(packed);
    dfileClose(plain);

    // Not there: nothing open, nothing lost from the kept streams.
    int kept = dbase->idleStreamsLength;
    CHECK(dfileOpen(dbase, "ART\\NONE.FRM", "rb") == nullptr);
    CHECK(dbase->idleStreamsLength == kept);

    // Closed with entries open and streams kept.
    CHECK(dfileOpen(dbase, "ART\\A.FRM", "rb") != nullptr);
    CHECK(dbaseClose(dbase));

    remove(path);
    printf("dfile_test: ok\n");
    return 0;
}
