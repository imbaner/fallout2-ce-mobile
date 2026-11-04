#include "door_targets.h"

#include <SDL.h>
#include <stdio.h>

#include <algorithm>
#include <set>
#include <unordered_map>

#include "combat.h"
#include "db.h"
#include "game.h"
#include "interface.h"
#include "item.h"
#include "map.h"
#include "object.h"
#include "platform_compat.h"
#include "proto.h"
#include "proto_instance.h"
#include "scripts.h"
#include "settings.h"
#include "tile.h"
#include "touch_controls.h"

namespace fallout {

namespace {

    // Answers of one script: they depend on nothing else, so it's read once.
    struct ScriptReactions {
        DoorReaction byDamageType[DAMAGE_TYPE_COUNT];
        DoorReaction unarmed;
        DoorReaction unknownDamageType;
    };

    // By script index.
    std::unordered_map<int, ScriptReactions> gReactions;

    // Weapons whose damage type global scripts change: while they fly it may
    // be not what the weapon says now.
    std::set<int> gWeaponsChangingDamageType;
    bool gGlobalScriptsRead = false;

    // Red as the enemies', still: theirs cycle.
    constexpr OutlineType kOutline = OUTLINE_TYPE_SAME_TEAM;

    // Looked for again this often while nothing it depends on changes (doors
    // open and break).
    constexpr unsigned int kRefreshMs = 250;

    struct TargetsKey {
        int tile = -1;
        int actionPoints = -1;
        int hitMode = -1;
        bool aiming = false;
        Object* item = nullptr;

        bool operator==(const TargetsKey& other) const
        {
            return tile == other.tile && actionPoints == other.actionPoints && hitMode == other.hitMode
                && aiming == other.aiming && item == other.item;
        }
    };

    TargetsKey gTargetsKey;
    unsigned int gTargetsTime = 0;
    std::vector<int> gTargetTiles;
    bool gTargetsFound = false;

    bool readFile(const char* path, std::vector<unsigned char>* data)
    {
        File* stream = fileOpen(path, "rb");
        if (stream == nullptr) {
            return false;
        }

        data->resize(fileGetSize(stream));
        size_t read = fileRead(data->data(), 1, data->size(), stream);
        fileClose(stream);
        return read == data->size();
    }

    bool readScriptFile(const char* name, std::vector<unsigned char>* data)
    {
        char path[COMPAT_MAX_PATH];
        snprintf(path, sizeof(path), "%sscripts\\%s", _cd_path_base, name);
        return readFile(path, data);
    }

    void readGlobalScripts()
    {
        if (gGlobalScriptsRead) {
            return;
        }
        gGlobalScriptsRead = true;

        for (const char* pattern : { "scripts\\gl*.int", "scripts\\hs_*.int" }) {
            char** files;
            int filesLength = fileNameListInit(pattern, &files);
            if (filesLength == 0) {
                continue;
            }

            for (int index = 0; index < filesLength; index++) {
                std::vector<unsigned char> data;
                if (readScriptFile(files[index], &data)) {
                    doorReactionFindDamageTypeChanges(data.data(), data.size(), &gWeaponsChangingDamageType);
                }
            }

            fileNameListFree(&files, 0);
        }
    }

    const ScriptReactions* reactionsOf(int scriptIndex)
    {
        auto known = gReactions.find(scriptIndex);
        if (known != gReactions.end()) {
            return &(known->second);
        }

        char name[COMPAT_MAX_PATH];
        std::vector<unsigned char> data;
        if (scriptsGetFileName(scriptIndex, name, sizeof(name)) != 0 || !readScriptFile(name, &data)) {
            return nullptr;
        }

        ScriptReactions reactions;
        for (int damageType = 0; damageType < DAMAGE_TYPE_COUNT; damageType++) {
            reactions.byDamageType[damageType] = doorReactionRead(data.data(), data.size(), damageType);
        }
        reactions.unarmed = doorReactionRead(data.data(), data.size(), kDoorReactionUnarmed);
        reactions.unknownDamageType = doorReactionRead(data.data(), data.size(), kDoorReactionUnknownDamageType);

        return &(gReactions[scriptIndex] = reactions);
    }

    // The doors of the elevation (the game's search has one state: the checks
    // of a door may use it).
    std::vector<Object*> doorsOfElevation()
    {
        std::vector<Object*> doors;
        for (Object* object = objectFindFirstAtElevation(gElevation); object != nullptr; object = objectFindNextAtElevation()) {
            if (doorTargetIsDoor(object)) {
                doors.push_back(object);
            }
        }
        return doors;
    }

    // Set directly, as `mapHintsSetInteraction`: many doors have the "no
    // highlight" flag. An outline set by something else stays.
    void setOutline(Object* door, bool outlined)
    {
        OutlineType outline = door->outline & OUTLINE_TYPE_MAX;
        if (outlined ? outline != OUTLINE_TYPE_NONE : outline != kOutline) {
            return;
        }

        // The rect while outlined covers the outline too.
        Rect rect;
        if (outlined) {
            door->outline = kOutline;
            objectGetRect(door, &rect);
        } else {
            objectGetRect(door, &rect);
            door->outline = OUTLINE_TYPE_NONE;
        }
        tileWindowRefreshRect(&rect, door->elevation);
    }

    void findTargets()
    {
        gTargetTiles.clear();
        gTargetsFound = true;

        bool highlight = settings.preferences.target_highlight != TARGET_HIGHLIGHT_OFF;
        for (Object* door : doorsOfElevation()) {
            bool target = (door->flags & OBJECT_HIDDEN) == 0 && doorTargetCanBeAttacked(door);
            if (target) {
                gTargetTiles.push_back(door->tile);
            }
            setOutline(door, target && highlight);
        }
    }

    void forgetTargets()
    {
        if (!gTargetsFound) {
            return;
        }

        for (Object* door : doorsOfElevation()) {
            setOutline(door, false);
        }
        gTargetTiles.clear();
        gTargetsKey = TargetsKey();
        gTargetsFound = false;
    }

} // namespace

bool doorTargetIsDoor(Object* object)
{
    const ProtoId protoId = object;
    if (protoId.objectType() != OBJ_TYPE_SCENERY) {
        return false;
    }

    Proto* proto;
    return protoGetProto(protoId, &proto) != -1 && proto->scenery.type == SCENERY_TYPE_DOOR;
}

DoorReaction doorTargetReaction(Object* door, Object* weapon)
{
    Script* script;
    if (door->sid == -1 || scriptGetScript(door->sid, &script) == -1) {
        return DoorReaction::None;
    }

    const ScriptReactions* reactions = reactionsOf(script->index & 0xFFFFFF);
    if (reactions == nullptr) {
        return DoorReaction::None;
    }

    if (weapon == nullptr) {
        return reactions->unarmed;
    }

    readGlobalScripts();
    if (gWeaponsChangingDamageType.count(weapon->pid) != 0) {
        return reactions->unknownDamageType;
    }

    DamageType damageType = weaponGetDamageType(nullptr, weapon);
    return damageTypeIsValid(damageType) ? reactions->byDamageType[damageType] : reactions->unknownDamageType;
}

bool doorTargetCanBeAttacked(Object* object)
{
    if (!doorTargetIsDoor(object) || objectIsOpen(object)) {
        return false;
    }

    HitMode hitMode;
    bool aiming;
    if (interfaceGetCurrentHitMode(&hitMode, &aiming) == -1) {
        return false;
    }

    DoorReaction reaction = doorTargetReaction(object, critterGetWeaponForHitMode(gDude, hitMode));
    DoorReaction least = settings.enhancements.door_targets_breakable_only ? DoorReaction::Door : DoorReaction::Script;
    return reaction >= least && _combat_check_bad_shot(gDude, object, hitMode, aiming) == COMBAT_BAD_SHOT_OK;
}

void doorTargetsUpdate()
{
    if (gDude == nullptr
        || !isInCombat()
        || (GameMode::getCurrentGameMode() & GameMode::kPlayerTurn) == 0
        || !touchControlsIsEnabled()) {
        forgetTargets();
        return;
    }

    TargetsKey key;
    key.tile = gDude->tile;
    key.actionPoints = gDude->data.critter.combat.ap;
    HitMode hitMode;
    if (interfaceGetCurrentHitMode(&hitMode, &key.aiming) == 0) {
        key.hitMode = hitMode;
    }
    if (interfaceGetActiveItem(&key.item) == -1) {
        key.item = nullptr;
    }

    unsigned int now = SDL_GetTicks();
    if (!gTargetsFound || !(key == gTargetsKey) || now - gTargetsTime >= kRefreshMs) {
        gTargetsKey = key;
        gTargetsTime = now;
        findTargets();
    }
}

const std::vector<int>& doorTargetTiles()
{
    return gTargetTiles;
}

Object* doorTargetAt(int tile)
{
    if (std::find(gTargetTiles.begin(), gTargetTiles.end(), tile) == gTargetTiles.end()) {
        return nullptr;
    }

    for (Object* object = objectFindFirstAtLocation(gElevation, tile); object != nullptr; object = objectFindNextAtLocation()) {
        if (doorTargetIsDoor(object)) {
            return object;
        }
    }
    return nullptr;
}

} // namespace fallout
