#ifndef FALLOUT_MUI_SCREENS_H_
#define FALLOUT_MUI_SCREENS_H_

#include <functional>
#include <string>
#include <vector>

#include "hud_layout.h"
#include "message.h"
#include "mui.h"
#include "mui_icons.h"
#include "obj_types.h"

namespace fallout {

// The port's pages and its author's (the main menu, the about section).
constexpr const char* kPortSourceUrl = "https://github.com/imbaner/fallout2-ce-mobile";
constexpr const char* kPortIssuesUrl = "https://github.com/imbaner/fallout2-ce-mobile/issues";
constexpr const char* kPortTelegramUrl = "https://t.me/imbaner";
constexpr const char* kPortYouTubeUrl = "https://www.youtube.com/@imbaner_dev";
constexpr const char* kPortThanksUrl = "https://boosty.to/imbaner";

// Parts shared by the mobile UI game screens (talk, barter, later loot and
// inventory).

// Tabs of the dialog screens at the right edge.
enum class MuiDialogTab {
    Talk,
    Barter,
    Review,
    // Party member control (party members only).
    Party,
};

// Dialog screens' content area and the tab column at the right edge, the
// same on every dialog screen.
void muiDialogLayout(MuiContext& ui, MuiRect* content, MuiRect* tabs);

// Tab buttons in [column] (barter only when available), returns the tapped
// tab or -1.
int muiDialogTabs(MuiContext& ui, const MuiRect& column, MuiDialogTab active, bool barterAvailable, bool partyAvailable);

// Square tab button of a tab column: icon, highlighted when [active].
// Returns true when tapped while not active (with the button sound unless
// [sound] is false: what it opens plays its own).
bool muiTabButton(MuiContext& ui, const std::string& id, const MuiRect& rect, MuiIcon icon, bool active, bool sound = true);

// Game screens switched between with the tabs at the right edge (the HUD's
// inventory, character, Pip-Boy and map buttons).
enum class MuiGameScreenTab {
    None,
    Back,
    Inventory,
    Character,
    Pipboy,
    Map,
};

// Back, then the game screens' tabs ([active] highlighted) in [column];
// [screens] false - back only. Widget ids "<idPrefix>.back",
// "<idPrefix>.inventory", ... Returns the tapped tab.
MuiGameScreenTab muiGameScreenTabs(MuiContext& ui, const MuiRect& column, const std::string& idPrefix, MuiGameScreenTab active, bool screens);

// Switches to the screen of [tab] (not back): posts the game's command of the
// HUD button opening it (the game's own rules and sound, run once the
// showing screen, which closes itself, is gone), keeps the frame until it
// shows. Returns false when the game wouldn't open it now (combat: action
// points for the inventory, no Pip-Boy): the game's message shows, the
// showing screen stays.
bool muiSwitchGameScreen(MuiContext& ui, MuiGameScreenTab tab);

// Action points opening the inventory costs now (combat, dude's turn);
// false when it's free. [blocked] - not enough of them.
bool muiInventoryCost(std::u32string* text, bool* blocked);

// Small number at the top right corner of [button] (costs), red when
// [warning].
void muiDrawCostBadge(MuiContext& ui, const MuiRect& button, const std::u32string& text, bool warning);

// Sections inside one screen (settings): text buttons down [column] (ids
// "<idPrefix>.<index>"), [selected] highlighted, [marked] ones with a dot
// (changed). Returns the section tapped, else [selected]. Moving between
// screens stays with the tab column at the right edge.
int muiSectionList(MuiContext& ui, const std::string& idPrefix, const MuiRect& column, const std::vector<std::u32string>& labels, int selected, const std::vector<bool>& marked);

// Where [map] / [elevation] is: "city · map" as the game names them (empty
// for an unknown map).
std::u32string muiPlaceText(int map, int elevation);
// The same with names from [names] (MAP.MSG loaded by the caller: the game
// loads it only for a game, not in the main menu).
std::u32string muiPlaceText(int map, int elevation, MessageList* names);

// Game menu (mui_options.cc) over what shows: continue, save and load if
// [saveLoad], settings, exit; [place] in its header (the current map's if
// null). Returns 1 if a game was saved or loaded.
int muiGameMenuRun(bool saveLoad, const std::u32string* place);

// Visual unavailable state shared by game-screen tabs and HUD buttons.
// Leaves the touch target active so the game's existing feedback still runs.
void muiDrawUnavailableOverlay(MuiContext& ui, const MuiRect& button, float radiusDp);

// Talk screen opens its review panel when it shows next.
void muiGameDialogOpenReview();
// Talk screen opens its party member control panel when it shows next.
void muiGameDialogOpenParty();

// Popup list of actions (party orders, later skills): rows with an icon and
// a name next to [anchor] (output pixels), on the side with room that
// doesn't cover [avoid] (what the actions are for). Tap on a row calls
// [onChoose] with its index and closes the list, tap outside closes it.
// [keepOpen] (optional) closes it when it returns false (e.g. the HUD is
// hidden). Row widget ids are "<id>.<index>".
struct MuiActionItem {
    MuiIcon icon;
    std::u32string label;
    // Shown at the right (skill value).
    std::u32string detail;
    // Disabled items are dimmed and can't be chosen.
    bool enabled = true;
};

void muiShowActionList(const std::string& id, const std::vector<MuiActionItem>& items, const MuiRect& anchor, const MuiRect& avoid, std::function<void(int)> onChoose, std::function<bool()> keepOpen = nullptr);
void muiCloseActionList();
bool muiIsActionListOpen(const std::string& id);

// The same list for synchronous game flows (skilldex): returns the chosen
// index or -1.
int muiChooseAction(const std::string& id, const std::vector<MuiActionItem>& items, const MuiRect& anchor, const MuiRect& avoid);

// Rect of [width] x [height] next to [anchor] (right, left, below, above)
// inside the safe area, not over [avoid] when possible.
MuiRect muiPlacePopup(MuiContext& ui, const MuiRect& anchor, const MuiRect& avoid, float width, float height);

// Screen rect of [object] (output pixels) as the world view shows it.
bool muiObjectRect(Object* object, MuiRect* rect);

// Where popups for a skill or item used on [target] stand: next to the
// target, or next to HUD element [fallback] (no target).
void muiPopupAnchor(Object* target, HudElementId fallback, MuiRect* anchor, MuiRect* avoid);

// Frame of the movie playing in the mobile UI player (`gameMovieStartMobile`)
// fit into [rect] on black, its subtitle at the bottom.
void muiDrawMovie(MuiContext& ui, const MuiRect& rect);

// Item categories of the lists (like the Inventory Filter mod), taken from
// game item types.
enum class MuiItemFilter {
    All,
    Weapons,
    WeaponsAndAmmo,
    Armor,
    Drugs,
    Ammo,
    Misc,
    Count,
};

struct MuiItemFilterState {
    MuiItemFilter filter = MuiItemFilter::All;
    // Long press on weapons: firearms only.
    bool gunsOnly = false;
};

bool muiItemMatchesFilter(Object* item, const MuiItemFilterState& state);

// Row of filter buttons over [rect], ids start with [id].
void muiItemFilterRow(MuiContext& ui, const std::string& id, const MuiRect& rect, MuiItemFilterState* state);

struct MuiListItem {
    Object* item;
    int quantity;
};

// Items of [owner] as the game lists them (newest first), money first,
// filtered.
std::vector<MuiListItem> muiInventoryItems(Object* owner, const MuiItemFilterState& state);

// Total weight of the listed items.
int muiItemsWeight(const std::vector<MuiListItem>& items);

// Item cell without the count: frame and inventory art (crisp pixels).
void muiDrawItemImage(MuiContext& ui, const MuiRect& rect, Object* item, bool pressed);

// Item cell: `muiDrawItemImage` and how many of it [quantity] of its stack
// are (`itemStackAmount`: ammo in rounds, always, as the game shows).
void muiDrawItemCell(MuiContext& ui, const MuiRect& rect, Object* item, int quantity, bool pressed);

// The dragged item under the finger: its cell ([quantity] of its stack) and,
// above, its name with how many the least part that moves is (one, ammo's
// first clip in rounds), its weight and [price] (< 0: not shown).
void muiDrawDraggedItem(MuiContext& ui, Object* item, int quantity, int price);

// Party member tabs (barter, loot): one out of reach
// (`inventoryPartyMemberIsReachable`) is dimmed, a tap or a drop on it says
// why.
void muiNotifyPartyMemberOutOfReach(Object* critter);

} // namespace fallout

#endif /* FALLOUT_MUI_SCREENS_H_ */
