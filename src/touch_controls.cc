#include "touch_controls.h"

#include <math.h>

#include <algorithm>

#include "action_log.h"
#include "actions.h"
#include "art.h"
#include "color.h"
#include "combat.h"
#include "critter.h"
#include "dev_autotest.h"
#include "draw.h"
#include "game.h"
#include "game_mouse.h"
#include "hud_layout.h"
#include "input.h"
#include "map.h"
#include "map_hints.h"
#include "mouse.h"
#include "mui.h"
#include "mui_icons.h"
#include "object.h"
#include "party_member.h"
#include "player_commands.h"
#include "settings.h"
#include "svga.h"
#include "tactical_view.h"
#include "tile.h"
#include "touch_hud.h"
#include "touch_log.h"
#include "window_manager.h"
#include "world_view.h"

namespace fallout {

enum TouchAction {
    TOUCH_ACTION_NONE,
    TOUCH_ACTION_MOVE,
    TOUCH_ACTION_USE,
    TOUCH_ACTION_ATTACK,
};

static constexpr int kRadialMenuMaxItems = GAME_MOUSE_ACTION_MENU_ITEM_COUNT - 1;

// Radial menu sizes, dp: button diameter, minimal distance from the center of
// the menu to button centers, extra space around buttons which still counts as
// a hit.
static constexpr float kRadialMenuButtonSizeDp = 54.0f;
static constexpr float kRadialMenuMinRadiusDp = 70.0f;
static constexpr float kRadialMenuHitSlopDp = 8.0f;

struct RadialMenu {
    bool open = false;
    Object* target = nullptr;

    // Screen point the menu was opened at (mouse cursor is placed there when
    // the action is performed).
    int anchorX = 0;
    int anchorY = 0;

    GameMouseActionMenuItem items[kRadialMenuMaxItems];
    int itemsLength = 0;

    // Index of the item in the list the menu was opened with.
    int itemSourceIndex[kRadialMenuMaxItems];

    // Modal menu returns selected item instead of performing it.
    bool modal = false;
    int result = -1;

    // Button centers in screen coordinates, button diameter.
    int itemX[kRadialMenuMaxItems];
    int itemY[kRadialMenuMaxItems];
    int itemSize = 0;

    // Menu center.
    int centerX = 0;
    int centerY = 0;

    int highlightedItem = -1;

    // The finger which opened the menu moved away from where it was. Menu
    // kept on screen may put a button under the finger, lifting it there
    // without sliding must not choose it.
    bool fingerMoved = false;
};

// Finger movement which starts choosing by sliding, dp.
static constexpr float kRadialMenuSlideStartDp = 12.0f;

// Draws the radial menu with the mobile UI. Input stays in touch controls
// (the finger which opened the menu by long press belongs to them), except
// menus opened from mobile UI screens: the finger belongs to the mobile UI,
// the menu takes it there (`pointerInput`).
class RadialMenuScreen : public MuiScreen {
public:
    RadialMenuScreen()
    {
        modal = false;
    }

    void build(MuiContext& ui) override;

    bool pointerInput = false;
    bool pointerWasDown = false;

private:
    void handlePointer(MuiContext& ui, float scale);
};

// Names of action menu items (shown while choosing), `game\ce.msg` 146 +
// item, indexed by `GAME_MOUSE_ACTION_MENU_ITEM_*`.
static constexpr int kActionNameMessageBase = 146;
static const char* const kActionNameFallbacks[GAME_MOUSE_ACTION_MENU_ITEM_COUNT] = {
    "Cancel",
    "Drop",
    "Inventory",
    "Look",
    "Rotate",
    "Talk",
    "Use",
    "Unload",
    "Use skill",
    "Push",
};

static RadialMenuScreen gRadialMenuScreen;

static void touchControlsSetGameMouseMode(GameMouseMode mode);
static TouchAction touchControlsGetActionAt(int worldX, int worldY, Object** targetPtr);
static TouchAction touchControlsGetActionFor(Object* target, Object** targetPtr);
static void touchControlsHandleTap(int x, int y);
static void touchControlsSnapTap(int x, int y, int* tile, TouchAction* action, Object** target);
static bool touchControlsSnapToItem(int x, int y, TouchAction* action, Object** target);
static void touchControlsStickToSelection(int x, int y, int worldX, int worldY, int* tile, TouchAction* action, Object** target);
static bool touchControlsGetBodyColumn(Object* critter, Rect* rect);
static Object* touchControlsFindItemNear(int x, int y);
static Object* touchControlsFindEnemyNear(int x, int y, int tile);
static bool touchControlsObjectExists(Object* object);
static void touchControlsPerform(TouchAction action, int tile, Object* target);
static void touchControlsClearSelection();
static bool radialMenuOpen(int x, int y);
static bool radialMenuOpenItems(const GameMouseActionMenuItem* items, int itemsLength, int x, int y);
static void radialMenuClose();
static int radialMenuHitTest(int x, int y);
static void radialMenuHighlight(int index);
static void radialMenuSelect(int index);
static bool radialMenuHandleGesture(const Gesture* gesture);

// One finger drag moving the view.
// Tap magnets, radius in dp (physical size, so on zoomed out map they cover
// several tiles, on zoomed in map less than a tile):
// - tap near the tile dude goes to (or the tile selected in combat) is a tap
//   on that tile (second tap on the same tile runs / confirms the move);
static constexpr float kTileSnapRadiusDp = 24.0f;
// - with item highlight on (HUD), items have priority over the ground: a tap
//   on or near an item's sprite box uses the item (also in the tactical
//   view). Without highlight taps pick items by their pixels as the game
//   does, so dude can walk next to them.
static constexpr float kItemSnapRadiusDp = 24.0f;
// - in combat a tap on the ground picks an enemy on the tile it stands on
//   (can't be walked to anyway) or on its body: a narrow column over its
//   tile from the top of the sprite to the feet, half width in world pixels
//   (zooms with the map) - less than the 16 px to the centers of neighbour
//   tiles, so they can still be walked to (sprite boxes of wide critters
//   cover them).
static constexpr int kEnemyBodyHalfWidth = 12;
static constexpr int kEnemyBodyBelowFeet = 4;
// - in combat a second tap near the selection confirms it, before anything
//   else under the finger (`touchControlsStickToSelection`): the selected
//   tile within the tile magnet even under a critter's sprite (but not on a
//   critter's own tile - tapping it picks the critter); the selected enemy
//   on its pixels even under another one's sprite (no margin around it:
//   tiles next to it stay easy to pick).

static bool gPanActive = false;
static int gPanPrevX = 0;
static int gPanPrevY = 0;

// Long press gesture over the map (its further events belong to us).
static bool gLongPressActive = false;

// Combat selection waiting for confirmation (second tap).
static TouchAction gPendingAction = TOUCH_ACTION_NONE;
static Object* gPendingTarget = nullptr;
static int gPendingTile = -1;

static RadialMenu gRadialMenu;

bool touchControlsIsEnabled()
{
#if FALLOUT_TOUCH_ONLY
    // Always on (touch.h): only the map's view has to exist (isoInit..isoExit).
    return worldViewIsEnabled();
#else
    return settings.touch.controls && worldViewIsEnabled();
#endif
}

bool touchControlsIsNative()
{
#if FALLOUT_TOUCH_ONLY
    return true;
#else
    return touchControlsIsEnabled() && muiIsEnabled();
#endif
}

// The last point touched on the map (scripts reading the mouse get it).
static bool gLastPointValid = false;
static int gLastPointX = 0;
static int gLastPointY = 0;

static void touchControlsSetLastPoint(int x, int y)
{
    gLastPointValid = true;
    gLastPointX = x;
    gLastPointY = y;
}

bool touchControlsGetLastPoint(int* x, int* y)
{
    if (!gLastPointValid) {
        return false;
    }
    *x = gLastPointX;
    *y = gLastPointY;
    return true;
}

// Scripts learn about taps and long presses on the map as they would about
// the mouse's left button (sfall `HOOK_MOUSECLICK`): holding it opens the
// action menu in the game, as a long press does.
static bool gScriptsButtonHeld = false;

static void touchControlsNotifyScripts(bool pressed)
{
    gScriptsButtonHeld = pressed;
    inputRunMouseClickHook(SDL_BUTTON_LEFT, pressed);
}

void touchControlsProcessGestures()
{
    tacticalViewUpdate();

    if (!touchControlsIsNative()) {
        return;
    }

    Gesture gesture;
    while (touch_get_gesture(&gesture)) {
        // Touch controls have no hidden multi-finger gestures (the game's
        // right button, shortcuts): only two finger pans (the view).
        if (gesture.numberOfTouches >= 2 && !(gesture.type == kPan && gesture.numberOfTouches == 2)) {
            touchLogGesture(gesture, TouchLogGestureFate::Ignored);
            continue;
        }

        // A tap or a long press made long ago isn't done now; movement of
        // the view and ends of what began are, keeping gestures whole.
        bool stale = SDL_GetTicks() - gesture.time > kTouchStaleMs;
        if (stale && (gesture.type == kTap || (gesture.type == kLongPress && gesture.state == kBegan))) {
            touchLogGesture(gesture, TouchLogGestureFate::Stale);
            continue;
        }

        touchLogGesture(gesture, TouchLogGestureFate::Handled);
        touchControlsHandleGesture(&gesture);

        // The long press let go (on the map or on the radial menu it
        // opened).
        if (gesture.type == kLongPress && gesture.state == kEnded && gScriptsButtonHeld) {
            touchControlsNotifyScripts(false);
        }
    }
}

#if !FALLOUT_TOUCH_ONLY
bool touchControlsWantsCursorWarp(int x, int y)
{
    return !(touchControlsIsEnabled() && worldViewIsWorldAt(x, y) && gameMouseIsMapScrollingEnabled());
}

// Distance (in screen pixels) within which small buttons catch touches.
static constexpr int kButtonTouchSlop = 14;

void touchControlsAdjustUiTouch(int* x, int* y)
{
    if (!touchControlsIsEnabled()) {
        return;
    }

    int buttonX;
    int buttonY;
    if (windowFindNearestButton(*x, *y, kButtonTouchSlop, &buttonX, &buttonY)) {
        *x = buttonX;
        *y = buttonY;
    }
}
#endif

void touchControlsMoveView(float dx, float dy, float zoomFactor, float anchorX, float anchorY)
{
    // Move the point under fingers along with them, then zoom around fingers'
    // new position.
    worldViewPanBy(dx, dy);
    if (zoomFactor != 1.0f) {
        worldViewZoomBy(zoomFactor, anchorX, anchorY);
    }
}

void touchControlsReset()
{
    // A long press forgotten (its end never came): scripts learn it's up.
    if (gScriptsButtonHeld) {
        touchControlsNotifyScripts(false);
    }

    radialMenuClose();
    touchControlsClearSelection();
    gPanActive = false;
    gLongPressActive = false;

    if (touchControlsIsEnabled()) {
        int mode = gameMouseGetMode();
        if (mode == GAME_MOUSE_MODE_ARROW || mode == GAME_MOUSE_MODE_CROSSHAIR) {
            gameMouseSetMode(GAME_MOUSE_MODE_MOVE);
        }
    }

    gameMouseObjectsHide();
}

bool touchControlsWantsGameMouseObjects()
{
    // Touch controls have no cursor, their hints are in map_hints.cc.
    return !touchControlsIsEnabled();
}

// The map's view may be moved and zoomed: while the game lets the map
// scroll, and also while it waits for its own animation over the map (its
// watch cursor: the dude changing weapons, a combat starting; on a
// computer the map doesn't scroll then). Not while a script holds the
// game's UI: a scene is seen as its authors meant it.
static bool touchControlsViewMayMove()
{
    if (isoIsDisabled() || gameUiIsDisabledByScript()) {
        return false;
    }
    return gameMouseIsMapScrollingEnabled() || gameMouseGetCursor() == MOUSE_CURSOR_WAIT_WATCH;
}

// Two fingers over the map move and zoom its view.
static bool touchControlsHandlePinch(const Gesture* gesture)
{
    static bool active = false;
    static int prevX;
    static int prevY;
    static int prevSpan;

    if (gesture->state == kBegan) {
        // As one finger moving the view: over the map while it may scroll.
        active = worldViewIsWorldAt(gesture->startX, gesture->startY) && touchControlsViewMayMove();
        if (active) {
            worldViewBeginGesture();
        }

        // Count movement from the point where fingers touched the screen,
        // so the map doesn't lag behind them.
        prevX = gesture->startX;
        prevY = gesture->startY;
        prevSpan = gesture->startSpan;
    }

    if (!active) {
        return false;
    }

    float zoomFactor = prevSpan > 0 && gesture->span > 0
        ? static_cast<float>(gesture->span) / prevSpan
        : 1.0f;
    touchControlsMoveView(static_cast<float>(gesture->x - prevX),
        static_cast<float>(gesture->y - prevY),
        zoomFactor,
        static_cast<float>(gesture->x),
        static_cast<float>(gesture->y));

    prevX = gesture->x;
    prevY = gesture->y;
    prevSpan = gesture->span;

    if (gesture->state == kEnded) {
        active = false;
        worldViewEndGesture();
    }
    return true;
}

bool touchControlsHandleGesture(const Gesture* gesture)
{
    if (!touchControlsIsEnabled()) {
        return false;
    }

    if (gRadialMenu.open) {
        if (radialMenuHandleGesture(gesture)) {
            return true;
        }
    }

    switch (gesture->type) {
    case kPan:
        if (gesture->numberOfTouches == 2) {
            return touchControlsHandlePinch(gesture);
        }

        if (gesture->numberOfTouches != 1) {
            return false;
        }

        if (gesture->state == kBegan) {
            gPanActive = worldViewIsWorldAt(gesture->startX, gesture->startY) && touchControlsViewMayMove();
            if (!gPanActive) {
                return false;
            }

            // Count movement from the point where the finger touched the
            // screen, so the map doesn't lag behind the finger.
            gPanPrevX = gesture->startX;
            gPanPrevY = gesture->startY;
        }

        if (!gPanActive) {
            return false;
        }

        touchControlsMoveView(static_cast<float>(gesture->x - gPanPrevX), static_cast<float>(gesture->y - gPanPrevY), 1.0f, 0.0f, 0.0f);
        gPanPrevX = gesture->x;
        gPanPrevY = gesture->y;

        if (gesture->state == kEnded) {
            gPanActive = false;
        }
        return true;
    case kTap:
        if (gesture->numberOfTouches != 1) {
            return false;
        }

        if (!worldViewIsWorldAt(gesture->x, gesture->y) || !gameMouseIsMapInputEnabled()) {
            return false;
        }

        touchControlsSetLastPoint(gesture->x, gesture->y);
        if (touchControlsIsNative()) {
            touchControlsNotifyScripts(true);
            touchControlsNotifyScripts(false);
        }

        touchControlsHandleTap(gesture->x, gesture->y);
        return true;
    case kLongPress:
        if (gesture->numberOfTouches != 1) {
            return false;
        }

        if (gesture->state == kBegan) {
            gLongPressActive = worldViewIsWorldAt(gesture->x, gesture->y) && gameMouseIsMapInputEnabled();
            if (gLongPressActive) {
                touchControlsSetLastPoint(gesture->x, gesture->y);
                if (touchControlsIsNative()) {
                    touchControlsNotifyScripts(true);
                }

                // Long press on the ground does nothing.
                radialMenuOpen(gesture->x, gesture->y);
            }
            return gLongPressActive;
        }

        if (!gLongPressActive) {
            return false;
        }

        if (gesture->state == kEnded) {
            gLongPressActive = false;
        }
        return true;
    default:
        break;
    }

    return false;
}

static TouchControlsDragHandler* gDragHandler = nullptr;
static bool gDragHandlerActive = false;

void touchControlsSetDragHandler(TouchControlsDragHandler* handler)
{
    gDragHandler = handler;
    gDragHandlerActive = false;
}

bool touchControlsHandleUiDrag(const Gesture* gesture, int dx, int dy)
{
    if (!touchControlsIsEnabled() || gDragHandler == nullptr) {
        return false;
    }

    if (gesture->state == kBegan) {
        gDragHandlerActive = gDragHandler(gesture->startX, gesture->startY, 0, 0, true);

        // Movement before the drag was recognized.
        dx = gesture->x - gesture->startX;
        dy = gesture->y - gesture->startY;
    }

    if (!gDragHandlerActive) {
        return false;
    }

    gDragHandler(gesture->x, gesture->y, dx, dy, false);

    if (gesture->state == kEnded) {
        gDragHandlerActive = false;
    }

    return true;
}

bool touchControlsGetRadialMenuItemPosition(int menuItem, int* x, int* y)
{
    if (!gRadialMenu.open) {
        return false;
    }

    for (int index = 0; index < gRadialMenu.itemsLength; index++) {
        if (gRadialMenu.items[index] == menuItem) {
            *x = gRadialMenu.itemX[index];
            *y = gRadialMenu.itemY[index];
            return true;
        }
    }

    return false;
}

// Moves (hidden) mouse cursor, which is used by the game mouse code to find
// tile and objects to act upon.
static void touchControlsSetGameMouseMode(GameMouseMode mode)
{
    if (gameMouseGetMode() != mode) {
        gameMouseSetMode(mode);
    }
}

static void touchControlsClearSelection()
{
    gPendingAction = TOUCH_ACTION_NONE;
    gPendingTarget = nullptr;
    gPendingTile = -1;
    mapHintsClearSelection();
}

// Decides what tap at a map point should do. Objects are the ones the mouse
// gets in arrow mode (`playerPrimaryTargetAt`), except that walls and
// decorations are walked to (look is in the radial menu) and enemies in combat
// are attacked.
static TouchAction touchControlsGetActionAt(int worldX, int worldY, Object** targetPtr)
{
    return touchControlsGetActionFor(playerPrimaryTargetAt(worldX, worldY, gElevation), targetPtr);
}

// What a tap on [target] does (nullptr - the ground: a walk).
static TouchAction touchControlsGetActionFor(Object* target, Object** targetPtr)
{
    *targetPtr = nullptr;

    if (target == nullptr) {
        return TOUCH_ACTION_MOVE;
    }

    switch (FrmId(target).objectType()) {
    case OBJ_TYPE_CRITTER:
        if (target == gDude) {
            return TOUCH_ACTION_NONE;
        }

        *targetPtr = target;

        if (isInCombat() && !critterIsDead(target) && !objectIsPartyMember(target)) {
            return TOUCH_ACTION_ATTACK;
        }

        return TOUCH_ACTION_USE;
    case OBJ_TYPE_ITEM:
        *targetPtr = target;
        return TOUCH_ACTION_USE;
    case OBJ_TYPE_SCENERY:
        if (_obj_action_can_use(target)) {
            *targetPtr = target;
            return TOUCH_ACTION_USE;
        }
        return TOUCH_ACTION_MOVE;
    default:
        return TOUCH_ACTION_MOVE;
    }
}

// The tactical view picks a critter by the tile it stands on (the figures,
// see-through, cover other tiles); else the object the game would act on
// without it, [object] (items, ladders, doors, bodies: #3, nothing could be
// picked up in combat), a standing critter's figure aside.
static Object* touchControlsTacticalPick(int worldX, int worldY, Object* object)
{
    Object* critter = tacticalViewCritterAt(tileFromScreenXY(worldX, worldY));
    if (critter != nullptr) {
        return critter;
    }

    if (object != nullptr && FrmId(object).objectType() == OBJ_TYPE_CRITTER && !critterIsDead(object)) {
        return nullptr;
    }

    return object;
}

static void touchControlsHandleTap(int x, int y)
{
    int worldX;
    int worldY;
    worldViewScreenToWorld(x, y, &worldX, &worldY);

    // Skill or item waiting for a target (chosen in skilldex, HUD or
    // inventory): tap on an object uses it on the object, elsewhere cancels,
    // as the game's mouse code does.
    // The tactical view picks tiles: a critter by the tile it stands on.
    bool tactical = tacticalViewIsShown();
    int mode = gameMouseGetMode();
    if (mode >= GAME_MOUSE_MODE_USE_CROSSHAIR) {
        touchControlsClearSelection();
        Object* object = playerObjectAt(worldX, worldY, OBJ_TYPE_INVALID, true, gElevation);
        if (tactical) {
            object = touchControlsTacticalPick(worldX, worldY, object);
        }
        mapHintsSetDestination(-1);
        mapHintsSetInteraction(object);
        if (mode == GAME_MOUSE_MODE_USE_CROSSHAIR) {
            playerUseActiveItemOn(object);
            gameMouseSetMode(GAME_MOUSE_MODE_MOVE);
        } else if (object == nullptr || playerUseSkillOn(gameMouseGetModeSkill(mode), object) != -1) {
            gameMouseSetMode(GAME_MOUSE_MODE_MOVE);
        }
        return;
    }

    Object* target;
    TouchAction action;
    int tile = tileFromScreenXY(worldX, worldY);
    if (tactical) {
        action = touchControlsGetActionFor(touchControlsTacticalPick(worldX, worldY, playerPrimaryTargetAt(worldX, worldY, gElevation)), &target);
        touchControlsSnapToItem(x, y, &action, &target);
    } else {
        action = touchControlsGetActionAt(worldX, worldY, &target);
        touchControlsSnapTap(x, y, &tile, &action, &target);
        if (isInCombat()) {
            touchControlsStickToSelection(x, y, worldX, worldY, &tile, &action, &target);
        }
    }

    if (!isInCombat()) {
        touchControlsClearSelection();
        touchControlsPerform(action, tile, target);
        return;
    }

    // In combat actions cost action points, so the first tap only selects
    // (hit chance or move cost shows up) and the second one confirms.
    bool confirmed = action != TOUCH_ACTION_NONE
        && action == gPendingAction
        && (action == TOUCH_ACTION_MOVE ? tile == gPendingTile : target == gPendingTarget);

    if (confirmed) {
        touchControlsPerform(action, tile, target);

        // Selected enemy stays selected, so it can be attacked again with a
        // single tap.
        if (action != TOUCH_ACTION_ATTACK) {
            touchControlsClearSelection();
        }
        return;
    }

    gPendingAction = action;
    gPendingTarget = target;
    gPendingTile = tile;

    // Game mouse mode stays the game's targeting state (combat outlines of
    // critters in sight, cursor mode for scripts); nothing is drawn for it.
    switch (action) {
    case TOUCH_ACTION_ATTACK:
        touchControlsSetGameMouseMode(GAME_MOUSE_MODE_CROSSHAIR);
        mapHintsSetAttackTarget(target);
        if (settings.preferences.combat_looks) {
            playerLook(target);
        }
        break;
    case TOUCH_ACTION_USE:
        touchControlsSetGameMouseMode(GAME_MOUSE_MODE_ARROW);
        mapHintsSetUseTarget(target);
        break;
    case TOUCH_ACTION_MOVE:
        touchControlsSetGameMouseMode(GAME_MOUSE_MODE_MOVE);
        mapHintsSetMoveTile(tile);
        break;
    default:
        touchControlsSetGameMouseMode(GAME_MOUSE_MODE_MOVE);
        mapHintsClearSelection();
        break;
    }
}

static void touchControlsGetTileScreenCenter(int tile, int* x, int* y)
{
    int worldX;
    int worldY;
    tileToScreenXY(tile, &worldX, &worldY);
    worldViewWorldToScreen(worldX + 16, worldY + 8, x, y);
}

static float touchControlsDistanceToRect(int x, int y, const Rect& rect)
{
    int dx = std::max({ rect.left - x, 0, x - rect.right });
    int dy = std::max({ rect.top - y, 0, y - rect.bottom });
    return sqrtf(static_cast<float>(dx * dx + dy * dy));
}

// Changes tap at screen point ([x], [y]) on the ground to the item or tile it
// most likely means (see magnets above). Taps on objects are not changed.
static void touchControlsSnapTap(int x, int y, int* tile, TouchAction* action, Object** target)
{
    if (*action != TOUCH_ACTION_MOVE) {
        return;
    }

    if (isInCombat()) {
        Object* enemy = touchControlsFindEnemyNear(x, y, *tile);
        if (enemy != nullptr) {
            *action = TOUCH_ACTION_ATTACK;
            *target = enemy;
            return;
        }
    }

    if (touchControlsSnapToItem(x, y, action, target)) {
        return;
    }

    int previousTile = isInCombat()
        ? (gPendingAction == TOUCH_ACTION_MOVE ? gPendingTile : -1)
        : mapHintsGetDestination();
    if (previousTile != -1 && previousTile != *tile) {
        int tileX;
        int tileY;
        touchControlsGetTileScreenCenter(previousTile, &tileX, &tileY);
        float radius = kTileSnapRadiusDp * hudGetPixelsPerDp();
        float dx = static_cast<float>(tileX - x);
        float dy = static_cast<float>(tileY - y);
        if (dx * dx + dy * dy <= radius * radius) {
            *tile = previousTile;
        }
    }
}

// Item magnet (with highlight on, see magnets above): changes tap at screen
// point ([x], [y]) on the ground to the item near it.
static bool touchControlsSnapToItem(int x, int y, TouchAction* action, Object** target)
{
    if (*action != TOUCH_ACTION_MOVE || !touchHudIsHighlightActive()) {
        return false;
    }

    Object* item = touchControlsFindItemNear(x, y);
    if (item == nullptr) {
        return false;
    }

    *action = TOUCH_ACTION_USE;
    *target = item;
    return true;
}

void touchControlsGetSelection(int* action, int* tile, Object** target)
{
    *action = gPendingAction;
    *tile = gPendingTile;
    *target = gPendingTarget;
}

// In combat a tap near the selection (tile or enemy) confirms it, whatever
// else is drawn under the finger (see magnets above).
static void touchControlsStickToSelection(int x, int y, int worldX, int worldY, int* tile, TouchAction* action, Object** target)
{
    int fingerTile = tileFromScreenXY(worldX, worldY);
    if (gPendingAction == TOUCH_ACTION_MOVE && gPendingTile != -1) {
        // A critter's own tile picks the critter (to switch to it).
        if (*action == TOUCH_ACTION_ATTACK && *target != nullptr && (*target)->tile == fingerTile) {
            return;
        }

        bool close = fingerTile == gPendingTile;
        if (!close) {
            int tileX;
            int tileY;
            touchControlsGetTileScreenCenter(gPendingTile, &tileX, &tileY);
            float radius = kTileSnapRadiusDp * hudGetPixelsPerDp();
            float dx = static_cast<float>(tileX - x);
            float dy = static_cast<float>(tileY - y);
            close = dx * dx + dy * dy <= radius * radius;
        }
        if (close) {
            *action = TOUCH_ACTION_MOVE;
            *tile = gPendingTile;
            *target = nullptr;
        }
        return;
    }

    if (gPendingAction == TOUCH_ACTION_ATTACK && *target != gPendingTarget && gPendingTarget != nullptr
        && touchControlsObjectExists(gPendingTarget) && !critterIsDead(gPendingTarget)
        && _obj_intersects_with(gPendingTarget, worldX, worldY) != OBJECT_NONE) {
        *action = TOUCH_ACTION_ATTACK;
        *target = gPendingTarget;
    }
}

// Nearest item on the ground whose sprite box on screen is within magnet
// radius. Box, not pixels: thin items (spear, knife) are hard to hit. Only
// items showing an outline (the highlight's): what the highlight leaves out
// (out of sight, behind walls) the finger doesn't jump to.
static Object* touchControlsFindItemNear(int x, int y)
{
    Object* nearest = nullptr;
    float nearestDistance = kItemSnapRadiusDp * hudGetPixelsPerDp();
    for (Object* object = objectFindFirstAtElevation(gElevation); object != nullptr; object = objectFindNextAtElevation()) {
        if (FrmId(object).objectType() != OBJ_TYPE_ITEM || (object->flags & OBJECT_HIDDEN) != 0 || !objectHasVisibleOutline(object)) {
            continue;
        }

        Rect worldRect;
        objectGetRect(object, &worldRect);

        Rect rect;
        worldViewWorldToScreen(worldRect.left, worldRect.top, &(rect.left), &(rect.top));
        worldViewWorldToScreen(worldRect.right, worldRect.bottom, &(rect.right), &(rect.bottom));

        float distance = touchControlsDistanceToRect(x, y, rect);
        if (distance <= nearestDistance) {
            nearest = object;
            nearestDistance = distance;
        }
    }

    return nearest;
}

// Screen rect of [critter]'s body column (see magnets above): from the top
// of its sprite to just below its feet, narrower than a tile.
static bool touchControlsGetBodyColumn(Object* critter, Rect* rect)
{
    int footX;
    int footY;
    if (tileToScreenXY(critter->tile, &footX, &footY) != 0) {
        return false;
    }
    footX += 16;
    footY += 8;

    Rect worldRect;
    objectGetRect(critter, &worldRect);
    worldViewWorldToScreen(footX - kEnemyBodyHalfWidth, worldRect.top, &(rect->left), &(rect->top));
    worldViewWorldToScreen(footX + kEnemyBodyHalfWidth, footY + kEnemyBodyBelowFeet, &(rect->right), &(rect->bottom));
    return true;
}

// Enemy (as `touchControlsGetActionAt` attacks) standing on [tile] or whose
// body column (see magnets above) contains ([x], [y]).
static Object* touchControlsFindEnemyNear(int x, int y, int tile)
{
    Object* nearest = nullptr;
    int nearestDistance = 0;
    for (Object* object = objectFindFirstAtElevation(gElevation); object != nullptr; object = objectFindNextAtElevation()) {
        if (FrmId(object).objectType() != OBJ_TYPE_CRITTER
            || object == gDude
            || (object->flags & OBJECT_HIDDEN) != 0
            || critterIsDead(object)
            || objectIsPartyMember(object)) {
            continue;
        }

        int distance = 0;
        if (object->tile != tile) {
            Rect body;
            if (!touchControlsGetBodyColumn(object, &body)
                || x < body.left || x > body.right || y < body.top || y > body.bottom) {
                continue;
            }
            distance = std::abs(x - (body.left + body.right) / 2);
        }

        if (nearest == nullptr || distance < nearestDistance) {
            nearest = object;
            nearestDistance = distance;
        }
    }

    return nearest;
}

static bool touchControlsObjectExists(Object* object)
{
    for (Object* candidate = objectFindFirstAtElevation(gElevation); candidate != nullptr; candidate = objectFindNextAtElevation()) {
        if (candidate == object) {
            return true;
        }
    }
    return false;
}

static void touchControlsPerform(TouchAction action, int tile, Object* target)
{
    static const char* const kActionNames[] = { "nothing", "move", "use", "attack" };
    actionLog("map tap: %s, tile %d%s", kActionNames[action], tile, actionLogObject(target));

    // A new command replaces the walk to the marked tile.
    if (action != TOUCH_ACTION_MOVE) {
        mapHintsSetDestination(-1);
    }

    switch (action) {
    case TOUCH_ACTION_MOVE:
        mapHintsSetInteraction(nullptr);
        touchControlsSetGameMouseMode(GAME_MOUSE_MODE_MOVE);
        // Tapping the same tile again runs (see `dudeMoveToTile`).
        if (playerMoveTo(tile, false) == 0) {
            mapHintsSetDestination(tile);
        }
        break;
    case TOUCH_ACTION_USE:
        mapHintsSetInteraction(target);
        playerPrimaryAction(target);
        touchControlsSetGameMouseMode(GAME_MOUSE_MODE_MOVE);
        break;
    case TOUCH_ACTION_ATTACK:
        mapHintsSetInteraction(nullptr);
        touchControlsSetGameMouseMode(GAME_MOUSE_MODE_CROSSHAIR);
        playerAttack(target);
        break;
    default:
        mapHintsSetInteraction(nullptr);
        touchControlsSetGameMouseMode(GAME_MOUSE_MODE_MOVE);
        break;
    }
}

static bool radialMenuOpen(int x, int y)
{
    radialMenuClose();

    int worldX;
    int worldY;
    worldViewScreenToWorld(x, y, &worldX, &worldY);

    // The tactical view: the menu of the critter standing on the tile (else
    // of the object, as without it).
    bool tactical = tacticalViewIsShown();
    Object* target = playerObjectAt(worldX, worldY, OBJ_TYPE_INVALID, true, gElevation);
    if (tactical) {
        target = touchControlsTacticalPick(worldX, worldY, target);
    }

    // Item magnet (with highlight on), as for taps.
    Object* actionTarget;
    TouchAction action = tactical
        ? touchControlsGetActionFor(touchControlsTacticalPick(worldX, worldY, playerPrimaryTargetAt(worldX, worldY, gElevation)), &actionTarget)
        : touchControlsGetActionAt(worldX, worldY, &actionTarget);
    if (touchControlsSnapToItem(x, y, &action, &actionTarget)) {
        target = actionTarget;
    }

    if (target == nullptr) {
        return false;
    }

    GameMouseActionMenuItem items[GAME_MOUSE_ACTION_MENU_ITEM_COUNT - 1];
    int itemsLength = gameMouseBuildActionMenuItems(target, items);
    if (!radialMenuOpenItems(items, itemsLength, x, y)) {
        return false;
    }

    gRadialMenu.target = target;
    return true;
}

// Opens radial menu with action menu items around the point. Cancel item is
// left out (tapping outside of the menu cancels it).
static bool radialMenuOpenItems(const GameMouseActionMenuItem* items, int itemsLength, int x, int y)
{
    radialMenuClose();

    gRadialMenu.itemsLength = 0;
    for (int index = 0; index < itemsLength && gRadialMenu.itemsLength < kRadialMenuMaxItems; index++) {
        if (items[index] != GAME_MOUSE_ACTION_MENU_ITEM_CANCEL) {
            gRadialMenu.items[gRadialMenu.itemsLength] = items[index];
            gRadialMenu.itemSourceIndex[gRadialMenu.itemsLength] = index;
            gRadialMenu.itemsLength++;
        }
    }

    if (gRadialMenu.itemsLength == 0) {
        return false;
    }

    // Buttons are laid out on a circle around the finger, clockwise from the
    // top, with some space between them. Near screen edges they are spread
    // over the part of the circle that fits on screen (e.g. the lower arc at
    // the top edge), so the menu stays centered on the finger. Sizes are in
    // dp (physical size).
    float pixelsPerDp = hudGetPixelsPerDp();
    int buttonSize = static_cast<int>(lround(kRadialMenuButtonSizeDp * pixelsPerDp));
    double minRadius = kRadialMenuMinRadiusDp * pixelsPerDp;
    int count = gRadialMenu.itemsLength;

    HudMetrics metrics = hudMetricsGet();
    double margin = 4.0 * pixelsPerDp + buttonSize / 2.0;
    double left = metrics.insetLeft + margin;
    double top = metrics.insetTop + margin;
    double right = screenGetWidth() - metrics.insetRight - margin;
    double bottom = screenGetHeight() - metrics.insetBottom - margin;

    // Angle between neighbour buttons which keeps them apart.
    auto minStep = [&](double radius) {
        return 2.0 * asin(std::min(buttonSize * 1.12 / (2.0 * radius), 1.0));
    };

    auto fits = [&](double centerX, double centerY, double radius, double angle) {
        double pointX = centerX + radius * cos(angle);
        double pointY = centerY + radius * sin(angle);
        return pointX >= left && pointX <= right && pointY >= top && pointY <= bottom;
    };

    constexpr int kSamples = 180;
    const double sampleStep = 2.0 * M_PI / kSamples;

    int centerX = x;
    int centerY = y;
    bool placed = false;
    double radius = minRadius;
    double arcStart = 0.0;
    double arcLength = 0.0;
    bool fullCircle = false;

    // Grow radius a little if the free arc is too short.
    for (double tryRadius = minRadius; tryRadius <= minRadius * 1.6 && !placed; tryRadius += 4.0 * pixelsPerDp) {
        double full = std::max(tryRadius, count > 1 ? buttonSize * 1.12 / (2.0 * sin(M_PI / count)) : 0.0);

        // Longest run of angles (from the top, clockwise) with buttons on
        // screen.
        bool fitsAll = true;
        bool sampleFits[kSamples];
        for (int sample = 0; sample < kSamples; sample++) {
            sampleFits[sample] = fits(x, y, tryRadius, -M_PI / 2 + sample * sampleStep);
            fitsAll = fitsAll && sampleFits[sample] && fits(x, y, full, -M_PI / 2 + sample * sampleStep);
        }

        if (fitsAll) {
            radius = full;
            fullCircle = true;
            placed = true;
            break;
        }

        int bestStart = -1;
        int bestLength = 0;
        for (int sample = 0; sample < kSamples; sample++) {
            if (!sampleFits[sample] || sampleFits[(sample + kSamples - 1) % kSamples]) {
                continue;
            }
            int length = 0;
            while (length < kSamples && sampleFits[(sample + length) % kSamples]) {
                length++;
            }
            if (length > bestLength) {
                bestStart = sample;
                bestLength = length;
            }
        }

        double length = (bestLength - 1) * sampleStep;
        if (bestStart != -1 && (count == 1 || length >= (count - 1) * minStep(tryRadius))) {
            radius = tryRadius;
            arcStart = -M_PI / 2 + bestStart * sampleStep;
            arcLength = length;
            placed = true;
        }
    }

    if (!placed) {
        // Corner with many actions: move the menu off the edge.
        radius = std::max(minRadius, count > 1 ? buttonSize * 1.12 / (2.0 * sin(M_PI / count)) : 0.0);
        int extent = static_cast<int>(ceil(radius)) + buttonSize / 2 + 2;
        centerX = std::clamp(x, extent, std::max(screenGetWidth() - extent, extent));
        centerY = std::clamp(y, extent, std::max(screenGetHeight() - extent, extent));
        fullCircle = true;
    }

    for (int index = 0; index < count; index++) {
        double angle;
        if (fullCircle) {
            angle = -M_PI / 2 + 2.0 * M_PI * index / count;
        } else if (count == 1) {
            angle = arcStart + arcLength / 2.0;
        } else {
            // Compact group in the middle of the free arc.
            double step = std::min(arcLength / (count - 1), std::max(minStep(radius) * 1.15, 2.0 * M_PI / (count + 2)));
            step = std::min(step, arcLength / (count - 1));
            double groupStart = arcStart + (arcLength - step * (count - 1)) / 2.0;
            angle = groupStart + step * index;
        }

        gRadialMenu.itemX[index] = centerX + static_cast<int>(lround(radius * cos(angle)));
        gRadialMenu.itemY[index] = centerY + static_cast<int>(lround(radius * sin(angle)));
    }

    gRadialMenu.itemSize = buttonSize;
    gRadialMenu.centerX = centerX;
    gRadialMenu.centerY = centerY;
    gRadialMenu.open = true;
    gRadialMenu.target = nullptr;
    gRadialMenu.anchorX = x;
    gRadialMenu.anchorY = y;
    gRadialMenu.highlightedItem = -1;
    gRadialMenu.fingerMoved = false;

    muiPush(&gRadialMenuScreen);

    return true;
}

int touchControlsChooseActionMenuItem(const GameMouseActionMenuItem* items, int itemsLength, int x, int y)
{
    if (!radialMenuOpenItems(items, itemsLength, x, y)) {
        return -1;
    }

    // Menu is opened by long press, the same finger may choose by sliding.
    // Over a mobile UI screen the finger is the mobile UI's.
    bool pointerInput = muiHasModal();
    gRadialMenuScreen.pointerInput = pointerInput;
    gRadialMenuScreen.pointerWasDown = true;
    gRadialMenuScreen.modal = pointerInput;
    gLongPressActive = !pointerInput;
    gRadialMenu.modal = true;
    gRadialMenu.result = -1;

    while (gRadialMenu.open) {
        sharedFpsLimiter.mark();

        inputGetInput();

        devAutotestTick();

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            radialMenuClose();
            break;
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    gRadialMenu.modal = false;
    gLongPressActive = false;
    gRadialMenuScreen.pointerInput = false;
    gRadialMenuScreen.modal = false;

    return gRadialMenu.result;
}

static void radialMenuClose()
{
    if (gRadialMenu.open) {
        muiRemove(&gRadialMenuScreen);
        gRadialMenu.open = false;
    }

    gRadialMenu.target = nullptr;
    gRadialMenu.itemsLength = 0;
    gRadialMenu.highlightedItem = -1;
}

static MuiIcon radialMenuIcon(int menuItem)
{
    switch (menuItem) {
    case GAME_MOUSE_ACTION_MENU_ITEM_DROP:
        return MuiIcon::Drop;
    case GAME_MOUSE_ACTION_MENU_ITEM_INVENTORY:
        return MuiIcon::Inventory;
    case GAME_MOUSE_ACTION_MENU_ITEM_LOOK:
        return MuiIcon::Look;
    case GAME_MOUSE_ACTION_MENU_ITEM_ROTATE:
        return MuiIcon::Rotate;
    case GAME_MOUSE_ACTION_MENU_ITEM_TALK:
        return MuiIcon::Talk;
    case GAME_MOUSE_ACTION_MENU_ITEM_USE:
        return MuiIcon::Use;
    case GAME_MOUSE_ACTION_MENU_ITEM_UNLOAD:
        return MuiIcon::Unload;
    case GAME_MOUSE_ACTION_MENU_ITEM_USE_SKILL:
        return MuiIcon::Skills;
    case GAME_MOUSE_ACTION_MENU_ITEM_PUSH:
        return MuiIcon::Push;
    default:
        return MuiIcon::Cancel;
    }
}

void RadialMenuScreen::build(MuiContext& ui)
{
    if (!gRadialMenu.open) {
        return;
    }

    const MuiTheme& theme = muiTheme();

    // Screen (logical) to output pixels.
    float scale = ui.screenRect().w / screenGetWidth();
    float appear = ui.appear();
    float centerX = gRadialMenu.centerX * scale;
    float centerY = gRadialMenu.centerY * scale;
    float radius = gRadialMenu.itemSize * scale / 2.0f;

    // Buttons fly out of the center while appearing.
    float spread = 0.6f + 0.4f * appear;
    Uint8 alpha = static_cast<Uint8>(255 * std::min(appear * 1.5f, 1.0f));

    if (pointerInput) {
        handlePointer(ui, scale);
        if (!gRadialMenu.open) {
            return;
        }
    }

    muiFillCircle(centerX, centerY, ui.dp(6.0f), theme.accent.withAlpha(static_cast<Uint8>(alpha * 0.8f)));

    int highlightedItem = -1;
    float highlightedX = 0.0f;
    float highlightedY = 0.0f;
    float highlightedRadius = 0.0f;

    for (int index = 0; index < gRadialMenu.itemsLength; index++) {
        bool highlighted = index == gRadialMenu.highlightedItem;
        float x = centerX + (gRadialMenu.itemX[index] * scale - centerX) * spread;
        float y = centerY + (gRadialMenu.itemY[index] * scale - centerY) * spread;
        float buttonRadius = highlighted ? radius * 1.1f : radius;

        MuiColor fill = highlighted ? theme.buttonPressed : theme.button;
        muiFillCircle(x, y, buttonRadius, fill.withAlpha(static_cast<Uint8>(fill.a * alpha / 255)));
        muiStrokeCircle(x, y, buttonRadius, ui.dp(highlighted ? 2.5f : theme.borderWidth), (highlighted ? theme.accent : theme.buttonBorder).withAlpha(alpha));
        ui.mark("radial." + std::to_string(gRadialMenu.items[index]), { x - buttonRadius, y - buttonRadius, buttonRadius * 2.0f, buttonRadius * 2.0f });

        MuiColor iconColor = highlighted ? theme.buttonText : theme.accent;
        muiDrawIcon(radialMenuIcon(gRadialMenu.items[index]), x, y, buttonRadius * 0.9f, iconColor.withAlpha(alpha));

        if (highlighted) {
            highlightedX = x;
            highlightedY = y;
            highlightedRadius = buttonRadius;
            highlightedItem = gRadialMenu.items[index];
        }
    }

    // Name of the action under the finger, below the button (above it at the
    // bottom edge).
    if (highlightedItem >= 0 && highlightedItem < GAME_MOUSE_ACTION_MENU_ITEM_COUNT) {
        std::u32string name = muiDecodeGameText(muiText(kActionNameMessageBase + highlightedItem, kActionNameFallbacks[highlightedItem]));
        float size = ui.dp(14.0f);
        float padding = ui.dp(8.0f);
        float width = muiTextWidth(name, size) + padding * 2.0f;
        float height = muiLineHeight(size) + ui.dp(6.0f);
        float gap = ui.dp(6.0f);
        float y = highlightedY + highlightedRadius + gap;
        if (y + height > ui.screenRect().bottom()) {
            y = highlightedY - highlightedRadius - gap - height;
        }
        float x = std::clamp(highlightedX - width / 2.0f, 0.0f, ui.screenRect().w - width);
        MuiRect label = { x, y, width, height };
        muiFillRoundRect(label, ui.dp(6.0f), theme.panel.withAlpha(alpha));
        muiStrokeRoundRect(label, ui.dp(6.0f), ui.dp(1.0f), theme.accent.withAlpha(alpha));
        muiDrawTextAligned(name, label, size, theme.text.withAlpha(alpha), MuiAlign::Center, MuiAlign::Center);
    }
}

// Menu over a mobile UI screen: the finger which opened it may slide to a
// button and lift there; lifted without sliding the menu stays, then a tap
// chooses a button or closes the menu.
void RadialMenuScreen::handlePointer(MuiContext& ui, float scale)
{
    if (!ui.isInteractive) {
        return;
    }

    int x = static_cast<int>(ui.pointerX() / scale);
    int y = static_cast<int>(ui.pointerY() / scale);
    bool down = ui.pointerDown();

    if (down) {
        if (!pointerWasDown) {
            // New touch after the menu stayed: buttons respond at once.
            gRadialMenu.fingerMoved = true;
        } else {
            float slide = kRadialMenuSlideStartDp * hudGetPixelsPerDp();
            if (std::abs(x - gRadialMenu.anchorX) > slide || std::abs(y - gRadialMenu.anchorY) > slide) {
                gRadialMenu.fingerMoved = true;
            }
        }
        radialMenuHighlight(gRadialMenu.fingerMoved ? radialMenuHitTest(x, y) : -1);
    } else if (pointerWasDown) {
        int index = gRadialMenu.fingerMoved ? radialMenuHitTest(x, y) : -1;
        if (index != -1) {
            radialMenuSelect(index);
        } else if (gRadialMenu.fingerMoved) {
            radialMenuClose();
        }
    }

    pointerWasDown = down;
}

static int radialMenuHitTest(int x, int y)
{
    float hitRadius = gRadialMenu.itemSize / 2.0f + kRadialMenuHitSlopDp * hudGetPixelsPerDp();

    int nearest = -1;
    float nearestDistance = hitRadius * hitRadius;
    for (int index = 0; index < gRadialMenu.itemsLength; index++) {
        float dx = static_cast<float>(x - gRadialMenu.itemX[index]);
        float dy = static_cast<float>(y - gRadialMenu.itemY[index]);
        float distance = dx * dx + dy * dy;
        if (distance <= nearestDistance) {
            nearest = index;
            nearestDistance = distance;
        }
    }

    return nearest;
}

static void radialMenuHighlight(int index)
{
    gRadialMenu.highlightedItem = index;
}

static void radialMenuSelect(int index)
{
    if (gRadialMenu.modal) {
        gRadialMenu.result = gRadialMenu.itemSourceIndex[index];
        radialMenuClose();
        return;
    }

    Object* target = gRadialMenu.target;
    GameMouseActionMenuItem menuItem = gRadialMenu.items[index];

    radialMenuClose();

    // The object may be gone while the menu was open.
    if (!touchControlsObjectExists(target)) {
        return;
    }

    // Actions on the object (look and turning dude aren't).
    if (menuItem != GAME_MOUSE_ACTION_MENU_ITEM_LOOK
        && menuItem != GAME_MOUSE_ACTION_MENU_ITEM_ROTATE
        && menuItem != GAME_MOUSE_ACTION_MENU_ITEM_CANCEL) {
        mapHintsSetDestination(-1);
        mapHintsSetInteraction(target);
    }

    actionLog("map menu: %s%s", menuItem >= 0 && menuItem < GAME_MOUSE_ACTION_MENU_ITEM_COUNT ? kActionNameFallbacks[menuItem] : "?",
        actionLogObject(target));
    gameMouseExecuteActionMenuItem(target, menuItem);
}

// Handles gestures while radial menu is open, returns true if the gesture was
// consumed.
static bool radialMenuHandleGesture(const Gesture* gesture)
{
    switch (gesture->type) {
    case kLongPress:
        if (gLongPressActive) {
            // The finger which opened the menu slides over its buttons,
            // lifting it over a button selects it.
            float slide = kRadialMenuSlideStartDp * hudGetPixelsPerDp();
            if (std::abs(gesture->x - gRadialMenu.anchorX) > slide || std::abs(gesture->y - gRadialMenu.anchorY) > slide) {
                gRadialMenu.fingerMoved = true;
            }

            int index = gRadialMenu.fingerMoved ? radialMenuHitTest(gesture->x, gesture->y) : -1;
            if (gesture->state == kEnded) {
                gLongPressActive = false;
                if (index != -1) {
                    radialMenuSelect(index);
                }
            } else {
                radialMenuHighlight(index);
            }
            return true;
        }

        radialMenuClose();
        return false;
    case kTap: {
        int index = radialMenuHitTest(gesture->x, gesture->y);
        if (index != -1) {
            radialMenuSelect(index);
        } else {
            radialMenuClose();
        }
        return true;
    }
    default:
        // Anything else (moving the view) closes the menu and is handled as
        // usual.
        radialMenuClose();
        return false;
    }
}

} // namespace fallout
