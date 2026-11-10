#include "game_config.h"
#include "debug.h"
#include "game_config_migration.h"
#include "settings.h"
#include "sfall_config.h"

#include <stdio.h>
#include <string.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "db.h"
#include "main.h"
#include "platform_compat.h"
#include "scan_unimplemented.h"

#if defined(__EMSCRIPTEN__)
#include "loadsave.h"
#endif

#ifdef __ANDROID__
#include <SDL.h>
#endif

namespace fallout {

constexpr char kDefaultGameConfigFileName[] = "fallout2.cfg";
constexpr char kMapperConfigFileName[] = "mapper2.cfg";

// A flag indicating if [gGameConfig] was initialized.
//
// 0x5186D0 gconfig_initialized
bool gGameConfigInitialized = false;

// fallout2.cfg
//
// 0x58E950 game_config
Config gGameConfig;

// NOTE: There are additional 4 bytes following this array at 0x58EA7C, which
// probably means it's size is 264 bytes.
//
// 0x58E978 gconfig_file_name
char gGameConfigFilePath[COMPAT_MAX_PATH];

struct CommandLineOverride {
    std::string section;
    std::string key;
    std::optional<std::string> originalValue;
};

static std::vector<CommandLineOverride> commandLineOverrides;

// CE: The port's own settings - its touch interface, the world view and its
// improvements of the game (settings.h) - where the app keeps several games
// (Android): in one file of the app's, the same for all of them. The game's
// fallout2.cfg keeps the game's settings, as on a computer.
static const char* const kPortSections[] = { "touch", "world_view", "enhancements" };

static bool gameConfigIsPortSection(const char* name)
{
    for (const char* section : kPortSections) {
        if (compat_stricmp(name, section) == 0) {
            return true;
        }
    }
    return false;
}

// The app's file of the port's settings, empty where they stay in the game's
// config (one game per folder).
static std::string gameConfigPortPath()
{
#ifdef __ANDROID__
    const char* internal = SDL_AndroidGetInternalStoragePath();
    if (internal != nullptr) {
        return std::string(internal) + "/f2touch.cfg";
    }
#endif
    return std::string();
}

// The port's sections of [source] ([port]) or the others, into [destination].
static bool gameConfigCopySections(Config* destination, const Config* source, bool port)
{
    for (int sectionIndex = 0; sectionIndex < source->entriesLength; sectionIndex++) {
        const DictionaryEntry& sectionEntry = source->entries[sectionIndex];
        if (gameConfigIsPortSection(sectionEntry.key) != port) {
            continue;
        }
        const auto* section = static_cast<const ConfigSection*>(sectionEntry.value);
        for (int keyIndex = 0; keyIndex < section->entriesLength; keyIndex++) {
            const DictionaryEntry& keyEntry = section->entries[keyIndex];
            if (!configSetString(destination, sectionEntry.key, keyEntry.key, *static_cast<char* const*>(keyEntry.value))) {
                return false;
            }
        }
    }
    return true;
}

// [config] whole into [path], through a temporary file: a run cut short
// leaves the old file.
static bool gameConfigWritePortFile(Config* config, const std::string& path)
{
    std::string tempPath = path + ".tmp";
    if (!configWriteEx(config, tempPath.c_str(), ConfigFlags::Default)) {
        compat_remove(tempPath.c_str());
        return false;
    }
    return compat_rename(tempPath.c_str(), path.c_str()) == 0;
}

// The port's sections out of the game's config at [path] (they're in the
// app's file): left by earlier builds, or by an import of the game's files.
static bool gameConfigDropPortSections(const char* path)
{
    FILE* original = compat_fopen(path, "rt");
    if (original == nullptr) {
        return true;
    }

    std::vector<std::string> lines;
    bool dropping = false;
    bool dropped = false;
    // As long as config.cc reads them.
    char line[2048];
    while (compat_fgets(line, sizeof(line), original) != nullptr) {
        const char* trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') {
            trimmed++;
        }
        if (*trimmed == '[') {
            std::string name(trimmed + 1);
            size_t end = name.find(']');
            name = end != std::string::npos ? name.substr(0, end) : std::string();
            while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) {
                name.erase(0, 1);
            }
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
                name.pop_back();
            }
            dropping = gameConfigIsPortSection(name.c_str());
            dropped |= dropping;
        }
        if (!dropping) {
            lines.emplace_back(line);
        }
    }
    fclose(original);

    if (!dropped) {
        return true;
    }

    std::string tempPath = std::string(path) + ".tmp";
    FILE* output = compat_fopen(tempPath.c_str(), "wt");
    if (output == nullptr) {
        return false;
    }
    for (const std::string& kept : lines) {
        fputs(kept.c_str(), output);
    }
    if (fclose(output) != 0) {
        compat_remove(tempPath.c_str());
        return false;
    }
    return compat_rename(tempPath.c_str(), path) == 0;
}

// [config] into the game's config file, the port's settings into the app's
// own where it keeps them apart.
static bool gameConfigWriteFiles(Config* config)
{
    std::string portPath = gameConfigPortPath();
    if (portPath.empty()) {
        return configWriteEx(config, gGameConfigFilePath, ConfigFlags::RetainAll);
    }

    ScopedConfig portConfig;
    ScopedConfig gameConfig;
    if (!portConfig || !gameConfig
        || !gameConfigCopySections(portConfig.get(), config, true)
        || !gameConfigCopySections(gameConfig.get(), config, false)) {
        return false;
    }
    if (!gameConfigWritePortFile(portConfig.get(), portPath)) {
        return false;
    }
    if (!configWriteEx(gameConfig.get(), gGameConfigFilePath, ConfigFlags::RetainAll)) {
        return false;
    }
    return gameConfigDropPortSections(gGameConfigFilePath);
}

static void gameConfigRecordCommandLineOverride(const char* section, const char* key)
{
    settingsMarkCommandLineOverride(section, key);

    for (const auto& override : commandLineOverrides) {
        if (compat_stricmp(override.section.c_str(), section) == 0
            && compat_stricmp(override.key.c_str(), key) == 0) {
            return;
        }
    }

    CommandLineOverride override;
    override.section = section;
    override.key = key;
    char* originalValue = nullptr;
    if (configGetString(&gGameConfig, section, key, &originalValue)) {
        override.originalValue = originalValue;
    }
    commandLineOverrides.push_back(std::move(override));
}

// Inits main game config.
//
// [isMapper] is a flag indicating whether we're initing config for a main
// game, or a mapper. This value is `false` for the game itself.
//
// [argc] and [argv] are command line arguments. The engine assumes there is
// at least 1 element which is executable path at index 0. There is no
// additional check for [argc], so it will crash if you pass NULL, or an empty
// array into [argv].
//
// The executable path from [argv] is used resolve path to `fallout2.cfg`,
// which should be in the same folder. This function provide defaults if
// `fallout2.cfg` is not present, or cannot be read for any reason.
//
// Finally, this function merges key-value pairs from [argv] if any, see
// [configParseCommandLineArguments] for expected format.
//
// 0x444570 gconfig_init
bool gameConfigInit(bool isMapper, int argc, char** argv)
{
    if (gGameConfigInitialized) {
        return false;
    }

    if (!configInit(&gGameConfig)) {
        return false;
    }
    commandLineOverrides.clear();

    // CE: Detect alternative default music directory.
    char alternativeMusicPath[COMPAT_MAX_PATH];
    strcpy(alternativeMusicPath, R"(data\sound\music\*.acm)");
    compat_windows_path_to_native(alternativeMusicPath);
    compat_resolve_path(alternativeMusicPath);

    char** acms;
    int acmsLength = fileNameListInit(alternativeMusicPath, &acms);
    if (acmsLength != -1) {
        if (acmsLength > 0) {
            // TODO: apply these to settings directly instead of config
            configSetString(&gGameConfig, GAME_CONFIG_SOUND_KEY, GAME_CONFIG_MUSIC_PATH1_KEY, R"(data\sound\music\)");
            configSetString(&gGameConfig, GAME_CONFIG_SOUND_KEY, GAME_CONFIG_MUSIC_PATH2_KEY, R"(data\sound\music\)");
        }
        fileNameListFree(&acms, 0);
    }

    // SFALL: Custom config file name.
    char* customConfigFileName = nullptr;
    configGetString(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_CONFIG_FILE, &customConfigFileName);

    const char* configFileName = customConfigFileName != nullptr && *customConfigFileName != '\0'
        ? customConfigFileName
        : kDefaultGameConfigFileName;

    // Make `fallout2.cfg` file path.
    char* executable = argv[0];
    char* ch = strrchr(executable, '\\');
    if (ch != nullptr) {
        *ch = '\0';
        if (isMapper) {
            snprintf(gGameConfigFilePath,
                sizeof(gGameConfigFilePath),
                "%s\\%s",
                executable,
                kMapperConfigFileName);
        } else {
            snprintf(gGameConfigFilePath,
                sizeof(gGameConfigFilePath),
                "%s\\%s",
                executable,
                configFileName);
        }
        *ch = '\\';
    } else {
        if (isMapper) {
            strcpy(gGameConfigFilePath, kMapperConfigFileName);
        } else {
            strcpy(gGameConfigFilePath, configFileName);
        }
    }

    configRead(&gGameConfig, gGameConfigFilePath, false);

    debugPrint("Game config loaded from %s.\n", gGameConfigFilePath);

    if (!isMapper && gameConfigMigrateFromF2Res(gGameConfigFilePath, &gGameConfig)) {
        debugPrint("Migrated settings from f2_res.ini.\n");
        gameConfigWriteFiles(&gGameConfig);
    }

    // CE: The port's settings kept by the app (`gameConfigPortPath`) win over
    // what the game's config has of them (an earlier build's, moved there at
    // the next save).
    std::string portPath = gameConfigPortPath();
    if (!isMapper && !portPath.empty() && configRead(&gGameConfig, portPath.c_str(), false)) {
        debugPrint("Port settings loaded from %s.\n", portPath.c_str());
    }

    // Add key-values from command line, which overrides both defaults and
    // whatever was loaded from `fallout2.cfg`.
    configParseCommandLineArguments(&gGameConfig, argc, argv, gameConfigRecordCommandLineOverride);

    // Writes default values to config, skipping keys that were already loaded.
    settingsWriteToConfig(true);

    gGameConfigInitialized = true;

    return true;
}

// Saves game config into the active config file.
//
// 0x444C14 gconfig_save
bool gameConfigSave()
{
    if (!gGameConfigInitialized) {
        return false;
    }

    // Strip process overrides from a copy so saving cannot alter active values
    // or invalidate strings held by config readers.
    ScopedConfig savedConfig;
    if (!savedConfig || !configCopy(savedConfig.get(), &gGameConfig)) {
        return false;
    }

    for (const auto& override : commandLineOverrides) {
        if (override.originalValue.has_value()) {
            if (!configSetString(savedConfig.get(), override.section.c_str(), override.key.c_str(), override.originalValue->c_str())) {
                return false;
            }
        } else {
            if (!configRemoveKey(savedConfig.get(), override.section.c_str(), override.key.c_str())) {
                return false;
            }
        }
    }

    if (!gameConfigWriteFiles(savedConfig.get())) {
#ifdef __EMSCRIPTEN__
        lsgWebCommitUserChanges(false);
#endif
        return false;
    }

#if defined(__EMSCRIPTEN__)
    return lsgWebCommitUserChanges(true);
#endif

    return true;
}

// Frees game config, optionally saving it.
//
// 0x444C3C gconfig_exit
bool gameConfigExit(bool shouldSave)
{
    if (!gGameConfigInitialized) {
        return false;
    }

    bool result = true;

    if (shouldSave) {
        if (!gameConfigSave()) {
            result = false;
        }
    }

    configFree(&gGameConfig);

    gGameConfigInitialized = false;
    commandLineOverrides.clear();

    return result;
}

} // namespace fallout
