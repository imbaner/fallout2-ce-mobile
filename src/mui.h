#ifndef FALLOUT_MUI_H_
#define FALLOUT_MUI_H_

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL.h>

#include "art_defs.h"
#include "combat_defs.h"
#include "mui_draw.h"
#include "obj_types.h"
#include "touch.h"

namespace fallout {

// Mobile UI: touch-first interface drawn by the GPU at the screen's native
// resolution over the game (world and original 8-bit windows), sized in dp
// like the touch HUD.
//
// Screens are immediate mode: every frame `MuiScreen::build` lays out, draws
// and handles input of its widgets through `MuiContext`. Screens are kept in
// a stack, the top one gets input. Modal screens take every touch, others
// only touches on their widgets (the rest goes to the game).
//
// Look (colors, sizes) comes from `MuiTheme`, so the visual style can be
// replaced later without touching screens.

// `[touch] mobile_ui` setting. Always on in the touch-only build (touch.h):
// a constant there, so the game's windows the mobile UI replaced are left
// out of it (the linker drops what only they use).
#if FALLOUT_TOUCH_ONLY
constexpr bool muiIsEnabled() { return true; }
#else
bool muiIsEnabled();
#endif

struct MuiTheme {
    MuiColor dim;
    MuiColor panel;
    MuiColor panelBorder;
    MuiColor text;
    MuiColor textDim;
    MuiColor accent;
    MuiColor button;
    MuiColor buttonPressed;
    MuiColor buttonBorder;
    MuiColor buttonText;
    MuiColor buttonPrimary;
    MuiColor buttonPrimaryText;

    // dp
    float radius;
    float padding;
    float gap;
    float buttonHeight;
    float borderWidth;
    float titleSize;
    float bodySize;
    float buttonTextSize;
    float touchSlop;
};

const MuiTheme& muiTheme();

enum class MuiButtonStyle {
    Normal,
    Primary,
    AccentBorder,
    Danger,
};

class MuiContext;

// View of content moved and zoomed by fingers (`MuiContext::panZoom`):
// content point at the view's center, output pixels per content unit.
struct MuiPanZoom {
    float centerX = 0.0f;
    float centerY = 0.0f;
    float zoom = 1.0f;
    float minZoom = 0.1f;
    float maxZoom = 10.0f;
};

class MuiScreen {
public:
    virtual ~MuiScreen() = default;
    virtual void build(MuiContext& ui) = 0;

    // Back button of the screen, Android back button, swipe back.
    virtual void back() { }

    // Other keys from game input loop (keyboard).
    virtual void key(int keyCode) { }

    // Inactive screens (e.g. HUD under a game screen) draw nothing and don't
    // take input; the topmost active screen gets input.
    virtual bool isActive() { return true; }

    // The screen is about to show (the game sets up its state), the last
    // shown frame stays instead of the game's windows under it.
    virtual bool holdsFrame() { return false; }

    // Opaque over the whole screen: the map and the game's screen under it
    // aren't drawn while the screen is active.
    virtual bool coversScreen() { return false; }

    // Over the map without covering it, but the HUD's buttons shouldn't show
    // under it (a game screen the game shows over the map: the elevator).
    virtual bool hidesHud() { return false; }

    bool modal = true;
    bool finished = false;

    // Set when pushed, for appear animations.
    unsigned int openTime = 0;
};

class MuiContext {
public:
    float dp(float value) const { return value * pixelsPerDp; }

    // Whole screen and its part outside display cutouts.
    MuiRect screenRect() const { return screen; }
    MuiRect safeRect() const { return safe; }

    // The screen being built gets input.
    bool interactive() const { return isInteractive; }

    // 0..1 progress of the screen's appear animation.
    float appear() const;

    // Darkens everything below (for modal screens).
    void dim();

    void panel(const MuiRect& rect);

    // Generic touchable area: returns true when tapped (finger up inside after
    // going down inside, not moved away), `pressed` - finger is down on it now,
    // `longPressed` - set once when the finger is held still long enough (the
    // tap is not reported then).
    bool touchable(const std::string& id, const MuiRect& rect, bool* pressed = nullptr, bool* longPressed = nullptr);

    bool button(const std::string& id, const MuiRect& rect, const std::u32string& label, MuiButtonStyle style = MuiButtonStyle::Normal);

    // Tap outside of [rect] (e.g. to close a popup).
    bool tappedOutside(const MuiRect& rect);

    // Area catching touches of a non-modal screen.
    void region(const MuiRect& rect);

    // Vertical scroll area over [rect] with content of [contentHeight]:
    // finger drag scrolls, a fling continues with inertia. Widgets inside
    // must use ids starting with "<id>." - they lose their press when the
    // finger starts scrolling. Returns scroll offset (0 - top of content).
    float scroll(const std::string& id, const MuiRect& rect, float contentHeight);
    // The same sideways over content of [contentWidth] (0 - its left end).
    float scrollHorizontal(const std::string& id, const MuiRect& rect, float contentWidth);
    void setScroll(const std::string& id, float offset);
    bool isScrolling(const std::string& id) const;

    // Drag and drop. Call after `touchable` of widget [id]: when the finger
    // holding it moves mostly sideways ([anyDirection] - moves at all, for
    // widgets outside scroll areas), a drag of [payload] starts (vertical
    // moves stay with scroll areas, the tap is not reported). Holding still is
    // a long press (action menu), it never starts a drag. Returns true while
    // this widget is dragged.
    bool dragSource(const std::string& id, int payload, bool anyDirection = false);
    // A drag is in progress, [payload] is set.
    bool dragging(int* payload) const;
    // Dragged payload was released over [rect] (reported once).
    bool dropped(const MuiRect& rect, int* payload);

    // Registers [rect] of widget [id] drawn without `touchable` (automated
    // tests find widgets by id).
    void mark(const std::string& id, const MuiRect& rect);

    // Finger position (output pixels) and state, for custom widgets.
    float pointerX() const;
    float pointerY() const;
    bool pointerDown() const;

    // Content under [rect] (maps): one finger drags it, two fingers pinch
    // it (zoom around them). Returns true while touched. [tapped] - the finger
    // went up where it went down, no pinch (at `pointerX` / `pointerY`).
    bool panZoom(const std::string& id, const MuiRect& rect, MuiPanZoom* view, bool* tapped = nullptr);

    // Text typed since the last call (text input started with
    // `beginTextInput`), taken by the screen reading it.
    std::u32string takeTextInput();

    // Horizontal slider over [rect], returns the value (finger sets it).
    // [dragging] - the finger holds it now.
    float slider(const std::string& id, const MuiRect& rect, float value, float minValue, float maxValue, bool* dragging = nullptr);

    // On / off switch over [rect]: a tap flips [value]. Returns true when it
    // changed.
    bool toggle(const std::string& id, const MuiRect& rect, bool* value);

    // One of [labels] in a row of joined buttons over [rect] (ids
    // "<id>.<index>"): a tap picks it. Returns true when [selected] changed.
    bool segmented(const std::string& id, const MuiRect& rect, const std::vector<std::u32string>& labels, int* selected);

    float scrollAlong(const std::string& id, const MuiRect& rect, float contentSize, bool horizontal);

    // Internal state, filled by mui.cc.
    float pixelsPerDp = 1.0f;
    MuiRect screen;
    MuiRect safe;
    bool isInteractive = false;
    MuiScreen* current = nullptr;
    unsigned int now = 0;
};

// Output pixels to game screen (logical) pixels.
void muiToScreen(float x, float y, int* screenX, int* screenY);

// Text [id] of `game\ce.msg` (mobile UI texts localized with game data),
// [fallback] without it.
const char* muiText(int id, const char* fallback);

void muiPush(MuiScreen* screen);
void muiRemove(MuiScreen* screen);
bool muiHasModal();

// A screen holds the last frame, don't present a new one (see
// `MuiScreen::holdsFrame`).
bool muiHoldsFrame();

// A full screen closes for another one the game opens next (tabs of the game
// screens): the last frame stays until that one covers the screen, so the
// map doesn't show between them. [from] is the screen closing.
void muiHoldFrameForSwitch(MuiScreen* from);

// An active screen covers the whole screen (see `MuiScreen::coversScreen`).
bool muiCoversScreen();

// An active screen hides the HUD (see `MuiScreen::hidesHud`).
bool muiHidesHud();

// Runs game input and rendering until [screen] is finished (synchronous flows
// like dialog boxes). Esc / Android back call `MuiScreen::back`.
void muiRunModal(MuiScreen* screen);

// svga.cc: draws screens over the game.
void muiRender(SDL_Renderer* renderer);
void muiResetRenderer();

// input.cc: returns true if the finger event belongs to the mobile UI.
bool muiHandleFingerEvent(const SDL_Event* event);

// input.cc: forgets fingers not in [down] (the [count] fingers really down
// now): ends that never came.
// Returns true if any was forgotten.
bool muiForgetMissingFingers(const SDL_FingerID* down, int count);

// input.cc: typed text (UTF-8).
void muiHandleTextInput(const char* text);

// Back (Android's back button): at the next frame the topmost active modal
// screen's `back()`, or with none the overlays' (the HUD opens the game's
// menu, as Esc on the map). Screens' Back buttons do the same.
void muiRequestBack();

// Talk screen (mui_game_dialog.cc): registered at game init, shown while
// talking. Back closes its review panel, returns true if it did.
void muiGameDialogInit();
void muiGameDialogExit();
bool muiGameDialogBack();

// Barter screen (mui_barter.cc): registered at game init, shown while the
// game's barter loop runs (trader's replies are notifications, see
// mui_notify.h).
void muiBarterInit();
void muiBarterExit();

// Loot screen (mui_loot.cc): registered when looting opens first, shown
// while the game's loot loop runs.
void muiLootInit();

// "Use item on" panel (mui_use_item.cc, inventory.cc
// `inventoryOpenUseItemOn`): dude's items next to [target], returns the
// chosen one or nullptr.
Object* muiChooseItemToUse(Object* target);

// Inventory screen (mui_inventory.cc): registered when the inventory opens
// first, shown while the game's inventory loop runs.
void muiInventoryInit();

// Quantity picker (inventory.cc `inventoryQuantitySelect`): returns chosen
// value or -1. [timer] shows seconds (explosives). Otherwise it's how many of
// [item]'s stack (ammo: clips, shown as rounds, `itemStackAmount`), shown
// with their weight and [price] (the price of a quantity in a deal, none:
// weight only).
int muiQuantitySelect(Object* item, int minValue, int maxValue, int step, int defaultValue, bool timer, const std::function<int(int)>& price = nullptr);

// Character screen (mui_character.cc) over the character editor's state
// (character_editor.h): level ups in game, or creating the character.
// Returns 0 - done, 1 - cancelled (creation).
int muiCharacterScreenRun();

// Pip-Boy screen (mui_pipboy.cc) over the Pip-Boy's state (pipboy.h);
// [rest] - opened to rest (a bed).
void muiPipboyScreenRun(bool rest);

// Map screen (mui_automap.cc): the automap of the current map (automap.h).
void muiAutomapScreenRun();

// Movie screen (mui_movie.cc): the movie playing into the player's texture
// (`gameMovieStartMobile`) over the whole screen until it ends or a tap or a
// key skips it (the game's rule).
void muiMovieScreenRun();

// Main menu (mui_main_menu.cc, mainmenu.cc): shown while the title menu is.
// The tapped button is taken by the menu's loop as a `MainMenuOption`.
void muiMainMenuShow();
// It faded in: taps count from now (not ones made in the dark before).
void muiMainMenuReady();
void muiMainMenuHide();
// The button tapped since the last call, -1 - none.
int muiMainMenuTakeChoice();

// Premade character selector (mui_character_selector.cc,
// character_selector.cc `characterSelectorOpen`), same results.
int muiCharacterSelectorRun();

// Game menu (mui_options.cc, options.cc `showOptions`): 1 - a game was saved
// or loaded from it, as the game's window.
int muiOptionsScreenRun();

// Settings screen (mui_preferences.cc, preferences.cc `doPreferences`):
// the game's preferences with this port's and CE's settings. [inGame] -
// opened from the game (the map previews brightness), not the main menu.
void muiPreferencesScreenRun(bool inGame);

// Save / load screen (mui_loadsave.cc, loadsave.cc `lsgSaveGame` /
// `lsgLoadGame`), [fromMainMenu] - load only. 1 - saved or loaded, 0 -
// closed, -1 - loading failed.
int muiLoadSaveScreenRun(bool saving, bool fromMainMenu);

// Full-screen scenes (mui_scenes.cc): the death screen (main.cc) and the
// ending slides (endgame.cc). The game's picture, or the frame the game
// composed (screen sized, its palette indices), on black in the game's
// palette and its fades; the subtitle (game text, nullptr - none) in the
// UI's font over the bottom. A tap is taken by the game's loop as its key.
void muiSceneShow();
void muiSceneHide();
void muiSceneSetPicture(InterfaceFrameId picture);
void muiSceneSetFrame(const unsigned char* pixels, int width, int height);
void muiSceneSetSubtitle(const char* text);
bool muiSceneTakeTap();

// Black over everything while a game loads (mui_scenes.cc, main.cc), the
// load screen over it; it takes the touches and hides the HUD.
void muiCurtainShow();
void muiCurtainHide();

// Credits and quotes (mui_scenes.cc, credits.cc `creditsOpen`): the lines
// (game text) roll up in the game's colors until the end, a tap or a key.
struct MuiCreditsLine {
    std::string text;
    MuiColor color;
    bool title = false;
};

void muiCreditsRun(const std::vector<MuiCreditsLine>& lines);

// World map (mui_worldmap.cc, worldmap.cc): its screen shows the game's
// state (`wmMobileGetState`) while the game's travel runs; what the player
// asked for is taken by the world map's loop.
enum class MuiWorldmapActionType {
    None,
    // Walk (drive) to [x], [y] (world pixels).
    TravelTo,
    // Walk to town [area].
    TravelToArea,
    // Enter the place the party stands at (a town, the wasteland).
    Enter,
    // The game menu (`muiWorldmapMenuRun`).
    Menu,
};

struct MuiWorldmapAction {
    MuiWorldmapActionType type = MuiWorldmapActionType::None;
    float x = 0.0f;
    float y = 0.0f;
    int area = -1;
};

void muiWorldmapShow();
void muiWorldmapHide();
// Esc, Android back: its popups close (the world map is left by entering).
void muiWorldmapBack();
MuiWorldmapAction muiWorldmapTakeAction();
// The game menu over the world map (as on the map, without save and load):
// continue, settings, exit.
void muiWorldmapMenuRun();

// Town map (mui_worldmap.cc, worldmap.cc `wmTownMapFunc`): the town's picture
// with its entrances. Returns the entrance chosen, -1 - back.
struct MuiTownMapEntrance {
    int index;
    // Where it is on the picture (its pixels).
    float x;
    float y;
    // Game text.
    std::string name;
};

struct MuiTownMapView {
    // Game text.
    std::string name;
    int pictureFid = -1;
    std::vector<MuiTownMapEntrance> entrances;
};

int muiTownMapRun(const MuiTownMapView& view);

// Elevator panel (mui_elevator.cc, elevator.cc `elevatorSelectLevel`): the
// game's panel art with its buttons (the levels' names are drawn on it) and
// gauge. Returns the level chosen (after the gauge moved to it), -1 - back.
struct MuiElevatorView {
    InterfaceFrameId background = InterfaceFrameId::Invalid;
    // Drawn over the background's bottom (other levels' buttons), or none.
    InterfaceFrameId panel = InterfaceFrameId::Invalid;
    int levels = 0;
    // Level the dude is at.
    int level = 0;
    // Keys choosing the levels (their names), as the game's panel.
    char keys[4] = {};
    // Milliseconds the gauge's needle takes for one of its 12 steps.
    float gaugeStepTime = 0.0f;
    // The lift starts from [from] to [to] (its sound).
    std::function<void(int from, int to)> onTravel;
};

int muiElevatorRun(const MuiElevatorView& view);

// Called shot panel (mui_called_shot.cc, combat.cc
// `calledShotSelectHitLocation`): 0 - [location] chosen, -1 - cancelled.
int muiSelectCalledShot(Object* critter, int hitMode, HitLocation* location);

// Dialog boxes (dbox.cc `showDialogBox`), same return values.
int muiShowDialogBox(const char* title, const char** body, int bodyLength, const char* secondaryButtonText, int flags);

// Automated tests: center of widget [id] of the last frame in normalized
// screen coordinates.
bool muiGetWidgetCenter(const char* id, float* x, float* y);

} // namespace fallout

#endif /* FALLOUT_MUI_H_ */
