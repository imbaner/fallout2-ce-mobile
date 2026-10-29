#ifndef FALLOUT_MUI_ICONS_H_
#define FALLOUT_MUI_ICONS_H_

#include "mui_draw.h"

namespace fallout {

// Line icons of the mobile UI, drawn with the theme colors at any size
// instead of game art or abbreviated labels.
enum class MuiIcon {
    // HUD.
    Menu,
    QuickSave,
    QuickLoad,
    Inventory,
    Character,
    Pipboy,
    Map,
    Skills,
    Highlight,
    Sneak,
    Log,
    EndTurn,
    EndCombat,
    SwapHands,
    ActionPoints,
    HitPoints,
    Aim,
    Reload,
    Fist,
    Weight,
    Back,
    // Two-way arrow around the feet: turn the character.
    Turn,

    // Party orders.
    PartyOrders,
    Holster,
    Regroup,
    Spread,
    Burst,
    AutoLoot,

    // Skills (sneak, first aid use Sneak and FilterDrugs).
    Lockpick,
    Steal,
    Traps,
    Doctor,
    Science,
    Repair,
    Target,

    // Dialog tabs.
    Talk,
    Barter,
    Review,

    // Item filters.
    FilterAll,
    FilterWeapons,
    FilterWeaponsAndAmmo,
    FilterArmor,
    FilterDrugs,
    FilterAmmo,
    FilterMisc,

    // Action menu (the rest are above).
    Cancel,
    Drop,
    Look,
    Rotate,
    Use,
    Unload,
    Push,

    // Steppers, editing.
    Plus,
    Minus,
    Edit,

    // Movie player.
    Play,
    Pause,
    Stop,
    Expand,
    Collapse,

    // Combat's tactical view: three tiles.
    TacticalView,

    // The port author's pages (the main menu).
    Telegram,
    YouTube,
    GitHub,

    Count,
};

// Icon centered at ([x], [y]) in a [size] square, strokes of [thickness]
// (output pixels).
void muiDrawIcon(MuiIcon icon, float x, float y, float size, float thickness, MuiColor color);

// Icons are drawn once per size into textures (see `muiDrawIcon`); they
// belong to the renderer.
void muiIconsReset();

} // namespace fallout

#endif /* FALLOUT_MUI_ICONS_H_ */
