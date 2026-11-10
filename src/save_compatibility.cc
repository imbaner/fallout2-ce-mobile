#include "save_compatibility.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "dfile.h"
#include "loadsave.h"
#include "platform_compat.h"
#include "save_records.h"
#include "settings.h"
#include "xfile.h"

namespace fallout {

namespace {

    // A composition the app's import gave a save: the game's at the next
    // start.
    constexpr const char* kCurrentMark = "*";

    std::string gCurrent;
    bool gCurrentKnown = false;

    // "mods\RPU.dat" -> "mods/rpu.dat".
    std::string componentName(const char* path)
    {
        std::string name = path;
        for (char& ch : name) {
            ch = ch == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(ch)));
        }
        while (name.compare(0, 2, "./") == 0) {
            name.erase(0, 2);
        }
        return name;
    }

    bool isGameArchive(const std::string& name)
    {
        std::string base = name.substr(name.find_last_of('/') + 1);
        return base == "master.dat" || base == "critter.dat" || base == "patch000.dat";
    }

    // Folders of game logic in the game's archives: a save keeps state of
    // their objects, scripts and maps.
    bool isLogicPath(const char* path)
    {
        static const char* const kFolders[] = { "proto\\", "scripts\\", "maps\\", "data\\" };
        for (const char* folder : kFolders) {
            if (compat_strnicmp(path, folder, strlen(folder)) == 0) {
                return true;
            }
        }
        return false;
    }

    // The archives as the engine opened them, in that order (the list keeps
    // the one searched first first).
    void computeCurrent()
    {
        if (gCurrentKnown) {
            return;
        }
        gCurrentKnown = true;

        std::vector<std::string> components;
        for (const XBase* base = xbaseGetFirst(); base != nullptr; base = base->next) {
            // Folders (data\) aren't part of it.
            if (!base->isDbase || base->dbase == nullptr) {
                continue;
            }

            std::string name = componentName(base->path);
            // The port's own (comes with the app, not the game's): next to
            // the game, or in the app's folder (Android, macOS bundle).
            if (name.substr(name.find_last_of('/') + 1) == "ce.dat") {
                continue;
            }
            char kind = isGameArchive(name) ? 'G' : 'C';

            // FNV-1a over the index: names, sizes, compression.
            uint64_t hash = 14695981039346656037ull;
            auto mix = [&hash](const void* data, size_t size) {
                const unsigned char* bytes = static_cast<const unsigned char*>(data);
                for (size_t index = 0; index < size; index++) {
                    hash = (hash ^ bytes[index]) * 1099511628211ull;
                }
            };
            const DBase* dbase = base->dbase;
            for (int index = 0; index < dbase->entriesLength; index++) {
                const DBaseEntry& entry = dbase->entries[index];
                mix(entry.path, strlen(entry.path) + 1);
                mix(&entry.uncompressedSize, sizeof(entry.uncompressedSize));
                mix(&entry.dataSize, sizeof(entry.dataSize));
                mix(&entry.compressed, sizeof(entry.compressed));
                if (kind == 'C' && isLogicPath(entry.path)) {
                    kind = 'L';
                }
            }

            char item[512];
            snprintf(item, sizeof(item), "%s=%016llx:%c", name.c_str(), static_cast<unsigned long long>(hash), kind);
            components.push_back(item);
        }
        std::reverse(components.begin(), components.end());

        gCurrent.clear();
        for (const std::string& item : components) {
            if (!gCurrent.empty()) {
                gCurrent += ';';
            }
            gCurrent += item;
        }
    }

    // [slot]'s save and its record ("*" made the game's now).
    bool recordOf(int slot, std::string* identity, SaveRecord* record)
    {
        computeCurrent();
        *identity = saveRecordId(lsgMobileSaveDatPath(slot));
        SaveRecords& records = gameSaveRecords();
        if (!records.find(slot, *identity, record)) {
            return false;
        }
        if (record->composition == kCurrentMark) {
            record->composition = gCurrent;
            records.put(slot, *identity, *record);
        }
        return true;
    }

} // namespace

const std::string& saveCompatibilityCurrent()
{
    computeCurrent();
    return gCurrent;
}

void saveCompatibilityRecord(int slot)
{
    std::string identity;
    SaveRecord record;
    if (recordOf(slot, &identity, &record)) {
        record.composition = gCurrent;
        gameSaveRecords().put(slot, identity, record);
    }
}

SaveCompatibility saveCompatibilityCheck(int slot, std::vector<SaveCompatibilityChange>* changes)
{
    if (changes != nullptr) {
        changes->clear();
    }
    if (!settings.enhancements.save_compatibility) {
        return SaveCompatibility::kSame;
    }

    std::string identity;
    SaveRecord record;
    if (!recordOf(slot, &identity, &record)) {
        return SaveCompatibility::kUnknown;
    }
    return saveCompositionCompare(record.composition, gCurrent, changes);
}

} // namespace fallout
