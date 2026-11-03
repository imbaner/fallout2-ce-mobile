#include "map_hints.h"

#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "animation.h"
#include "combat.h"
#include "critter.h"
#include "game.h"
#include "ground_lines.h"
#include "map.h"
#include "mui.h"
#include "mui_draw.h"
#include "mui_icons.h"
#include "mui_screens.h"
#include "object.h"
#include "player_commands.h"
#include "svga.h"
#include "tactical_view.h"
#include "tile.h"
#include "touch_controls.h"
#include "touch_hud.h"
#include "window_manager.h"
#include "world_view.h"

namespace fallout {

namespace {

    const MuiColor kDanger = muiRgb(0xE0584F);
    const MuiColor kPill = muiRgb(0x0B120D, 230);

    // Game modes of screens covering the map (as for the HUD).
    constexpr int kScreenGameModes = ~(GameMode::kCombat | GameMode::kPlayerTurn | GameMode::kSpecial | GameMode::kUseOn);

    Object* gAttackTarget = nullptr;
    Object* gUseTarget = nullptr;
    int gMoveTile = -1;
    int gDestination = -1;

    // Object dude interacts with, outlined green (the game's outline) until
    // dude is done; its own outline is restored then.
    Object* gInteraction = nullptr;
    OutlineType gInteractionSavedOutline = OUTLINE_TYPE_NONE;

    // Move cost of `gMoveTile`, computed again when dude moves or spends
    // action points (pathfinding is too slow for every frame).
    struct MoveCost {
        int tile = -1;
        int from = -1;
        int actionPoints = -1;
        int cost = -1;
    };
    MoveCost gMoveCost;

    // [object] at the elevation shown.
    bool objectExists(Object* object)
    {
        for (Object* candidate = objectFindFirstAtElevation(gElevation); candidate != nullptr; candidate = objectFindNextAtElevation()) {
            if (candidate == object) {
                return true;
            }
        }
        return false;
    }

    // [object] on the map at any elevation: stairs and ladders take the dude
    // to another elevation before he's done using them.
    bool objectOnMap(Object* object)
    {
        for (Object* candidate = objectFindFirst(); candidate != nullptr; candidate = objectFindNext()) {
            if (candidate == object) {
                return true;
            }
        }
        return false;
    }

    // The attack target: in the tactical view its outline alone cycles
    // (`objectSetTargetOutline`), the game's buffer is drawn again where it
    // changes.
    void setAttackTarget(Object* target)
    {
        if (target == gAttackTarget) {
            return;
        }

        Object* previous = gAttackTarget;
        gAttackTarget = target;
        objectSetTargetOutline(target);
        for (Object* object : { previous, target }) {
            if (object != nullptr && objectExists(object)) {
                Rect rect;
                objectGetRect(object, &rect);
                tileWindowRefreshRect(&rect, object->elevation);
            }
        }
    }

    void clearInteraction()
    {
        if (gInteraction != nullptr && objectOnMap(gInteraction)) {
            // The rect while outlined covers the outline too.
            Rect rect;
            objectGetRect(gInteraction, &rect);
            gInteraction->outline = gInteractionSavedOutline;
            tileWindowRefreshRect(&rect, gInteraction->elevation);
        }
        gInteraction = nullptr;
    }

    // Center of [tile] in output pixels.
    bool tileCenter(int tile, float scale, SDL_FPoint* point)
    {
        int worldX;
        int worldY;
        if (tileToScreenXY(tile, &worldX, &worldY) != 0) {
            return false;
        }

        int screenX;
        int screenY;
        worldViewWorldToScreen(worldX + 16, worldY + 8, &screenX, &screenY);
        *point = { screenX * scale, screenY * scale };
        return true;
    }

    // MARK: Tactical view

    MuiColor paletteColor(int index, Uint8 alpha = 255)
    {
        const unsigned char* palette = directDrawGetPalette();
        return { static_cast<Uint8>(palette[index * 3] << 2), static_cast<Uint8>(palette[index * 3 + 1] << 2), static_cast<Uint8>(palette[index * 3 + 2] << 2), alpha };
    }

    // The combat's living critters (all on the map), the dude among them.
    std::vector<Object*> gCombatCritters;

    void addCombatCritter(Object* critter)
    {
        gCombatCritters.push_back(critter);
    }

    // A tile's center in the game's buffer, times 3 (corners are sums of
    // three centers: exact, the same from each tile around them).
    bool tileCenter3(int tile, int* x, int* y)
    {
        int worldX;
        int worldY;
        if (!tileIsValid(tile) || tileToScreenXY(tile, &worldX, &worldY) != 0) {
            return false;
        }
        *x = (worldX + 16) * 3;
        *y = (worldY + 8) * 3;
        return true;
    }

    // Corner `k` of [tile]'s hex is where it meets neighbours `k` and
    // `k + 1`: the sum of the three centers (world, times 3).
    bool tileCorners3(int tile, std::array<SDL_Point, ROTATION_COUNT>* corners)
    {
        int centerX;
        int centerY;
        if (!tileCenter3(tile, &centerX, &centerY)) {
            return false;
        }

        std::array<SDL_Point, ROTATION_COUNT> neighbours;
        for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
            if (!tileCenter3(tileGetTileInDirection(tile, static_cast<Rotation>(rotation), 1), &(neighbours[rotation].x), &(neighbours[rotation].y))) {
                return false;
            }
        }

        for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
            const SDL_Point& next = neighbours[(rotation + 1) % ROTATION_COUNT];
            (*corners)[rotation] = { (centerX + neighbours[rotation].x + next.x) / 3, (centerY + neighbours[rotation].y + next.y) / 3 };
        }
        return true;
    }

    // A corner (times 3) in the game's buffer, at the pixel's middle.
    SDL_FPoint cornerPoint(const SDL_Point& corner)
    {
        return { corner.x / 3.0f + 0.5f, corner.y / 3.0f + 0.5f };
    }

    long long cornerKey(const SDL_Point& corner)
    {
        return (static_cast<long long>(corner.x) << 32) ^ static_cast<unsigned int>(corner.y);
    }

    // Lying on the ground (ground_lines.h, between its begin and end): a
    // faint grid where the dude can walk, the area's border, everyone's tile
    // in their outline's color (the dude's and the selected enemy's
    // thicker).
    void addTacticalView(MuiContext& ui)
    {
        const TacticalViewReach& reach = tacticalViewGetReach();
        if (!reach.reachable.empty()) {
            std::unordered_set<int> area(reach.reachable.begin(), reach.reachable.end());
            area.insert(gDude->tile);

            // Fainter zoomed out (tiles get small).
            float zoom = std::clamp(worldViewGetZoom(), 0.5f, 1.0f);
            MuiColor grid = muiRgb(0xD9C79A, static_cast<Uint8>(22.0f + 26.0f * (zoom - 0.5f) * 2.0f));

            // The area's border: its tiles' edges towards tiles outside it,
            // each from corner `d - 1` to `d` (the same way round for every
            // tile), joined into closed loops.
            std::unordered_map<long long, SDL_Point> borderNext;
            std::unordered_map<long long, SDL_Point> borderFrom;
            std::array<SDL_Point, ROTATION_COUNT> corners;
            for (int tile : area) {
                if (!tileCorners3(tile, &corners)) {
                    continue;
                }
                for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
                    const SDL_Point& from = corners[(rotation + ROTATION_COUNT - 1) % ROTATION_COUNT];
                    const SDL_Point& to = corners[rotation];
                    if (area.count(tileGetTileInDirection(tile, static_cast<Rotation>(rotation), 1)) == 0) {
                        borderNext[cornerKey(from)] = to;
                        borderFrom[cornerKey(from)] = from;
                    } else if (rotation < 3) {
                        // An edge inside: drawn by the tile on its side
                        // towards neighbours 0-2, once.
                        groundLinesAddPath({ cornerPoint(from), cornerPoint(to) }, false, ui.dp(1.0f), grid);
                    }
                }
            }

            std::vector<std::vector<SDL_FPoint>> loops;
            while (!borderNext.empty()) {
                std::vector<SDL_FPoint> loop;
                SDL_Point corner = borderFrom.begin()->second;
                while (true) {
                    auto it = borderNext.find(cornerKey(corner));
                    if (it == borderNext.end()) {
                        break;
                    }
                    loop.push_back(cornerPoint(corner));
                    corner = it->second;
                    borderFrom.erase(it->first);
                    borderNext.erase(it);
                }
                loops.push_back(std::move(loop));
            }

            // A dark edge under the line keeps it apart from the ground.
            for (const std::vector<SDL_FPoint>& loop : loops) {
                groundLinesAddPath(loop, true, ui.dp(1.4f) + ui.dp(2.0f), muiRgb(0x000000, 70));
            }
            for (const std::vector<SDL_FPoint>& loop : loops) {
                groundLinesAddPath(loop, true, ui.dp(1.4f), muiTheme().accent.withAlpha(115));
            }
        }

        gCombatCritters.clear();
        gCombatCritters.push_back(gDude);
        combatForEachCritter(addCombatCritter);
        std::array<SDL_Point, ROTATION_COUNT> corners;
        for (Object* object : gCombatCritters) {
            if (object->elevation != gElevation || (object->flags & OBJECT_HIDDEN) != 0) {
                continue;
            }

            int color;
            bool thick;
            if (!tacticalViewTileLook(object, &color, &thick) || !tileCorners3(object->tile, &corners)) {
                continue;
            }

            std::vector<SDL_FPoint> outline;
            for (const SDL_Point& corner : corners) {
                outline.push_back(cornerPoint(corner));
            }
            groundLinesAddPath(outline, true, ui.dp(thick ? 2.6f : 1.6f), paletteColor(color));
        }
    }

    // The tile dude walks to or would move to in combat, lying on the
    // ground as the tactical view's tiles: filled faintly, its edge in
    // [color], thinner zoomed out (down to 60%: tiles get small, the edge
    // stays seen).
    void addTileMarker(MuiContext& ui, int tile, MuiColor color)
    {
        std::array<SDL_Point, ROTATION_COUNT> corners;
        if (!tileCorners3(tile, &corners)) {
            return;
        }
        std::vector<SDL_FPoint> outline;
        for (const SDL_Point& corner : corners) {
            outline.push_back(cornerPoint(corner));
        }
        groundLinesAddFill(outline, color.withAlpha(45));
        groundLinesAddPath(outline, true, ui.dp(1.6f) * std::clamp(worldViewGetZoom(), 0.6f, 1.0f), color);
    }

    float outlineRight(const std::vector<SDL_FPoint>& points)
    {
        float right = points.front().x;
        for (const SDL_FPoint& point : points) {
            right = std::max(right, point.x);
        }
        return right;
    }

    // Small label: optional icon, optional text.
    void drawPill(MuiContext& ui, float x, float centerY, bool centered, MuiIcon icon, bool hasIcon, const std::u32string& text, MuiColor color, MuiColor textColor)
    {
        float size = ui.dp(13.0f);
        float height = ui.dp(22.0f);
        float padding = ui.dp(7.0f);
        float iconSize = ui.dp(13.0f);
        float textWidth = text.empty() ? 0.0f : muiTextWidth(text, size);
        float width = padding * 2.0f + (hasIcon ? iconSize : 0.0f) + (hasIcon && !text.empty() ? ui.dp(3.0f) : 0.0f) + textWidth;

        MuiRect rect = { centered ? x - width / 2.0f : x, centerY - height / 2.0f, width, height };
        muiFillRoundRect(rect, height / 2.0f, kPill);
        muiStrokeRoundRect(rect, height / 2.0f, ui.dp(1.0f), color.withAlpha(200));

        float cursor = rect.x + padding;
        if (hasIcon) {
            muiDrawIcon(icon, cursor + iconSize / 2.0f, rect.centerY(), iconSize, color);
            cursor += iconSize + ui.dp(3.0f);
        }
        if (!text.empty()) {
            muiDrawTextAligned(text, { cursor, rect.y, textWidth + 1.0f, rect.h }, size, textColor, MuiAlign::Start, MuiAlign::Center);
        }
    }

    std::u32string toText(const std::string& text)
    {
        return std::u32string(text.begin(), text.end());
    }

    class MapHintsScreen : public MuiScreen {
    public:
        MapHintsScreen()
        {
            modal = false;
        }

        bool isActive() override
        {
            validate();

            bool anything = gAttackTarget != nullptr || gMoveTile != -1 || gDestination != -1 || tacticalViewIsShown();
            return anything
                && touchControlsIsEnabled()
                && (GameMode::getCurrentGameMode() & kScreenGameModes) == 0
                && !windowIsModalShown();
        }

        void build(MuiContext& ui) override;

    private:
        void validate();
    };

    MapHintsScreen gMapHintsScreen;
    bool gMapHintsPushed = false;

    void MapHintsScreen::validate()
    {
        if (gDude == nullptr) {
            mapHintsClearSelection();
            gDestination = -1;
            return;
        }

        // Selection is for combat only.
        if (!isInCombat()) {
            mapHintsClearSelection();
        }

        if (gAttackTarget != nullptr && (!objectExists(gAttackTarget) || critterIsDead(gAttackTarget))) {
            setAttackTarget(nullptr);
        }

        if (gUseTarget != nullptr && !objectExists(gUseTarget)) {
            gUseTarget = nullptr;
        }

        if (gDestination != -1 && (gDude->tile == gDestination || !animationIsBusy(gDude))) {
            gDestination = -1;
        }

        // Selected for use in combat, or dude still walking to / acting on
        // it.
        if (gInteraction != nullptr && gInteraction != gUseTarget && !animationIsBusy(gDude)) {
            clearInteraction();
        }
    }

    void MapHintsScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        float scale = ui.screenRect().w / screenGetWidth();

        bool reachable = false;
        if (gMoveTile != -1) {
            int actionPoints = gDude->data.critter.combat.ap;
            if (gMoveCost.tile != gMoveTile || gMoveCost.from != gDude->tile || gMoveCost.actionPoints != actionPoints) {
                gMoveCost = { gMoveTile, gDude->tile, actionPoints, playerGetMoveCost(gMoveTile) };
            }
            reachable = gMoveCost.cost != -1 && gMoveCost.cost <= actionPoints;
        }

        // On the ground, in one draw: the tactical view, the tiles dude
        // walks to and would move to.
        if (tacticalViewIsShown() || gDestination != -1 || gMoveTile != -1) {
            groundLinesBegin(scale);
            if (tacticalViewIsShown()) {
                addTacticalView(ui);
            }
            if (gDestination != -1) {
                addTileMarker(ui, gDestination, theme.accent);
            }
            if (gMoveTile != -1) {
                addTileMarker(ui, gMoveTile, reachable ? theme.accent : kDanger);
            }
            groundLinesEnd();
        }

        if (gMoveTile != -1) {
            // Cost inside the tile, over what stands there, with a shadow to
            // read it over the ground.
            SDL_FPoint center;
            if (tileCenter(gMoveTile, scale, &center)) {
                if (gMoveCost.cost == -1) {
                    muiDrawIcon(MuiIcon::Cancel, center.x, center.y, ui.dp(12.0f), kDanger);
                } else {
                    std::u32string text = toText(std::to_string(gMoveCost.cost));
                    float size = ui.dp(14.0f);
                    MuiRect textRect = { center.x - ui.dp(20.0f), center.y - ui.dp(10.0f), ui.dp(40.0f), ui.dp(20.0f) };
                    MuiRect shadowRect = { textRect.x + ui.dp(1.0f), textRect.y + ui.dp(1.0f), textRect.w, textRect.h };
                    muiDrawTextAligned(text, shadowRect, size, muiRgb(0x000000, 220), MuiAlign::Center, MuiAlign::Center);
                    muiDrawTextAligned(text, textRect, size, reachable ? theme.text : kDanger, MuiAlign::Center, MuiAlign::Center);
                }
            }
        }

        // Hit chance above the enemy (the tactical view too: there the enemy
        // is picked by its tile).
        if (gAttackTarget != nullptr) {
            SDL_FPoint pill;
            MuiRect rect;
            bool placed = false;
            if (muiObjectRect(gAttackTarget, &rect)) {
                pill = { rect.centerX(), rect.y - ui.dp(14.0f) };
                placed = true;
            }

            int chance;
            if (placed && playerGetHitChance(gAttackTarget, &chance)) {
                // No chance: dashes, like the game's called shot window.
                drawPill(ui, pill.x, pill.y, true, MuiIcon::Aim, false, toText(chance >= 0 ? std::to_string(chance) + "%" : "--"), theme.accent, theme.text);
            } else if (placed) {
                drawPill(ui, pill.x, pill.y, true, MuiIcon::Cancel, true, std::u32string(), kDanger, kDanger);
            }
        }
    }

} // namespace

void mapHintsInit()
{
    // The tile markers lie on the ground in any mode (`addTileMarker`).
    objectSetCoverRecorded(true);

    if (!gMapHintsPushed) {
        // Under the HUD (screens draw in the order they were pushed).
        muiPush(&gMapHintsScreen);
        gMapHintsPushed = true;
    }
}

void mapHintsSetAttackTarget(Object* target)
{
    clearInteraction();
    setAttackTarget(target);
    gUseTarget = nullptr;
    gMoveTile = -1;
}

void mapHintsSetUseTarget(Object* target)
{
    gUseTarget = target;
    setAttackTarget(nullptr);
    gMoveTile = -1;
    mapHintsSetInteraction(target);
}

void mapHintsSetInteraction(Object* object)
{
    if (object == gInteraction) {
        return;
    }

    clearInteraction();
    if (object == nullptr || object == gDude) {
        return;
    }

    // Set directly: the game's outline is drawn for any object, "no
    // highlight" objects (shelves, many containers and doors) are only kept
    // out of the game's own highlighting.
    // The highlight's outline isn't kept: the highlight outlines the object
    // again if it's still on, and nothing is left if it was switched off.
    gInteraction = object;
    gInteractionSavedOutline = touchHudHighlightOwns(object) ? OUTLINE_TYPE_NONE : object->outline;
    object->outline = OUTLINE_TYPE_FRIENDLY;
    if ((object->flags & OBJECT_HIDDEN) != 0) {
        object->outline = static_cast<OutlineType>(object->outline | OUTLINE_DISABLED);
    }

    Rect rect;
    objectGetRect(object, &rect);
    tileWindowRefreshRect(&rect, object->elevation);
}

void mapHintsSetMoveTile(int tile)
{
    clearInteraction();
    gMoveTile = tile;
    setAttackTarget(nullptr);
    gUseTarget = nullptr;
}

void mapHintsSetDestination(int tile)
{
    gDestination = tile;
}

int mapHintsGetDestination()
{
    return gDestination;
}

Object* mapHintsGetAttackTarget()
{
    return gAttackTarget;
}

void mapHintsClearSelection()
{
    setAttackTarget(nullptr);
    gUseTarget = nullptr;
    gMoveTile = -1;
}

} // namespace fallout
