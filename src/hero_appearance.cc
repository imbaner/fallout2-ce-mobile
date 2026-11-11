#include "hero_appearance.h"

#include <stdio.h>

#include <algorithm>
#include <map>
#include <tuple>

#include "art.h"
#include "config.h"
#include "content_config.h"
#include "critter.h"
#include "debug.h"
#include "dfile.h"
#include "game_config.h"
#include "message.h"
#include "object.h"
#include "platform_compat.h"
#include "proto.h"
#include "settings.h"
#include "sfall_config.h"
#include "sfall_global_vars.h"
#include "stat.h"
#include "tile.h"
#include "xfile.h"

namespace fallout {

namespace {

    // sfall's limit for the numbers read from a .gcd file.
    constexpr int kMaxSetNumber = 100;

    // A set's folder and/or .dat (sfall looks in the folder first).
    struct HeroSet {
        std::string folder;
        DBase* dbase = nullptr;
    };

    struct HeroTexts {
        std::string name;
        std::string description;
    };

    int gHeroAppearanceMode = 0;
    int gHeroRace = 0;
    int gHeroStyle = 0;

    // The open sets: the chosen style's and its race's style 0.
    HeroSet gHeroStyleSet;
    HeroSet gHeroRaceSet;

    // Read once: the screens ask every frame. Keys: gender letter, race,
    // style, the style's text (else the race's).
    std::map<std::tuple<char, int, int, bool>, HeroTexts> gHeroTexts;

    // Whether a set is there, looked up once (the screens count the races
    // and styles every frame). Keys: gender letter, race, style.
    std::map<std::tuple<char, int, int>, bool> gHeroSetsFound;

    char genderLetter(int gender)
    {
        return gender == GENDER_FEMALE ? 'F' : 'M';
    }

    char dudeGenderLetter()
    {
        return genderLetter(critterGetStat(gDude, STAT_GENDER));
    }

    std::string setPath(char gender, int race, int style)
    {
        char path[COMPAT_MAX_PATH];
        snprintf(path, sizeof(path), "Appearance\\h%cR%02dS%02d", gender, race, style);
        return path;
    }

    bool setExists(char gender, int race, int style)
    {
        auto key = std::make_tuple(gender, race, style);
        auto it = gHeroSetsFound.find(key);
        if (it == gHeroSetsFound.end()) {
            std::string path = setPath(gender, race, style);
            bool found = compat_access(path.c_str(), 0) == 0 || compat_access((path + ".dat").c_str(), 0) == 0;
            it = gHeroSetsFound.emplace(key, found).first;
        }
        return it->second;
    }

    void setClose(HeroSet* set)
    {
        if (set->dbase != nullptr) {
            dbaseClose(set->dbase);
        }
        set->dbase = nullptr;
        set->folder.clear();
    }

    bool setOpen(HeroSet* set, char gender, int race, int style)
    {
        setClose(set);

        std::string path = setPath(gender, race, style);
        if (compat_access(path.c_str(), 0) == 0) {
            set->folder = path;
        }

        std::string datPath = path + ".dat";
        if (compat_access(datPath.c_str(), 0) == 0) {
            set->dbase = dbaseOpen(datPath.c_str());
            if (set->dbase == nullptr) {
                debugPrint("\nHero Appearance: can't open %s", datPath.c_str());
            }
        }

        return !set->folder.empty() || set->dbase != nullptr;
    }

    File* setOpenFile(const HeroSet& set, const char* path, const char* mode)
    {
        File* stream = nullptr;
        if (!set.folder.empty()) {
            stream = xfileOpenIn(nullptr, set.folder.c_str(), path, mode);
        }
        if (stream == nullptr && set.dbase != nullptr) {
            stream = xfileOpenIn(set.dbase, nullptr, path, mode);
        }
        return stream;
    }

    // The sets of [race] and [style] of [gender] become the open ones: false
    // (nothing changed) when the style's set isn't there, the game's own look
    // (race 0, style 0) excepted.
    bool loadSets(char gender, int race, int style)
    {
        HeroSet styleSet;
        if (!setOpen(&styleSet, gender, race, style) && (race != 0 || style != 0)) {
            return false;
        }

        HeroSet raceSet;
        if (style != 0) {
            setOpen(&raceSet, gender, race, 0);
        }

        setClose(&gHeroStyleSet);
        setClose(&gHeroRaceSet);
        gHeroStyleSet = styleSet;
        gHeroRaceSet = raceSet;

        // The hero's art from the new sets.
        artCacheFlush();
        return true;
    }

    // As sfall: a choice that isn't there falls back to the race's style 0,
    // then to the game's own look.
    void chooseOrFallBack(int race, int style)
    {
        char gender = dudeGenderLetter();
        if (!loadSets(gender, race, style)) {
            style = 0;
            if (!loadSets(gender, race, style)) {
                race = 0;
                loadSets(gender, race, style);
            }
        }

        gHeroRace = race;
        gHeroStyle = style;
    }

    void storeChoice()
    {
        sfall_gl_vars_store("HAp_Race", gHeroRace);
        sfall_gl_vars_store("HApStyle", gHeroStyle);
    }

    HeroTexts readTexts(char gender, int race, int style, bool ofStyle)
    {
        HeroTexts texts;
        const char* fileName = ofStyle ? "AppStyle.msg" : "AppRace.msg";

        // A style's text from its set, else its race's style 0.
        int styles[] = { style, 0 };
        int count = ofStyle && style != 0 ? 2 : 1;
        for (int index = 0; index < count; index++) {
            HeroSet set;
            if (!setOpen(&set, gender, race, ofStyle ? styles[index] : 0)) {
                continue;
            }

            const char* languages[] = { settings.system.language.c_str(), ENGLISH };
            for (const char* language : languages) {
                char path[COMPAT_MAX_PATH];
                snprintf(path, sizeof(path), "text\\%s\\game\\%s", language, fileName);
                File* stream = setOpenFile(set, path, "rt");
                if (stream == nullptr) {
                    continue;
                }

                MessageList messageList;
                messageListInit(&messageList);
                if (messageListLoadFromFile(&messageList, stream, path)) {
                    MessageListItem item;
                    item.num = 100;
                    if (messageListGetItem(&messageList, &item)) {
                        texts.name = item.text;
                    }
                    item.num = 101;
                    if (messageListGetItem(&messageList, &item)) {
                        texts.description = item.text;
                    }
                }
                messageListFree(&messageList);
                break;
            }

            setClose(&set);
            if (!texts.name.empty()) {
                break;
            }
        }

        return texts;
    }

    const HeroTexts& texts(int gender, int race, int style, bool ofStyle)
    {
        auto key = std::make_tuple(genderLetter(gender), race, ofStyle ? style : 0, ofStyle);
        auto it = gHeroTexts.find(key);
        if (it == gHeroTexts.end()) {
            it = gHeroTexts.emplace(key, readTexts(std::get<0>(key), race, style, ofStyle)).first;
        }
        return it->second;
    }

} // namespace

void heroAppearanceInit()
{
    // Not set in game.cfg: sfall's own setting in ddraw.ini (games set up
    // before game.cfg had it).
    int mode = 0;
    if (!configGetInt(&gContentConfig, CONTENT_CONFIG_MISC_SECTION, "hero_appearance", &mode)) {
        configGetInt(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_HERO_APPEARANCE_KEY, &mode);
    }
    gHeroAppearanceMode = std::clamp(mode, 0, 2);
    gHeroRace = 0;
    gHeroStyle = 0;
    gHeroSetsFound.clear();
}

void heroAppearanceExit()
{
    setClose(&gHeroStyleSet);
    setClose(&gHeroRaceSet);
    gHeroTexts.clear();
    gHeroSetsFound.clear();
    gHeroAppearanceMode = 0;
}

int heroAppearanceMode()
{
    return gHeroAppearanceMode;
}

bool heroAppearanceHasChoice()
{
    if (gHeroAppearanceMode == 0) {
        return false;
    }

    for (char gender : { 'M', 'F' }) {
        if (setExists(gender, 1, 0) || setExists(gender, 0, 1)) {
            return true;
        }
    }
    return false;
}

int heroAppearanceRaceCount(int gender)
{
    int count = 1;
    while (gHeroAppearanceMode != 0 && count < kMaxSetNumber && setExists(genderLetter(gender), count, 0)) {
        count++;
    }
    return count;
}

int heroAppearanceStyleCount(int gender, int race)
{
    int count = 1;
    while (gHeroAppearanceMode != 0 && count < kMaxSetNumber && setExists(genderLetter(gender), race, count)) {
        count++;
    }
    return count;
}

int heroAppearanceRace()
{
    return gHeroRace;
}

int heroAppearanceStyle()
{
    return gHeroStyle;
}

bool heroAppearanceChoose(int race, int style)
{
    if (gHeroAppearanceMode == 0 || race < 0 || style < 0 || race >= kMaxSetNumber || style >= kMaxSetNumber) {
        return false;
    }

    if (!loadSets(dudeGenderLetter(), race, style)) {
        return false;
    }

    gHeroRace = race;
    gHeroStyle = style;
    storeChoice();
    return true;
}

void heroAppearanceGetRaceText(int gender, int race, std::string* name, std::string* description)
{
    const HeroTexts& found = texts(gender, race, 0, false);
    *name = found.name;
    *description = found.description;
}

void heroAppearanceGetStyleText(int gender, int race, int style, std::string* name, std::string* description)
{
    const HeroTexts& found = texts(gender, race, style, true);
    *name = found.name;
    *description = found.description;
}

File* heroAppearanceOpenFile(const char* path)
{
    // No path: an animation the hero's weapon has no art name for
    // (`FrmId::filePath`), not in any set either.
    if (path == nullptr) {
        return nullptr;
    }

    File* stream = setOpenFile(gHeroStyleSet, path, "rb");
    if (stream == nullptr) {
        stream = setOpenFile(gHeroRaceSet, path, "rb");
    }
    return stream;
}

void heroAppearanceReset()
{
    if (gHeroAppearanceMode == 0) {
        return;
    }

    gHeroRace = 0;
    gHeroStyle = 0;
    loadSets(dudeGenderLetter(), 0, 0);
    // The gender's base look (the character's may have changed).
    _proto_dude_update_gender();
}

void heroAppearanceOnNewGame()
{
    if (gHeroAppearanceMode == 0) {
        return;
    }

    // The new game cleared sfall's variables: the character's choice back,
    // only when it isn't the default (as sfall).
    if (gHeroRace > 0 || gHeroStyle > 0) {
        storeChoice();
    }

    heroAppearanceOnGameLoaded();
}

void heroAppearanceOnGameLoaded()
{
    if (gHeroAppearanceMode == 0) {
        return;
    }

    int race = 0;
    int style = 0;
    sfall_gl_vars_fetch("HAp_Race", race);
    sfall_gl_vars_fetch("HApStyle", style);
    chooseOrFallBack(race, style);

    // The dude's art id into the hero's own (`objectSetFrmId`), drawn at
    // once: the map is on the screen already (a save's), and a standing
    // dude would keep the look it was drawn with until he moves (sfall
    // refreshes the map here too).
    Rect rect;
    objectSetFrmId(gDude, FrmId(gDude), &rect);
    tileWindowRefreshRect(&rect, gDude->elevation);
}

void heroAppearanceReadGcd(File* stream)
{
    if (gHeroAppearanceMode == 0) {
        return;
    }

    int race = 0;
    int style = 0;
    int value;
    if (fileReadInt32(stream, &value) != -1 && value >= 0 && value < kMaxSetNumber) {
        race = value;
        if (fileReadInt32(stream, &value) != -1 && value >= 0 && value < kMaxSetNumber) {
            style = value;
        }
    }
    chooseOrFallBack(race, style);
}

void heroAppearanceWriteGcd(File* stream)
{
    if (gHeroAppearanceMode == 0) {
        return;
    }

    if (fileWriteInt32(stream, gHeroRace) != -1) {
        fileWriteInt32(stream, gHeroStyle);
    }
}

} // namespace fallout
