#include "mui.h"
#include "mui_screens.h"

#include <stdio.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "app_update.h"
#include "db.h"
#include "dbox.h"
#include "debug.h"
#include "dev_autotest.h"
#include "fps_limiter.h"
#include "game.h"
#include "game_sound.h"
#include "input.h"
#include "kb.h"
#include "loadsave.h"
#include "message.h"
#include "mui_notify.h"
#include "mui_update.h"
#include "platform/git_version.h"
#include "platform_compat.h"
#include "preferences.h"
#include "settings.h"
#include "svga.h"
#include "world_view.h"
#include "xfile.h"

#include <SDL.h>

#ifdef __ANDROID__
#include <jni.h>
#endif

namespace fallout {

namespace {

    const MuiColor kBackground = muiRgb(0x060A07);
    const MuiColor kPanel = muiRgb(0x0B120D);
    const MuiColor kRowLine = muiRgb(0x12301A);
    const MuiColor kChanged = muiRgb(0xFFB000);
    const MuiColor kPortBadge = muiRgb(0xFFB000);

    // Texts in `game\ce.msg`.
    constexpr int kTextSectionGame = 245;
    constexpr int kTextSectionCombat = 246;
    constexpr int kTextSectionSound = 247;
    constexpr int kTextSectionDisplay = 248;
    constexpr int kTextPortBadge = 249;
    constexpr int kTextGameDifficulty = 250;
    constexpr int kTextViolenceLevel = 251;
    constexpr int kTextRunning = 252;
    constexpr int kTextSubtitles = 253;
    constexpr int kTextLanguageFilter = 254;
    constexpr int kTextTextDelay = 255;
    constexpr int kTextConvenience = 256;
    constexpr int kTextAutoOpenDoors = 257;
    constexpr int kTextWalkWhenSneaking = 258;
    constexpr int kTextFastAmmoLoad = 259;
    constexpr int kTextPartyMembers = 260;
    constexpr int kTextPartyTradeFromMenu = 261;
    constexpr int kTextPartyLootAndBarter = 262;
    constexpr int kTextPartyMemberExtraInfo = 263;
    constexpr int kTextCombatDifficulty = 264;
    constexpr int kTextCombatSpeed = 265;
    constexpr int kTextPlayerSpeedup = 266;
    constexpr int kTextAllAnimations = 267;
    constexpr int kTextAllAnimationsNote = 268;
    constexpr int kTextCombatLooks = 269;
    constexpr int kTextCombatMessages = 270;
    constexpr int kTextCombatTaunts = 271;
    constexpr int kTextTargetHighlight = 272;
    constexpr int kTextMasterVolume = 273;
    constexpr int kTextMusicVolume = 274;
    constexpr int kTextSoundEffectsVolume = 275;
    constexpr int kTextSpeechVolume = 276;
    constexpr int kTextBrightness = 277;
    constexpr int kTextItemHighlight = 278;
    constexpr int kTextInterfaceSize = 279;
    constexpr int kTextMapFilter = 280;
    constexpr int kTextFilterNearest = 281;
    constexpr int kTextFilterLinear = 282;
    constexpr int kTextFilterSharp = 283;
    constexpr int kTextResetAll = 284;
    constexpr int kTextApply = 285;
    constexpr int kTextResetTitle = 286;
    constexpr int kTextResetBody = 287;
    constexpr int kTextApplyTitle = 288;
    constexpr int kTextSeconds = 289;
    constexpr int kTextMainMenuContinue = 311;
    constexpr int kTextMainMenuContinueNote = 312;
    constexpr int kTextWorldmapFollow = 322;
    constexpr int kTextWorldmapFollowNote = 323;
    constexpr int kTextTacticalView = 373;
    constexpr int kTextTacticalViewNote = 374;
    constexpr int kTextSaveCompatibility = 327;
    constexpr int kTextSaveCompatibilityNote = 328;
    constexpr int kTextSectionFiles = 336;
    constexpr int kTextFilesInstalled = 337;
    constexpr int kTextReplaceGame = 338;
    constexpr int kTextReplaceGameNote = 339;
    constexpr int kTextImportSaves = 340;
    constexpr int kTextImportSavesNote = 341;
    constexpr int kTextExportSaves = 342;
    constexpr int kTextExportSavesNote = 343;
    constexpr int kTextDeleteSaves = 344;
    constexpr int kTextDeleteSavesNote = 345;
    constexpr int kTextCloseGameTitle = 346;
    constexpr int kTextCloseGameInGame = 347;
    constexpr int kTextCloseGameMenu = 348;
    constexpr int kTextDeleteSavesTitle = 349;
    constexpr int kTextDeleteSavesBody = 350;
    constexpr int kTextDeleteSavesFailed = 351;
    constexpr int kTextReplaceButton = 352;
    constexpr int kTextImportButton = 353;
    constexpr int kTextExportButton = 354;
    constexpr int kTextDeleteButton = 355;
    constexpr int kTextKarmaChanges = 356;
    constexpr int kTextKarmaChangesNote = 357;
    constexpr int kTextBonusDamage = 358;
    constexpr int kTextBonusDamageNote = 359;
    constexpr int kTextQuickSaves = 360;
    constexpr int kTextQuickSavesNote = 361;
    constexpr int kTextQuickSavesOff = 362;
    constexpr int kTextQuickSavesOldest = 363;
    constexpr int kTextQuickSavesAll = 364;
    constexpr int kTextQuickSavesButton = 365;
    constexpr int kTextQuickSavesFailed = 366;
    constexpr int kTextSectionAbout = 367;
    constexpr int kTextAboutVersion = 368;
    constexpr int kTextAboutNotice = 369;
    constexpr int kTextAboutLicenses = 372;

#ifndef FALLOUT_APP_VERSION
#define FALLOUT_APP_VERSION "1.3.0"
#endif

    // The port's number of quick saves (one page, the phone's config); CE's
    // own default is none.
    constexpr int kDefaultQuickSaves = 10;

    // Texts in OPTIONS.MSG (the game's window: values).
    constexpr int kMessageOn = 201;
    constexpr int kMessageOff = 202;
    constexpr int kMessageEasy = 203;
    constexpr int kMessageNormal = 204;
    constexpr int kMessageHard = 205;
    constexpr int kMessageWimpy = 206;
    constexpr int kMessageRough = 208;
    constexpr int kMessageVerbose = 211;
    constexpr int kMessageBrief = 212;
    constexpr int kMessageTargetingOnly = 213;
    constexpr int kMessageNone = 214;
    constexpr int kMessageMinimal = 215;
    constexpr int kMessageMaximumBlood = 216;
    constexpr int kMessageAlways = 219;

    // Interface size range, %.
    constexpr int kInterfaceSizeMin = 60;
    constexpr int kInterfaceSizeMax = 150;

    // Everything the screen edits: the game's preferences (also kept in
    // saves) and this port's / CE's settings from fallout2.cfg.
    struct Values {
        PreferenceValues preferences;
        bool allAnimations;
        bool mainMenuContinue;
        bool worldmapFollow;
        bool tacticalView;
        bool saveCompatibility;
        int interfaceSize;
        int mapFilter;
        bool autoOpenDoors;
        bool walkWhenSneaking;
        bool fastAmmoLoad;
        bool partyTradeFromMenu;
        bool partyLootAndBarter;
        bool partyMemberExtraInfo;
        bool karmaChanges;
        bool bonusDamage;
        // Tens, 0 - none (`lsgSetQuickSaveCount`).
        int quickSaves;
    };

    Values currentValues()
    {
        Values values;
        values.preferences = preferencesGetValues();
        values.allAnimations = settings.enhancements.combat_speed_all_animations;
        values.mainMenuContinue = settings.enhancements.main_menu_continue;
        values.worldmapFollow = settings.enhancements.worldmap_follow_party;
        values.tacticalView = settings.enhancements.tactical_view;
        values.saveCompatibility = settings.enhancements.save_compatibility;
        values.interfaceSize = settings.touch.hud_scale;
        values.mapFilter = settings.world_view.filter;
        values.autoOpenDoors = settings.qol.auto_open_doors;
        values.walkWhenSneaking = settings.qol.walk_when_sneaking;
        values.fastAmmoLoad = settings.qol.fast_ammo_load;
        values.partyTradeFromMenu = settings.qol.party_trade_from_menu;
        values.partyLootAndBarter = settings.qol.party_loot_and_barter;
        values.partyMemberExtraInfo = settings.ui.party_member_extra_info;
        values.karmaChanges = settings.ui.display_karma_changes;
        values.bonusDamage = settings.ui.display_bonus_damage;
        values.quickSaves = lsgQuickSaveCount();
        return values;
    }

    // The game's window's defaults; ours are the settings' own.
    Values defaultValues()
    {
        Values values;
        values.preferences = preferencesGetDefaults();
        values.allAnimations = EnhancementSettings().combat_speed_all_animations;
        values.mainMenuContinue = EnhancementSettings().main_menu_continue;
        values.worldmapFollow = EnhancementSettings().worldmap_follow_party;
        values.tacticalView = EnhancementSettings().tactical_view;
        values.saveCompatibility = EnhancementSettings().save_compatibility;
        values.interfaceSize = TouchSettings().hud_scale;
        values.mapFilter = WorldViewSettings().filter;
        QolSettings qol;
        values.autoOpenDoors = qol.auto_open_doors;
        values.walkWhenSneaking = qol.walk_when_sneaking;
        values.fastAmmoLoad = qol.fast_ammo_load;
        values.partyTradeFromMenu = qol.party_trade_from_menu;
        values.partyLootAndBarter = qol.party_loot_and_barter;
        UISettings ui;
        values.partyMemberExtraInfo = ui.party_member_extra_info;
        values.karmaChanges = ui.display_karma_changes;
        values.bonusDamage = ui.display_bonus_damage;
        values.quickSaves = kDefaultQuickSaves;
        return values;
    }

    // Lines for the confirmation of [values]' number of quick saves: quick
    // saves becoming permanent, the quick save button opening the save
    // screen; none - nothing to confirm.
    std::vector<std::string> quickSavesWarning(const Values& values)
    {
        std::vector<std::string> lines;
        int current = lsgQuickSaveCount();
        if (values.quickSaves == current) {
            return lines;
        }

        int over = lsgQuickSavesOverCount(values.quickSaves);
        char text[512];
        if (over > 0) {
            snprintf(text, sizeof(text), values.quickSaves == 0 ? muiText(kTextQuickSavesAll, "All quick saves (%d) become permanent and stay where they are in the list.") : muiText(kTextQuickSavesOldest, "The oldest quick saves (%d) become permanent: they stay where they are in the list but are no longer replaced by new ones."),
                over);
            lines.push_back(text);
        }
        if (values.quickSaves == 0 && current > 0) {
            lines.push_back(muiText(kTextQuickSavesButton, "The quick save button will open the save screen, like a manual save."));
        }
        return lines;
    }

    // Asks to apply [values] ([always] - even when nothing needs a warning,
    // leaving the screen); the quick saves' warning is in the question.
    bool confirmApply(const Values& values, bool always)
    {
        std::vector<std::string> lines = quickSavesWarning(values);
        if (lines.empty() && !always) {
            return true;
        }

        std::vector<const char*> body;
        for (const std::string& line : lines) {
            body.push_back(line.c_str());
        }
        return showDialogBox(muiText(kTextApplyTitle, "Apply the changes?"), body.empty() ? nullptr : body.data(), static_cast<int>(body.size()), 0, 0, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_YES_NO) != 0;
    }

    // Sets [values] as the game's window's Done does, then this port's and
    // CE's settings; all written to fallout2.cfg. Asked first
    // (`confirmApply`).
    void applyValues(const Values& values)
    {
        // The saves rearranged first: the setting is written only when they
        // are (`lsgSetQuickSaveCount`).
        if (values.quickSaves != lsgQuickSaveCount() && !lsgSetQuickSaveCount(values.quickSaves)) {
            soundPlayFile("iisxxxx1");
            const char* body[] = { muiText(kTextQuickSavesFailed, "Could not rearrange the quick saves, their number stays.") };
            showDialogBox(muiText(kTextQuickSaves, "Quick saves"), body, 1, 0, 0, COLOR_AMBER, nullptr, COLOR_AMBER, 0);
        }

        settings.enhancements.combat_speed_all_animations = values.allAnimations;
        settings.enhancements.main_menu_continue = values.mainMenuContinue;
        settings.enhancements.worldmap_follow_party = values.worldmapFollow;
        settings.enhancements.tactical_view = values.tacticalView;
        settings.enhancements.save_compatibility = values.saveCompatibility;
        settings.touch.hud_scale = values.interfaceSize;
        settings.world_view.filter = values.mapFilter;
        worldViewSetFilter(values.mapFilter);
        settings.qol.auto_open_doors = values.autoOpenDoors;
        settings.qol.walk_when_sneaking = values.walkWhenSneaking;
        settings.qol.fast_ammo_load = values.fastAmmoLoad;
        settings.qol.party_trade_from_menu = values.partyTradeFromMenu;
        settings.qol.party_loot_and_barter = values.partyLootAndBarter;
        settings.ui.party_member_extra_info = values.partyMemberExtraInfo;
        // Both read when shown (the karma message, the damage line), so they
        // apply at once.
        settings.ui.display_karma_changes = values.karmaChanges;
        settings.ui.display_bonus_damage = values.bonusDamage;

        // Writes the settings file too.
        preferencesApply(values.preferences);
    }

    enum class Section {
        Game,
        Combat,
        Sound,
        Display,
        // The game's files and saves (Android: the app's import screen):
        // actions, not settings - no apply or reset there.
        Files,
        // Version, notices, the source and the licenses.
        About,
        Count,
    };

    // What the "Game files" section's buttons do.
    enum class FilesAction {
        None,
        ReplaceGame,
        ImportSaves,
        ExportSaves,
        DeleteSaves,
    };

    // The section is there on Android (the app's import screen does the
    // work); elsewhere for automated tests only.
    bool gameFilesAvailable()
    {
#ifdef __ANDROID__
        return true;
#else
        return devAutotestIsEnabled();
#endif
    }

    // Opens the app's import screen ([mode]: ImportActivity's; null -
    // replacing the game, the app closes this game then: MainActivity).
    void openGameFiles(const char* mode)
    {
#ifdef __ANDROID__
        JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (env == nullptr || activity == nullptr) {
            return;
        }
        jclass activityClass = env->GetObjectClass(activity);
        jmethodID method = env->GetMethodID(activityClass, "openGameFiles", "(Ljava/lang/String;)V");
        if (method != nullptr) {
            jstring value = mode != nullptr ? env->NewStringUTF(mode) : nullptr;
            env->CallVoidMethod(activity, method, value);
            if (value != nullptr) {
                env->DeleteLocalRef(value);
            }
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(activityClass);
        env->DeleteLocalRef(activity);
#else
        debugPrint("game files: %s\n", mode != nullptr ? mode : "replace game");
#endif
    }

    enum class RowKind {
        Header,
        Choice,
        Toggle,
        Slider,
    };

    // Where a setting comes from: the game's window, Fallout 2 CE, this port.
    enum class Origin {
        Game,
        Ce,
        Port,
    };

    // Sample played when a volume slider is let go.
    enum class Sample {
        None,
        SoundEffect,
        Speech,
    };

    // A setting on the screen: its value as a number (choice index, 0/1,
    // slider value) read from and written to `Values`.
    struct Row {
        Section section;
        RowKind kind;
        int textId;
        const char* fallback;
        Origin origin = Origin::Game;
        int noteId = -1;
        const char* noteFallback = nullptr;
        // Choice: labels (OPTIONS.MSG, or ce.msg with `optionsFromCe`).
        std::vector<int> options;
        bool optionsFromCe = false;
        // Slider range and its value as text.
        float minValue = 0.0f;
        float maxValue = 1.0f;
        std::function<std::string(float)> format;
        Sample sample = Sample::None;
        std::function<float(const Values&)> get;
        std::function<void(Values&, float)> set;
    };

    std::string percentText(float value, float minValue, float maxValue)
    {
        char text[16];
        snprintf(text, sizeof(text), "%d%%", static_cast<int>(std::lround((value - minValue) * 100.0f / (maxValue - minValue))));
        return text;
    }

    std::vector<Row> makeRows()
    {
        std::vector<Row> rows;

        auto choice = [&](Section section, int textId, const char* fallback, std::vector<int> options, int PreferenceValues::*field) {
            Row row { section, RowKind::Choice, textId, fallback };
            row.options = options;
            row.get = [field](const Values& values) { return static_cast<float>(values.preferences.*field); };
            row.set = [field](Values& values, float value) { values.preferences.*field = static_cast<int>(value); };
            rows.push_back(row);
        };

        // Off / on of the game's window (OPTIONS.MSG labels in that order).
        auto preferenceToggle = [&](Section section, int textId, const char* fallback, int PreferenceValues::*field) {
            Row row { section, RowKind::Toggle, textId, fallback };
            row.get = [field](const Values& values) { return static_cast<float>(values.preferences.*field); };
            row.set = [field](Values& values, float value) { values.preferences.*field = value != 0.0f ? 1 : 0; };
            rows.push_back(row);
        };

        auto toggle = [&](Section section, int textId, const char* fallback, Origin origin, bool Values::*field) {
            Row row { section, RowKind::Toggle, textId, fallback, origin };
            row.get = [field](const Values& values) { return values.*field ? 1.0f : 0.0f; };
            row.set = [field](Values& values, float value) { values.*field = value != 0.0f; };
            rows.push_back(row);
        };

        auto volume = [&](int textId, const char* fallback, int PreferenceValues::*field, Sample sample) {
            Row row { Section::Sound, RowKind::Slider, textId, fallback };
            row.minValue = 0.0f;
            row.maxValue = static_cast<float>(VOLUME_MAX);
            row.format = [](float value) { return percentText(value, 0.0f, static_cast<float>(VOLUME_MAX)); };
            row.sample = sample;
            row.get = [field](const Values& values) { return static_cast<float>(values.preferences.*field); };
            row.set = [field](Values& values, float value) { values.preferences.*field = static_cast<int>(std::lround(value)); };
            rows.push_back(row);
        };

        auto header = [&](Section section, int textId, const char* fallback) {
            rows.push_back({ section, RowKind::Header, textId, fallback });
        };

        // Game.
        choice(Section::Game, kTextGameDifficulty, "Game difficulty", { kMessageEasy, kMessageNormal, kMessageHard }, &PreferenceValues::gameDifficulty);
        choice(Section::Game, kTextViolenceLevel, "Violence level", { kMessageNone, kMessageMinimal, kMessageNormal, kMessageMaximumBlood }, &PreferenceValues::violenceLevel);
        choice(Section::Game, kTextRunning, "Running", { kMessageNormal, kMessageAlways }, &PreferenceValues::running);
        preferenceToggle(Section::Game, kTextSubtitles, "Subtitles", &PreferenceValues::subtitles);
        preferenceToggle(Section::Game, kTextLanguageFilter, "Language filter", &PreferenceValues::languageFilter);
        {
            // The window's knob: slow (6 s) at the left, faster (1 s) right.
            Row row { Section::Game, RowKind::Slider, kTextTextDelay, "Text delay" };
            row.minValue = 1.0f;
            row.maxValue = 6.0f;
            row.format = [](float value) {
                char text[32];
                snprintf(text, sizeof(text), "%.1f %s", 7.0f - value, muiText(kTextSeconds, "s"));
                return std::string(text);
            };
            row.get = [](const Values& values) { return static_cast<float>(7.0 - values.preferences.textBaseDelay); };
            row.set = [](Values& values, float value) { values.preferences.textBaseDelay = std::round((7.0 - value) * 10.0) / 10.0; };
            rows.push_back(row);
        }
        header(Section::Game, kTextConvenience, "Convenience");
        {
            Row row { Section::Game, RowKind::Toggle, kTextMainMenuContinue, "Continue in the main menu", Origin::Port, kTextMainMenuContinueNote, "Loads the save made last" };
            row.get = [](const Values& values) { return values.mainMenuContinue ? 1.0f : 0.0f; };
            row.set = [](Values& values, float value) { values.mainMenuContinue = value != 0.0f; };
            rows.push_back(row);
        }
        {
            Row row { Section::Game, RowKind::Toggle, kTextSaveCompatibility, "Save compatibility", Origin::Port, kTextSaveCompatibilityNote, "Marks saves made with other game files or mods" };
            row.get = [](const Values& values) { return values.saveCompatibility ? 1.0f : 0.0f; };
            row.set = [](Values& values, float value) { values.saveCompatibility = value != 0.0f; };
            rows.push_back(row);
        }
        {
            // sfall's AutoQuickSave pages: 0-10 of 10 slots.
            Row row { Section::Game, RowKind::Slider, kTextQuickSaves, "Quick saves", Origin::Ce, kTextQuickSavesNote, "The oldest are replaced by new ones. Of 1000 places the rest are for manual saves" };
            row.minValue = 0.0f;
            row.maxValue = 100.0f;
            row.format = [](float value) {
                int count = static_cast<int>(std::lround(value));
                return count == 0 ? std::string(muiText(kTextQuickSavesOff, "Off")) : std::to_string(count);
            };
            row.get = [](const Values& values) { return static_cast<float>(values.quickSaves); };
            row.set = [](Values& values, float value) { values.quickSaves = static_cast<int>(std::lround(value / 10.0f)) * 10; };
            rows.push_back(row);
        }
        toggle(Section::Game, kTextAutoOpenDoors, "Open unlocked doors on the way", Origin::Ce, &Values::autoOpenDoors);
        toggle(Section::Game, kTextWalkWhenSneaking, "Walk instead of leaving sneak", Origin::Ce, &Values::walkWhenSneaking);
        toggle(Section::Game, kTextFastAmmoLoad, "Fast ammo loading", Origin::Ce, &Values::fastAmmoLoad);
        {
            Row row { Section::Game, RowKind::Toggle, kTextKarmaChanges, "Karma change messages", Origin::Ce, kTextKarmaChangesNote, "In the log: \"You gained 5 karma.\"" };
            row.get = [](const Values& values) { return values.karmaChanges ? 1.0f : 0.0f; };
            row.set = [](Values& values, float value) { values.karmaChanges = value != 0.0f; };
            rows.push_back(row);
        }
        header(Section::Game, kTextPartyMembers, "Party members");
        toggle(Section::Game, kTextPartyTradeFromMenu, "Trade from the action menu", Origin::Ce, &Values::partyTradeFromMenu);
        toggle(Section::Game, kTextPartyLootAndBarter, "Their items when looting and bartering", Origin::Ce, &Values::partyLootAndBarter);
        toggle(Section::Game, kTextPartyMemberExtraInfo, "Level, AC and addictions in control", Origin::Ce, &Values::partyMemberExtraInfo);

        // Combat.
        choice(Section::Combat, kTextCombatDifficulty, "Combat difficulty", { kMessageWimpy, kMessageNormal, kMessageRough }, &PreferenceValues::combatDifficulty);
        {
            Row row { Section::Combat, RowKind::Slider, kTextCombatSpeed, "Combat speed" };
            row.minValue = 0.0f;
            row.maxValue = 50.0f;
            row.format = [](float value) { return percentText(value, 0.0f, 50.0f); };
            row.get = [](const Values& values) { return static_cast<float>(values.preferences.combatSpeed); };
            row.set = [](Values& values, float value) { values.preferences.combatSpeed = static_cast<int>(std::lround(value)); };
            rows.push_back(row);
        }
        preferenceToggle(Section::Combat, kTextPlayerSpeedup, "Player speedup", &PreferenceValues::playerSpeedup);
        {
            Row row { Section::Combat, RowKind::Toggle, kTextAllAnimations, "All combat animations faster", Origin::Port, kTextAllAnimationsNote, "Running, attacks, hits and their sounds, up to 2x" };
            row.get = [](const Values& values) { return values.allAnimations ? 1.0f : 0.0f; };
            row.set = [](Values& values, float value) { values.allAnimations = value != 0.0f; };
            rows.push_back(row);
        }
        {
            Row row { Section::Combat, RowKind::Toggle, kTextTacticalView, "Tactical view", Origin::Port, kTextTacticalViewNote, "Combat button: see-through figures, tiles and where you can walk" };
            row.get = [](const Values& values) { return values.tacticalView ? 1.0f : 0.0f; };
            row.set = [](Values& values, float value) { values.tacticalView = value != 0.0f; };
            rows.push_back(row);
        }
        preferenceToggle(Section::Combat, kTextCombatLooks, "Combat looks", &PreferenceValues::combatLooks);
        choice(Section::Combat, kTextCombatMessages, "Combat messages", { kMessageVerbose, kMessageBrief }, &PreferenceValues::combatMessages);
        preferenceToggle(Section::Combat, kTextCombatTaunts, "Combat taunts", &PreferenceValues::combatTaunts);
        choice(Section::Combat, kTextTargetHighlight, "Target highlight", { kMessageOff, kMessageOn, kMessageTargetingOnly }, &PreferenceValues::targetHighlight);
        {
            Row row { Section::Combat, RowKind::Toggle, kTextBonusDamage, "Bonus damage in the stats", Origin::Ce, kTextBonusDamageNote, "Weapon damage with the bonus damage perks" };
            row.get = [](const Values& values) { return values.bonusDamage ? 1.0f : 0.0f; };
            row.set = [](Values& values, float value) { values.bonusDamage = value != 0.0f; };
            rows.push_back(row);
        }

        // Sound.
        volume(kTextMasterVolume, "Master volume", &PreferenceValues::masterVolume, Sample::SoundEffect);
        volume(kTextMusicVolume, "Music and movies", &PreferenceValues::musicVolume, Sample::None);
        volume(kTextSoundEffectsVolume, "Sound effects", &PreferenceValues::soundEffectsVolume, Sample::SoundEffect);
        volume(kTextSpeechVolume, "Speech", &PreferenceValues::speechVolume, Sample::Speech);

        // Display.
        {
            Row row { Section::Display, RowKind::Slider, kTextBrightness, "Brightness" };
            row.minValue = 1.0f;
            row.maxValue = 1.17999267578125f;
            row.format = [](float value) { return percentText(value, 1.0f, 1.17999267578125f); };
            row.get = [](const Values& values) { return static_cast<float>(values.preferences.brightness); };
            row.set = [](Values& values, float value) { values.preferences.brightness = value; };
            rows.push_back(row);
        }
        preferenceToggle(Section::Display, kTextItemHighlight, "Item highlight", &PreferenceValues::itemHighlight);
        {
            Row row { Section::Display, RowKind::Slider, kTextInterfaceSize, "Interface size", Origin::Port };
            row.minValue = static_cast<float>(kInterfaceSizeMin);
            row.maxValue = static_cast<float>(kInterfaceSizeMax);
            row.format = [](float value) {
                char text[16];
                snprintf(text, sizeof(text), "%d%%", static_cast<int>(std::lround(value)));
                return std::string(text);
            };
            row.get = [](const Values& values) { return static_cast<float>(values.interfaceSize); };
            row.set = [](Values& values, float value) { values.interfaceSize = static_cast<int>(std::lround(value / 5.0f) * 5); };
            rows.push_back(row);
        }
        {
            Row row { Section::Display, RowKind::Toggle, kTextWorldmapFollow, "Camera follows the party on the world map", Origin::Port, kTextWorldmapFollowNote, "Moving the map pauses it until the next destination" };
            row.get = [](const Values& values) { return values.worldmapFollow ? 1.0f : 0.0f; };
            row.set = [](Values& values, float value) { values.worldmapFollow = value != 0.0f; };
            rows.push_back(row);
        }
        {
            Row row { Section::Display, RowKind::Choice, kTextMapFilter, "Map smoothing", Origin::Port };
            row.options = { kTextFilterNearest, kTextFilterLinear, kTextFilterSharp };
            row.optionsFromCe = true;
            row.get = [](const Values& values) { return static_cast<float>(values.mapFilter); };
            row.set = [](Values& values, float value) { values.mapFilter = static_cast<int>(value); };
            rows.push_back(row);
        }

        return rows;
    }

    // Settings: sections at the left, the section's settings to their right
    // with their origin (port, CE), Reset all and Apply under them, Back at
    // the right edge. Changes are kept until Apply (volumes and brightness
    // heard and seen meanwhile); Back with changes asks to apply them.
    class PreferencesScreen : public MuiScreen {
    public:
        explicit PreferencesScreen(bool inGame);
        ~PreferencesScreen() override;

        bool coversScreen() override { return true; }
        void build(MuiContext& ui) override;
        void back() override;

        void runPending();

    private:
        std::vector<Row> rows;
        Values applied;
        Values edited;
        // Opened from the game, not the main menu: there's a map to preview.
        bool inGame;
        Section section = Section::Game;
        bool resetScroll = true;
        bool pendingLicenses = false;
        bool pendingUpdate = false;
        bool pendingDeleteUpdate = false;
        bool waitingUpdate = false;

        MessageList optionsMessages;
        bool optionsLoaded = false;

        // A volume slider held last frame (its sample plays when let go).
        int heldSlider = -1;

        // Map preview of the display section (brightness).
        std::vector<unsigned char> preview;
        int previewWidth = 0;
        int previewHeight = 0;
        bool previewValid = false;
        unsigned int previewVersion = 0;

        bool pendingReset = false;
        bool pendingApply = false;
        bool pendingBack = false;
        FilesAction pendingFiles = FilesAction::None;

        std::u32string optionLabel(const Row& row, int index);
        bool changed(const Row& row) const;
        bool anyChanged() const;
        void previewChanges(const Values& before);
        void capturePreview();

        void buildRows(MuiContext& ui, const MuiRect& rect);
        void buildFiles(MuiContext& ui, const MuiRect& rect);
        void buildAbout(MuiContext& ui, const MuiRect& rect);
        void runFilesAction(FilesAction action);
        void buildRow(MuiContext& ui, size_t index, const MuiRect& rect);
    };

    PreferencesScreen::PreferencesScreen(bool inGame)
        : inGame(inGame)
    {
        modal = true;
        rows = makeRows();
        applied = currentValues();
        edited = applied;

        if (messageListInit(&optionsMessages)) {
            char path[COMPAT_MAX_PATH];
            snprintf(path, sizeof(path), "%s%s", asc_5186C8, "options.msg");
            optionsLoaded = messageListLoad(&optionsMessages, path);
        }

        capturePreview();
    }

    PreferencesScreen::~PreferencesScreen()
    {
        messageListFree(&optionsMessages);
    }

    std::u32string PreferencesScreen::optionLabel(const Row& row, int index)
    {
        int id = row.options[index];
        if (row.optionsFromCe) {
            static const char* const kFallbacks[] = { "Pixels", "Smooth", "Sharp" };
            return muiDecodeGameText(muiText(id, kFallbacks[std::min(index, 2)]));
        }

        MessageListItem item;
        item.num = id;
        if (optionsLoaded && messageListGetItem(&optionsMessages, &item)) {
            return muiDecodeGameText(item.text);
        }
        return U"?";
    }

    bool PreferencesScreen::changed(const Row& row) const
    {
        return row.kind != RowKind::Header && row.get(edited) != row.get(applied);
    }

    bool PreferencesScreen::anyChanged() const
    {
        for (const Row& row : rows) {
            if (changed(row)) {
                return true;
            }
        }
        return false;
    }

    // Volumes and brightness are heard and seen while they change.
    void PreferencesScreen::previewChanges(const Values& before)
    {
        const PreferenceValues& a = before.preferences;
        const PreferenceValues& b = edited.preferences;
        if (a.masterVolume != b.masterVolume || a.musicVolume != b.musicVolume
            || a.soundEffectsVolume != b.soundEffectsVolume || a.speechVolume != b.speechVolume
            || a.brightness != b.brightness) {
            preferencesPreview(b);
            if (a.brightness != b.brightness) {
                capturePreview();
            }
        }
    }

    void PreferencesScreen::capturePreview()
    {
        previewValid = inGame && worldViewCaptureView(&preview, &previewWidth, &previewHeight, 480);
        previewVersion++;
    }

    void PreferencesScreen::back()
    {
        if (anyChanged()) {
            pendingBack = true;
        } else {
            finished = true;
        }
    }

    void PreferencesScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        muiFillRect(ui.screenRect(), kBackground);

        MuiRect content;
        MuiRect rail;
        muiDialogLayout(ui, &content, &rail);

        if (muiGameScreenTabs(ui, rail, "prefs", MuiGameScreenTab::None, false) == MuiGameScreenTab::Back) {
            back();
        }

        float gap = ui.dp(8.0f);
        float listWidth = std::max(ui.dp(130.0f), content.w * 0.22f);
        MuiRect list = { content.x, content.y, listWidth, content.h };
        MuiRect panel = { list.right() + gap, content.y, content.right() - list.right() - gap, content.h };

        // Sections, a dot where something changed.
        static const struct {
            int textId;
            const char* fallback;
        } kSections[] = {
            { kTextSectionGame, "Game" },
            { kTextSectionCombat, "Combat" },
            { kTextSectionSound, "Sound" },
            { kTextSectionDisplay, "Display" },
        };
        std::vector<std::u32string> labels;
        std::vector<Section> shown;
        for (const auto& entry : kSections) {
            labels.push_back(muiDecodeGameText(muiText(entry.textId, entry.fallback)));
            shown.push_back(static_cast<Section>(shown.size()));
        }
        if (gameFilesAvailable()) {
            labels.push_back(muiDecodeGameText(muiText(kTextSectionFiles, "Game files")));
            shown.push_back(Section::Files);
        }
        labels.push_back(muiDecodeGameText(muiText(kTextSectionAbout, "About")));
        shown.push_back(Section::About);

        std::vector<bool> marked(shown.size(), false);
        for (const Row& row : rows) {
            if (changed(row)) {
                marked[static_cast<size_t>(row.section)] = true;
            }
        }
        // The same quiet marker as changed settings, with separate state:
        // an update never makes settings dirty or asks to apply changes.
        if (appUpdateHasRelease()) {
            size_t index = std::find(shown.begin(), shown.end(), Section::About) - shown.begin();
            marked[index] = true;
            ui.mark("prefs.about.notice", list);
        }

        muiFillRoundRect(list, ui.dp(7.0f), kPanel);
        int current = static_cast<int>(std::find(shown.begin(), shown.end(), section) - shown.begin());
        int chosen = muiSectionList(ui, "prefs.sections", list, labels, current, marked);
        if (chosen != current && chosen >= 0 && chosen < static_cast<int>(shown.size())) {
            section = shown[chosen];
            resetScroll = true;
            if (section == Section::Display) {
                capturePreview();
            }
        }

        // Settings over the buttons.
        muiFillRoundRect(panel, ui.dp(7.0f), kPanel);
        muiStrokeRoundRect(panel, ui.dp(7.0f), ui.dp(1.0f), theme.panelBorder);
        MuiRect inner = panel.inset(ui.dp(8.0f));
        if (section == Section::Files) {
            buildFiles(ui, inner);
            return;
        }
        if (section == Section::About) {
            buildAbout(ui, inner);
            return;
        }
        float buttonHeight = ui.dp(40.0f);
        MuiRect buttons = { inner.x, inner.bottom() - buttonHeight, inner.w, buttonHeight };
        MuiRect rowsRect = { inner.x, inner.y, inner.w, buttons.y - gap - inner.y };
        buildRows(ui, rowsRect);

        float buttonGap = ui.dp(8.0f);
        MuiRect reset = { buttons.x, buttons.y, (buttons.w - buttonGap) / 2.0f, buttons.h };
        MuiRect apply = { reset.right() + buttonGap, buttons.y, reset.w, buttons.h };
        if (ui.button("prefs.reset", reset, muiDecodeGameText(muiText(kTextResetAll, "Reset all")))) {
            pendingReset = true;
        }

        bool dirty = anyChanged();
        if (ui.button("prefs.apply", apply, muiDecodeGameText(muiText(kTextApply, "Apply")), dirty ? MuiButtonStyle::Primary : MuiButtonStyle::Normal) && dirty) {
            _gsound_red_butt_press(-1, 0);
            pendingApply = true;
        }
    }

    // The game's files and saves, laid out as the settings' rows: what is
    // installed as a header, then each action - its name and what it does at
    // the left, its button at the right.
    // What this build is, that it isn't official, where its source is, and
    // the licenses in full (scrolling).
    void PreferencesScreen::buildAbout(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        auto label = [](int id, const char* fallback) { return muiDecodeGameText(muiText(id, fallback)); };
        float gap = ui.dp(10.0f);
        float size = ui.dp(12.0f);
        float line = muiLineHeight(size);
        float rowHeight = ui.dp(48.0f);
        bool updates = appUpdateState() != AppUpdateState::Unsupported;
        bool deleting = updates && !inGame && appUpdateReady() && appUpdateState() != AppUpdateState::Checking && appUpdateState() != AppUpdateState::Installing;
        auto notice = muiWrapText(label(kTextAboutNotice, "An unofficial mobile version of Fallout 2 Community Edition: CE's engine changed for touch screens. Not affiliated with or endorsed by Microsoft, Bethesda Softworks, ZeniMax or Interplay. Needs your own copy of Fallout 2."), rect.w - ui.dp(8.0f), size);
        auto thanksNote = muiWrapText(label(444, "The app, its updates and all of its features are free. A thank-you goes to the port's author personally: it is not a payment for Fallout 2 or for the app, and it has nothing to do with Microsoft, Bethesda Softworks, ZeniMax, Interplay or any other owner of Fallout. It gives no access to builds or features, no priority for fixes and no commitment to development."), rect.w - ui.dp(8.0f), size);
        float titleHeight = ui.dp(24.0f);
        float noticeHeight = notice.size() * line;
        float updateNote = updates && inGame && appUpdateHasRelease() ? line * 2.0f : 0.0f;
        float height = titleHeight + line + gap + noticeHeight + gap
            + (updates ? rowHeight * (deleting ? 3.0f : 2.0f) + line + updateNote + gap : 0.0f) + rowHeight * 6.0f
            + thanksNote.size() * line + gap;
        if (resetScroll) {
            ui.setScroll("prefs.about", 0.0f);
            resetScroll = false;
        }
        float offset = ui.scroll("prefs.about", rect, height);
        muiPushClip(rect);
        float y = rect.y - offset;
        muiDrawTextAligned(U"Fallout 2 CE Mobile", { rect.x + ui.dp(4.0f), y, rect.w, titleHeight }, ui.dp(16.0f), theme.text, MuiAlign::Start, MuiAlign::Center);
        y += titleHeight;
        char version[160];
        snprintf(version, sizeof(version), muiText(kTextAboutVersion, "Version %s, build %s"), FALLOUT_APP_VERSION, _BUILD_HASH);
        muiDrawTextAligned(muiDecodeGameText(version), { rect.x + ui.dp(4.0f), y, rect.w, line }, size, theme.textDim, MuiAlign::Start, MuiAlign::Center);
        y += line + gap;
        for (const auto& value : notice) {
            muiDrawTextAligned(value, { rect.x + ui.dp(4.0f), y, rect.w - ui.dp(8.0f), line }, size, theme.textDim, MuiAlign::Start, MuiAlign::Center);
            y += line;
        }
        y += gap;

        auto visible = [&](const MuiRect& row) { return row.bottom() > rect.y && row.y < rect.bottom(); };
        auto separator = [&]() {
            muiFillRect({ rect.x, y - ui.dp(1.0f), rect.w, ui.dp(1.0f) }, kRowLine);
        };
        auto actionRow = [&](const char* id, const std::u32string& title, const std::u32string& verb, MuiButtonStyle style = MuiButtonStyle::Normal, bool marked = false) {
            MuiRect row = { rect.x, y, rect.w, rowHeight };
            y += rowHeight;
            if (!visible(row)) return false;
            float buttonWidth = std::min(ui.dp(150.0f), row.w * 0.42f);
            MuiRect button = { row.right() - buttonWidth, row.y + ui.dp(7.0f), buttonWidth, row.h - ui.dp(14.0f) };
            MuiRect name = { row.x + ui.dp(4.0f), row.y, button.x - row.x - ui.dp(12.0f), row.h };
            if (marked) {
                float dot = ui.dp(4.0f);
                muiFillCircle(name.x + dot, name.centerY(), dot, kChanged);
                ui.mark("prefs.about.update.notice", { name.x, name.centerY() - dot, dot * 2.0f, dot * 2.0f });
                float inset = dot * 2.0f + ui.dp(8.0f);
                name.x += inset;
                name.w -= inset;
            }
            auto lines = muiWrapText(title, name.w, size);
            float top = name.centerY() - std::min(lines.size(), static_cast<size_t>(2)) * line / 2.0f;
            for (size_t index = 0; index < std::min(lines.size(), static_cast<size_t>(2)); index++) {
                muiDrawTextAligned(lines[index], { name.x, top + index * line, name.w, line }, size, theme.text, MuiAlign::Start, MuiAlign::Center);
            }
            return ui.button(id, button, verb, style);
        };

        if (updates) {
            bool available = appUpdateHasRelease();
            bool busy = appUpdateState() == AppUpdateState::Checking || appUpdateState() == AppUpdateState::Installing;
            std::u32string verb = !inGame && available ? (appUpdateReady() ? label(408, "Install") : label(407, "Update")) : label(411, "Check");
            if (actionRow("prefs.about.update", muiUpdateStatus(), verb, available && !inGame ? MuiButtonStyle::Primary : MuiButtonStyle::Normal, available) && !busy) pendingUpdate = true;
            if (deleting && actionRow("prefs.about.update.delete", label(426, "Delete downloaded update"), label(kTextDeleteButton, "Delete"), MuiButtonStyle::Danger)) pendingDeleteUpdate = true;
            if (updateNote > 0.0f) {
                MuiRect hint = { rect.x + ui.dp(4.0f), y, rect.w - ui.dp(8.0f), updateNote };
                auto lines = muiWrapText(label(428, "Download and install updates from the main menu."), hint.w, size);
                for (const auto& value : lines) {
                    muiDrawTextAligned(value, { hint.x, y, hint.w, line }, size, theme.textDim, MuiAlign::Start, MuiAlign::Center);
                    y += line;
                }
                y = hint.bottom();
                ui.mark("prefs.about.update.ingame", hint);
            }
            separator();

            MuiRect toggle = { rect.x, y, rect.w, rowHeight };
            y += rowHeight;
            if (visible(toggle)) {
                MuiRect control = { toggle.right() - ui.dp(52.0f), toggle.y + ui.dp(10.0f), ui.dp(52.0f), toggle.h - ui.dp(20.0f) };
                auto name = label(424, "Check for updates");
                float badgeWidth = ui.dp(38.0f);
                MuiRect nameRect = { toggle.x + ui.dp(4.0f), toggle.y, control.x - toggle.x - badgeWidth - ui.dp(16.0f), toggle.h };
                float nameSize = muiFitTextSize(name, nameRect.w, ui.dp(14.0f), ui.dp(10.0f));
                muiDrawTextAligned(name, nameRect, nameSize, theme.text, MuiAlign::Start, MuiAlign::Center);
                MuiRect badge = { nameRect.x + muiTextWidth(name, nameSize) + ui.dp(6.0f), toggle.centerY() - ui.dp(9.0f), badgeWidth, ui.dp(18.0f) };
                muiStrokeRoundRect(badge, ui.dp(4.0f), ui.dp(1.0f), kPortBadge);
                muiDrawTextAligned(label(kTextPortBadge, "port"), badge, ui.dp(10.0f), kPortBadge, MuiAlign::Center, MuiAlign::Center);
                bool enabled = settings.touch.update_check;
                if (ui.toggle("prefs.about.update.auto", control, &enabled)) {
                    bool previous = settings.touch.update_check;
                    settings.touch.update_check = enabled;
                    if (!settingsSave()) {
                        settings.touch.update_check = previous;
                        muiNotify(muiText(433, "Could not save this setting."), MuiNoticeKind::Warning);
                    }
                }
            }
            muiDrawTextAligned(label(425, "At startup, at most once an hour"), { rect.x + ui.dp(4.0f), y, rect.w, line }, size, theme.textDim, MuiAlign::Start, MuiAlign::Center);
            y += line + gap;
            separator();
        }
        // The author's pages, then the source; the thanks with what it is not.
        if (actionRow("prefs.about.telegram", label(440, "Developer news on Telegram"), label(438, "Open"))) SDL_OpenURL(kPortTelegramUrl);
        separator();
        if (actionRow("prefs.about.youtube", label(441, "Videos and guides on YouTube"), label(438, "Open"))) SDL_OpenURL(kPortYouTubeUrl);
        separator();
        if (actionRow("prefs.about.issues", label(442, "Report a problem"), label(438, "Open"))) SDL_OpenURL(kPortIssuesUrl);
        separator();
        if (actionRow("prefs.about.source", label(437, "Source code"), label(438, "Open"))) SDL_OpenURL(kPortSourceUrl);
        separator();
        if (actionRow("prefs.about.thanks", label(443, "Thank the port's author"), U"Boosty")) SDL_OpenURL(kPortThanksUrl);
        for (const auto& value : thanksNote) {
            muiDrawTextAligned(value, { rect.x + ui.dp(4.0f), y, rect.w - ui.dp(8.0f), line }, size, theme.textDim, MuiAlign::Start, MuiAlign::Center);
            y += line;
        }
        y += gap;
        separator();
        if (actionRow("prefs.about.licenses", label(kTextAboutLicenses, "Licenses"), label(435, "View"))) pendingLicenses = true;
        separator();
        muiPopClip();
    }

    void PreferencesScreen::buildFiles(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();

        int mods = 0;
        for (const XBase* base = xbaseGetFirst(); base != nullptr; base = base->next) {
            if (base->isDbase && compat_strnicmp(base->path, "mods", 4) == 0 && (base->path[4] == '\\' || base->path[4] == '/')) {
                mods++;
            }
        }
        int saves = static_cast<int>(lsgAllSaveDatPaths().size());
        char installed[128];
        snprintf(installed, sizeof(installed), muiText(kTextFilesInstalled, "Mods: %d - saves: %d"), mods, saves);

        struct Action {
            FilesAction action;
            const char* id;
            int textId;
            const char* fallback;
            int noteId;
            const char* noteFallback;
            int buttonId;
            const char* buttonFallback;
        };
        static const Action kActions[] = {
            { FilesAction::ReplaceGame, "prefs.files.replace", kTextReplaceGame, "Replace game", kTextReplaceGameNote, "New build or new mods; saves and settings stay", kTextReplaceButton, "Replace" },
            { FilesAction::ImportSaves, "prefs.files.import", kTextImportSaves, "Import saves", kTextImportSavesNote, "From a folder or an archive: add or replace all", kTextImportButton, "Import" },
            { FilesAction::ExportSaves, "prefs.files.export", kTextExportSaves, "Export saves", kTextExportSavesNote, "All saves in one archive", kTextExportButton, "Export" },
            { FilesAction::DeleteSaves, "prefs.files.delete", kTextDeleteSaves, "Delete all saves", kTextDeleteSavesNote, "Export them first if they may be needed", kTextDeleteButton, "Delete" },
        };
        constexpr int kActionCount = sizeof(kActions) / sizeof(kActions[0]);

        // As the settings' rows (buildRows, buildRow).
        float rowHeight = ui.dp(50.0f);
        float headerHeight = ui.dp(30.0f);
        if (resetScroll) {
            ui.setScroll("prefs.files.rows", 0.0f);
            resetScroll = false;
        }
        float offset = ui.scroll("prefs.files.rows", rect, headerHeight + rowHeight * kActionCount);
        muiPushClip(rect);

        float y = rect.y - offset;
        muiDrawTextAligned(muiDecodeGameText(installed), { rect.x + ui.dp(4.0f), y + ui.dp(8.0f), rect.w, headerHeight - ui.dp(8.0f) }, ui.dp(12.0f), theme.textDim, MuiAlign::Start, MuiAlign::Center);
        y += headerHeight;

        for (const Action& action : kActions) {
            MuiRect row = { rect.x, y, rect.w, rowHeight };
            y += rowHeight;
            muiFillRect({ row.x, row.bottom() - ui.dp(1.0f), row.w, ui.dp(1.0f) }, kRowLine);

            MuiRect button = { row.right() - ui.dp(130.0f), row.y + ui.dp(9.0f), ui.dp(130.0f), row.h - ui.dp(18.0f) };
            float x = row.x + ui.dp(4.0f);
            float labelWidth = button.x - x - ui.dp(12.0f);

            std::u32string label = muiDecodeGameText(muiText(action.textId, action.fallback));
            MuiRect labelRect = { x, row.y + ui.dp(4.0f), labelWidth, ui.dp(24.0f) };
            muiDrawTextAligned(label, labelRect, muiFitTextSize(label, labelWidth, ui.dp(14.0f), ui.dp(11.0f)), theme.text, MuiAlign::Start, MuiAlign::Center);
            std::u32string note = muiDecodeGameText(muiText(action.noteId, action.noteFallback));
            MuiRect noteRect = { x, labelRect.bottom(), labelWidth, row.bottom() - labelRect.bottom() - ui.dp(4.0f) };
            muiDrawTextAligned(note, noteRect, muiFitTextSize(note, noteRect.w, ui.dp(11.0f), ui.dp(9.0f)), theme.textDim, MuiAlign::Start, MuiAlign::Center);

            std::u32string verb = muiDecodeGameText(muiText(action.buttonId, action.buttonFallback));
            if (ui.button(action.id, button, verb, action.action == FilesAction::DeleteSaves ? MuiButtonStyle::Danger : MuiButtonStyle::Normal)) {
                pendingFiles = action.action;
            }
        }

        muiPopClip();
    }

    // Replacing the game (the game closes first, said) and the saves'
    // import and export (over the game) are done by the app's screen;
    // deleting all saves here.
    void PreferencesScreen::runFilesAction(FilesAction action)
    {
        if (action == FilesAction::DeleteSaves) {
            std::vector<std::string> saves = lsgAllSaveDatPaths();
            if (saves.empty()) {
                return;
            }
            char body[512];
            snprintf(body, sizeof(body), muiText(kTextDeleteSavesBody, "All saves (%d) will be deleted permanently."), static_cast<int>(saves.size()));
            const char* lines[] = { body };
            if (showDialogBox(muiText(kTextDeleteSavesTitle, "Delete all saves?"), lines, 1, 0, 0, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_YES_NO) == 0) {
                return;
            }
            bool all = true;
            lsgMobileRefreshSlots();
            for (int slot = 0; slot < lsgGetTotalSlotCount(); slot++) {
                MobileSaveSlotInfo info;
                if (lsgMobileGetSlotInfo(slot, &info) && info.state != MobileSaveSlotState::Empty && !lsgMobileDeleteSlot(slot)) {
                    all = false;
                }
            }
            if (!all) {
                const char* failure[] = { muiText(kTextDeleteSavesFailed, "Not all saves could be deleted.") };
                showDialogBox(muiText(kTextDeleteSavesTitle, "Delete all saves?"), failure, 1, 0, 0, COLOR_AMBER, nullptr, COLOR_AMBER, 0);
            }
            return;
        }

        // The saves' import and export: the app's screen over the game (it
        // waits behind, paused; reads the saves again when back).
        if (action == FilesAction::ImportSaves) {
            openGameFiles("saves_import");
            return;
        }
        if (action == FilesAction::ExportSaves) {
            openGameFiles("saves_export");
            return;
        }

        // Replacing the game: its files can't change under it - it closes
        // (said first) and starts again after.
        const char* body[] = { inGame ? muiText(kTextCloseGameInGame, "The game closes for this and opens again after. Progress since the last save will be lost.")
                                      : muiText(kTextCloseGameMenu, "The game closes for this and opens again after.") };
        if (showDialogBox(muiText(kTextCloseGameTitle, "Close the game?"), body, 1, 0, 0, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_YES_NO) == 0) {
            return;
        }
        // Settings changed here are kept.
        if (anyChanged() && confirmApply(edited, false)) {
            applyValues(edited);
        }
        openGameFiles(nullptr);
    }

    void PreferencesScreen::buildRows(MuiContext& ui, const MuiRect& rect)
    {
        float rowHeight = ui.dp(50.0f);
        float headerHeight = ui.dp(30.0f);
        bool showPreview = section == Section::Display && previewValid;
        float previewSpace = showPreview ? std::min(rect.h * 0.45f, ui.dp(130.0f)) + ui.dp(8.0f) : 0.0f;

        float contentHeight = previewSpace;
        for (const Row& row : rows) {
            if (row.section == section) {
                contentHeight += row.kind == RowKind::Header ? headerHeight : rowHeight;
            }
        }

        if (resetScroll) {
            ui.setScroll("prefs.rows", 0.0f);
            resetScroll = false;
        }
        float offset = ui.scroll("prefs.rows", rect, contentHeight);

        muiPushClip(rect);

        float y = rect.y - offset;
        if (showPreview) {
            // The map with the brightness being chosen.
            MuiRect frame = { rect.x, y, rect.w, previewSpace - ui.dp(8.0f) };
            SDL_Texture* texture = muiRgbaTexture("prefs.preview", preview.data(), previewWidth, previewHeight, previewVersion);
            if (texture != nullptr) {
                muiDrawTexture(texture, muiFitRect(frame, static_cast<float>(previewWidth), static_cast<float>(previewHeight)));
            }
            y += previewSpace;
        }

        for (size_t index = 0; index < rows.size(); index++) {
            const Row& row = rows[index];
            if (row.section != section) {
                continue;
            }

            float height = row.kind == RowKind::Header ? headerHeight : rowHeight;
            MuiRect rowRect = { rect.x, y, rect.w, height };
            if (rowRect.bottom() >= rect.y && rowRect.y <= rect.bottom()) {
                buildRow(ui, index, rowRect);
            }
            y += height;
        }

        muiPopClip();
    }

    void PreferencesScreen::buildRow(MuiContext& ui, size_t index, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        Row& row = rows[index];
        std::u32string label = muiDecodeGameText(muiText(row.textId, row.fallback));

        if (row.kind == RowKind::Header) {
            muiDrawTextAligned(label, { rect.x + ui.dp(4.0f), rect.y + ui.dp(8.0f), rect.w, rect.h - ui.dp(8.0f) }, ui.dp(12.0f), theme.textDim, MuiAlign::Start, MuiAlign::Center);
            return;
        }

        muiFillRect({ rect.x, rect.bottom() - ui.dp(1.0f), rect.w, ui.dp(1.0f) }, kRowLine);

        // Label: a dot when changed, the origin badge, a note under it.
        // Choices get more room (several labels side by side).
        float controlWidth = row.kind == RowKind::Choice ? std::min(rect.w * 0.6f, ui.dp(400.0f)) : std::min(rect.w * 0.48f, ui.dp(300.0f));
        MuiRect control = { rect.right() - controlWidth, rect.y + ui.dp(8.0f), controlWidth, rect.h - ui.dp(16.0f) };
        float x = rect.x + ui.dp(4.0f);
        float labelWidth = control.x - x - ui.dp(12.0f);

        bool hasNote = row.noteId != -1;
        float labelTop = rect.y + ui.dp(4.0f);
        float labelHeight = hasNote ? ui.dp(24.0f) : rect.h - ui.dp(8.0f);
        if (changed(row)) {
            float dot = ui.dp(4.0f);
            muiFillCircle(x + dot, labelTop + labelHeight / 2.0f, dot, kChanged);
            x += dot * 2.0f + ui.dp(8.0f);
            labelWidth -= dot * 2.0f + ui.dp(8.0f);
        }

        MuiRect labelRect = { x, labelTop, labelWidth, labelHeight };
        std::u32string badge;
        MuiColor badgeColor = theme.textDim;
        if (row.origin == Origin::Port) {
            badge = muiDecodeGameText(muiText(kTextPortBadge, "port"));
            badgeColor = kPortBadge;
        } else if (row.origin == Origin::Ce) {
            badge = U"CE";
        }

        float badgeSize = ui.dp(10.0f);
        float badgeWidth = badge.empty() ? 0.0f : muiTextWidth(badge, badgeSize) + ui.dp(10.0f);
        float labelSize = muiFitTextSize(label, labelRect.w - (badge.empty() ? 0.0f : badgeWidth + ui.dp(6.0f)), ui.dp(14.0f), ui.dp(11.0f));
        muiDrawTextAligned(label, labelRect, labelSize, theme.text, MuiAlign::Start, MuiAlign::Center);

        if (!badge.empty()) {
            float textWidth = muiTextWidth(label, labelSize);
            MuiRect badgeRect = { labelRect.x + textWidth + ui.dp(6.0f), labelRect.centerY() - ui.dp(9.0f), badgeWidth, ui.dp(18.0f) };
            muiStrokeRoundRect(badgeRect, ui.dp(4.0f), ui.dp(1.0f), badgeColor);
            muiDrawTextAligned(badge, badgeRect, badgeSize, badgeColor, MuiAlign::Center, MuiAlign::Center);
        }

        if (hasNote) {
            std::u32string note = muiDecodeGameText(muiText(row.noteId, row.noteFallback));
            MuiRect noteRect = { x, labelRect.bottom(), labelWidth, rect.bottom() - labelRect.bottom() - ui.dp(4.0f) };
            muiDrawTextAligned(note, noteRect, muiFitTextSize(note, noteRect.w, ui.dp(11.0f), ui.dp(9.0f)), theme.textDim, MuiAlign::Start, MuiAlign::Center);
        }

        // By its name's text: stays when rows move (automated tests).
        std::string id = "prefs.rows." + std::to_string(row.textId);
        Values before = edited;
        float value = row.get(edited);

        switch (row.kind) {
        case RowKind::Choice: {
            std::vector<std::u32string> options;
            for (size_t option = 0; option < row.options.size(); option++) {
                options.push_back(optionLabel(row, static_cast<int>(option)));
            }
            int selected = static_cast<int>(value);
            if (ui.segmented(id, control, options, &selected)) {
                _gsound_red_butt_press(-1, 0);
                row.set(edited, static_cast<float>(selected));
            }
            break;
        }
        case RowKind::Toggle: {
            bool on = value != 0.0f;
            MuiRect switchRect = { control.right() - ui.dp(52.0f), control.y, ui.dp(52.0f), control.h };
            if (ui.toggle(id, switchRect, &on)) {
                _gsound_red_butt_press(-1, 0);
                row.set(edited, on ? 1.0f : 0.0f);
            }
            break;
        }
        case RowKind::Slider: {
            float valueWidth = ui.dp(56.0f);
            MuiRect track = { control.x + ui.dp(10.0f), control.y, control.w - valueWidth - ui.dp(20.0f), control.h };
            bool dragging = false;
            float newValue = ui.slider(id, track, value, row.minValue, row.maxValue, &dragging);
            if (newValue != value) {
                row.set(edited, newValue);
            }

            // Numbers and the game's texts (its code page).
            std::u32string text = muiDecodeGameText(row.format(row.get(edited)).c_str());
            muiDrawTextAligned(text, { control.right() - valueWidth, control.y, valueWidth, control.h }, ui.dp(12.0f), theme.textDim, MuiAlign::End, MuiAlign::Center);

            // The window's samples once the finger lets go.
            if (dragging) {
                heldSlider = static_cast<int>(index);
            } else if (heldSlider == static_cast<int>(index)) {
                heldSlider = -1;
                if (row.sample == Sample::SoundEffect) {
                    preferencesPlaySoundEffectSample();
                } else if (row.sample == Sample::Speech) {
                    preferencesPlaySpeechSample();
                }
            }
            break;
        }
        case RowKind::Header:
            break;
        }

        previewChanges(before);
    }

    void PreferencesScreen::runPending()
    {
        if (pendingDeleteUpdate) {
            pendingDeleteUpdate = false;
            appUpdateDelete();
        }
        if (pendingLicenses) {
            pendingLicenses = false;
            muiLicensesShow();
        }
        if (pendingUpdate) {
            pendingUpdate = false;
            if (!inGame && appUpdateHasRelease()) {
                muiUpdateShow();
            } else {
                appUpdateCheck(true);
                waitingUpdate = true;
            }
        }
        if (waitingUpdate && appUpdateState() != AppUpdateState::Checking) {
            waitingUpdate = false;
            if (!inGame && appUpdateHasRelease()) muiUpdateShow();
        }
        if (pendingFiles != FilesAction::None) {
            FilesAction action = pendingFiles;
            pendingFiles = FilesAction::None;
            runFilesAction(action);
        }

        if (pendingReset) {
            pendingReset = false;
            const char* body[] = { muiText(kTextResetBody, "Every setting goes back to its default.") };
            if (showDialogBox(muiText(kTextResetTitle, "Reset all settings?"), body, 1, 0, 0, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_YES_NO) != 0) {
                Values before = edited;
                edited = defaultValues();
                previewChanges(before);
            }
        }

        if (pendingApply) {
            pendingApply = false;
            if (confirmApply(edited, false)) {
                applyValues(edited);
                applied = currentValues();
                edited = applied;
            }
        }

        if (pendingBack) {
            pendingBack = false;
            if (confirmApply(edited, true)) {
                applyValues(edited);
            } else {
                // Volumes and brightness as they were.
                preferencesPreview(applied.preferences);
            }
            finished = true;
        }
    }

} // namespace

void muiPreferencesScreenRun(bool inGame)
{
    PreferencesScreen screen(inGame);
    muiPush(&screen);

    while (!screen.finished) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        if (keyCode == KEY_ESCAPE) {
            screen.back();
        }

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            screen.finished = true;
        }

        devAutotestTick();
        renderPresent();

        screen.runPending();

        sharedFpsLimiter.throttle();
    }

    muiRemove(&screen);
}

} // namespace fallout
