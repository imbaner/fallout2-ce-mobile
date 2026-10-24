#include "dev_autotest.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <lodepng.h>

#include "actions.h"
#include "animation.h"
#include "app_update.h"
#include "art.h"
#include "character_editor.h"
#include "combat.h"
#include "combat_ai.h"
#include "critter.h"
#include "db.h"
#include "display_monitor.h"
#include "elevator.h"
#include "game.h"
#include "game_commands.h"
#include "game_dialog.h"
#include "game_mouse.h"
#include "game_movie.h"
#include "game_sound.h"
#include "input.h"
#include "interface.h"
#include "inventory.h"
#include "item.h"
#include "kb.h"
#include "loadsave.h"
#include "map.h"
#include "map_hints.h"
#include "mouse.h"
#include "movie.h"
#include "mui.h"
#include "object.h"
#include "party_member.h"
#include "perf_monitor.h"
#include "perk.h"
#include "platform_compat.h"
#include "player_commands.h"
#include "preferences.h"
#include "proto.h"
#include "proto_instance.h"
#include "queue.h"
#include "save_storage.h"
#include "scripts.h"
#include "settings.h"
#include "skill.h"
#include "sound.h"
#include "stat.h"
#include "svga.h"
#include "tactical_view.h"
#include "text_object.h"
#include "tile.h"
#include "touch.h"
#include "touch_controls.h"
#include "touch_hud.h"
#include "touch_log.h"
#include "window_manager.h"
#include "world_view.h"
#include "worldmap.h"

namespace fallout {

enum DevAutotestAction {
    DEV_AUTOTEST_ACTION_NONE,
    DEV_AUTOTEST_ACTION_ZOOM,
    DEV_AUTOTEST_ACTION_ZOOM_AT_DUDE,
    DEV_AUTOTEST_ACTION_PAN,
    // Synthetic two finger touch: a - start span, b - end span, c -
    // horizontal centroid movement (all in fractions of window width).
    DEV_AUTOTEST_ACTION_PINCH,
    // Points mouse at the tile `a` hexes away from dude and walks there the
    // same way as mouse click does.
    DEV_AUTOTEST_ACTION_WALK,
    // Switches game mouse to arrow mode with mouse at the given screen point.
    DEV_AUTOTEST_ACTION_ARROW,
    // Logs where dude ended up after the walk.
    DEV_AUTOTEST_ACTION_CHECK_WALK,
    // Checks that a map tap after title-screen load moved the player.
    DEV_AUTOTEST_ACTION_CHECK_TOUCH_MOVED,
    // Centers view on dude.
    DEV_AUTOTEST_ACTION_CENTER_DUDE,
    // Synthetic single finger touches (touch controls):
    // drag from view center by (a, b) screen pixels;
    DEV_AUTOTEST_ACTION_TOUCH_DRAG,
    // tap on the tile `a` hexes away from dude in direction `b`;
    DEV_AUTOTEST_ACTION_TOUCH_TAP_TILE,
    // long press on the nearest critter;
    DEV_AUTOTEST_ACTION_TOUCH_LONG_PRESS_CRITTER,
    // tap on radial menu button of action menu item `a`;
    DEV_AUTOTEST_ACTION_TOUCH_TAP_RADIAL,
    // tap on the nearest critter.
    DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER,
    // Attacks the nearest critter with mouse (starts combat, the script
    // continues from combat loop during player turns).
    DEV_AUTOTEST_ACTION_START_COMBAT,
    // Logs combat state.
    DEV_AUTOTEST_ACTION_LOG_COMBAT,
    // Tap on the tile next to the nearest critter (on dude's side).
    DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_CRITTER,
    // Gives dude item with pid `a`.
    DEV_AUTOTEST_ACTION_GIVE_ITEM,
    // Presses key with SDL scancode `a` (desktop screens; the touch-only
    // build takes only a text field's editing keys).
    DEV_AUTOTEST_ACTION_KEY,
    // Posts game command `a` (`GameCommandType`), as the HUD's buttons do.
    DEV_AUTOTEST_ACTION_COMMAND,
    // Back (Android's back button).
    DEV_AUTOTEST_ACTION_BACK,
    // Logs the talk: reply, options, dialog modes, caps and inventory sizes
    // of dude and the speaker, checksums of global, map and the speaker's
    // local variables (one run of the game's logic gives the same lines).
    DEV_AUTOTEST_ACTION_LOG_DIALOG,
    // Logs the game's windows made since the scenario started (hidden ones
    // too).
    DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS,
    // Logs the critters at dude's elevation (pid, name, tile, distance).
    DEV_AUTOTEST_ACTION_LOG_CRITTERS,
    // Talks to the critter with pid `0x01000000 + a` (walks there first).
    DEV_AUTOTEST_ACTION_TALK_TO_PID,
    // Chooses talk option `a`: the mobile UI screen's way, or the digit key
    // with the game's windows.
    DEV_AUTOTEST_ACTION_CHOOSE_OPTION,
    // Waits (up to `a` frames) until a talk shows options, then as NONE.
    DEV_AUTOTEST_ACTION_WAIT_TALKING,
    // Waits (up to `a` frames) until no map is loading (a map's enter
    // script may play a movie inside the load), then as NONE.
    DEV_AUTOTEST_ACTION_WAIT_MAP_LOADED,
    // The talk's barter, barter's offer and back to talk: the mobile UI
    // screens' way, or the game's keys (B, M, T) with its windows.
    DEV_AUTOTEST_ACTION_DIALOG_BARTER,
    DEV_AUTOTEST_ACTION_BARTER_OFFER,
    DEV_AUTOTEST_ACTION_BARTER_TALK,
    // Tap a legacy screen point (a, b), for title-menu buttons.
    DEV_AUTOTEST_ACTION_TOUCH_TAP_SCREEN,
    // Inventory slot touches: tap on slot `a`; long press on slot `a`
    // (captures while held); drag from slot `a` to slot `b`.
    // Logs dude's hands and inventory.
    DEV_AUTOTEST_ACTION_LOG_INVENTORY,
    // Logs whether mod files are visible.
    DEV_AUTOTEST_ACTION_CHECK_MODS,
    // After title-screen Load, the world palette and map input must be ready.
    DEV_AUTOTEST_ACTION_CHECK_LOADED_WORLD,
    // Centers view on the nearest critter.
    DEV_AUTOTEST_ACTION_CENTER_CRITTER,
    // Tap on the tile next to dude on the side opposite to the nearest
    // critter.
    DEV_AUTOTEST_ACTION_TOUCH_TAP_AWAY_FROM_CRITTER,
    // Taps a free tile next to the nearest critter that its sprite partly
    // covers (north-east or north-west of it): `b` 0 - where the tile shows
    // (off the critter's body column), 1 - where the critter's sprite is over
    // it; the dude's tile is the start for CHECK_TOUCH_MOVED.
    DEV_AUTOTEST_ACTION_TOUCH_TAP_BEHIND_CRITTER,
    // Logs the combat selection (touch controls); FAIL when its action isn't
    // `a` (1 move, 3 attack).
    DEV_AUTOTEST_ACTION_CHECK_SELECTION,
    // Taps the center of the nearest critter's tile; `b` > 0 - holds that
    // many frames (a long press).
    DEV_AUTOTEST_ACTION_TOUCH_CRITTER_TILE,
    // Logs the tactical view (on, shown, the walk's tiles); FAIL when shown
    // isn't `a` (or, `b` 1, it isn't on).
    DEV_AUTOTEST_ACTION_CHECK_TACTICAL,
    // World map: open it (the script continues from its loop); log its
    // state; drag from view center by (a, b); tap at party position + (a, b).
    DEV_AUTOTEST_ACTION_OPEN_WORLDMAP,
    DEV_AUTOTEST_ACTION_LOG_WORLDMAP,
    DEV_AUTOTEST_ACTION_WORLDMAP_DRAG,
    DEV_AUTOTEST_ACTION_WORLDMAP_TAP,
    // Logs attack mode.
    DEV_AUTOTEST_ACTION_LOG_HIT_MODE,
    // Creates item with pid `a` and equips it in hand `b`.
    DEV_AUTOTEST_ACTION_EQUIP_ITEM,
    // Touch HUD: tap on element `a` (HudElementId); tap on attack mode chip
    // `a`.
    DEV_AUTOTEST_ACTION_HUD_TAP,
    DEV_AUTOTEST_ACTION_HUD_TAP_MODE,
    // Same with finger up in the same frame.
    DEV_AUTOTEST_ACTION_HUD_QUICK_TAP_MODE,
    // Logs whether save slot `a` (1-based) has a save.
    DEV_AUTOTEST_ACTION_LOG_SLOT,
    // Tap magnets: tap at destination marker + (a, b) screen pixels; logs
    // dude movement; puts item with pid `a` on the ground `b` hexes from dude
    // (direction `c`); taps at that item + (a, b).
    DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_MARKER,
    DEV_AUTOTEST_ACTION_LOG_MOVEMENT,
    DEV_AUTOTEST_ACTION_PLACE_ITEM,
    DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_ITEM,
    // Logs whether the placed item is still on the ground.
    DEV_AUTOTEST_ACTION_LOG_PLACED_ITEM,
    // Mobile UI: tap on widget `widget` (a = 1 - quick tap, finger up in the
    // same frame).
    DEV_AUTOTEST_ACTION_MUI_TAP,
    // Mobile UI: drag from widget `widget` to widget `target`.
    DEV_AUTOTEST_ACTION_MUI_DRAG,
    // Holds HUD element `a` for `b` frames, captures "<name>_held" before
    // the finger goes up.
    DEV_AUTOTEST_ACTION_HUD_LONG_PRESS,
    // Mobile UI: holds widget `widget` for `a` frames, captures
    // "<name>_held" before the finger goes up.
    DEV_AUTOTEST_ACTION_MUI_LONG_PRESS,
    // Kills the nearest critter, it's the corpse for the actions below.
    DEV_AUTOTEST_ACTION_KILL_CRITTER,
    // Puts item with pid `a` into the corpse's inventory.
    DEV_AUTOTEST_ACTION_GIVE_CORPSE_ITEM,
    // To the nearest critter: `a` - pid, `b` - quantity, `c` - ammo: rounds
    // in the clip not full.
    DEV_AUTOTEST_ACTION_GIVE_CRITTER_ITEM,
    DEV_AUTOTEST_ACTION_CENTER_CORPSE,
    DEV_AUTOTEST_ACTION_TOUCH_TAP_CORPSE,
    // Floating text over dude (like float_msg).
    DEV_AUTOTEST_ACTION_FLOAT_TEXT,
    // Critter proto `a` (index, pid is 0x1000000 + a) `b` hexes from dude
    // (direction `c`) with script `widget` (file name in scripts.lst), joins
    // the party; it's the one GIVE_PARTY_ITEM gives to.
    DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER,
    // Item pid `a` x `b` to the last added party member (else the first one
    // of a loaded game).
    DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM,
    // The party member of `ADD_PARTY_MEMBER` gets items of pid [a] until he
    // can carry only [b] more.
    DEV_AUTOTEST_ACTION_FILL_PARTY_WEIGHT,
    // The party member of `ADD_PARTY_MEMBER` [a] tiles south-east of the dude.
    DEV_AUTOTEST_ACTION_MOVE_PARTY_MEMBER,
    // The last added party member takes item pid `a` of its inventory in
    // hand.
    DEV_AUTOTEST_ACTION_WIELD_PARTY_ITEM,
    // Dude's name `widget` (game encoding).
    DEV_AUTOTEST_ACTION_SET_NAME,
    // Saves the game to slot `a` (0-based) with description `widget`.
    DEV_AUTOTEST_ACTION_SAVE_GAME,
    DEV_AUTOTEST_ACTION_QUICK_SAVE,
    DEV_AUTOTEST_ACTION_QUICK_LOAD,
    DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION,
    // Loads slot `a` (0-based).
    DEV_AUTOTEST_ACTION_LOAD_GAME,
    // Logs party members and what their combat AI goes by.
    DEV_AUTOTEST_ACTION_LOG_PARTY,
    // The hit points of the last added party member (else the first one of a
    // loaded game): [a]; [b] - the party member of pid `0x01000000 + b`.
    DEV_AUTOTEST_ACTION_SET_PARTY_HP,
    // FAIL unless the party member of SET_PARTY_HP has fewer healing drugs
    // and more hit points than then, at least its run away hit points, and
    // isn't fleeing.
    DEV_AUTOTEST_ACTION_CHECK_PARTY_HEALED,
    // In combat: the dude counts as hit by the nearest critter not in the
    // party, so combat goes on after the round (nobody turns hostile).
    DEV_AUTOTEST_ACTION_KEEP_COMBAT,
    // Ends the dude's turn and waits (up to `a` frames) for his turn in the
    // next combat round, then as NONE.
    DEV_AUTOTEST_ACTION_END_TURN_WAIT,
    // FAIL unless the critical hit table has [c] in kill type `a / 100`, hit
    // location `a / 10 % 10`, effect `a % 10` (0-based), data member [b].
    DEV_AUTOTEST_ACTION_CHECK_CRITICAL,
    // The nearest critter not in the party the dude can shoot at starts
    // combat attacking the dude, with [a] bonus action points (combat start
    // data, as scripts' attacks); the script continues in the dude's turn.
    DEV_AUTOTEST_ACTION_START_COMBAT_ATTACKED,
    // FAIL unless the dude has his maximum action points.
    DEV_AUTOTEST_ACTION_CHECK_DUDE_AP,
    // The dude gets perk [a] (one rank).
    DEV_AUTOTEST_ACTION_ADD_DUDE_PERK,
    // In combat: the dude's Bonus Move free move points are [a].
    DEV_AUTOTEST_ACTION_SET_FREE_MOVE,
    // FAIL unless the dude's free move points are [a] (in combat).
    DEV_AUTOTEST_ACTION_CHECK_FREE_MOVE,
    // The dude uses a new item of pid [a] (put in his inventory) on the
    // nearest critter (walks there first).
    DEV_AUTOTEST_ACTION_USE_ITEM_ON_CRITTER,
    // FAIL unless the dude is at most [a] hexes from the nearest critter.
    DEV_AUTOTEST_ACTION_CHECK_NEAR_CRITTER,
    // The dude walks towards the free tile [a] hexes away from the nearest
    // critter, the walk's distance [b] (as the engine's walks to tiles).
    DEV_AUTOTEST_ACTION_WALK_LIMITED,
    // FAIL unless the dude is [a] hexes from where WALK_LIMITED started.
    DEV_AUTOTEST_ACTION_CHECK_WALKED,
    // Knockouts of the nearest critter not in the party (sfall's fixes),
    // `a`: 1 - stand up standing (FAIL if it animates or stays knocked
    // down); 2 - in combat, wake up from a knockout (FAIL unless marked woken
    // and knocked down); 3 - knock it down, losing its turn, prone; 4 - the
    // dude isn't hit by anyone (combat may end); 5 - FAIL if it's knocked
    // down or loses a turn; 6 - knocked out, the map left (FAIL unless
    // knocked down and its wake up event removed).
    DEV_AUTOTEST_ACTION_KNOCKOUT,
    // A party member's corpse (sfall's fix in partyFixMultipleMembers): `a`
    // 1 - kills the party member (ADD_PARTY_MEMBER's, else the first one of
    // a loaded game); 2 - FAIL unless its corpse is on the map.
    DEV_AUTOTEST_ACTION_PARTY_CORPSE,
    // Critters healing while the dude is away from a map (sfall's fix in
    // _map_age_dead_critters): `a` 1 - the nearest living critter not in
    // the party that heals (not a robot, no NO_HEAL flag) loses its script
    // and is put at 1 hit point; 2 - [b] hours pass; 3 - FAIL unless that
    // critter has more than 1 hit point.
    DEV_AUTOTEST_ACTION_MAP_AGE,
    // Where the original differs from fallout2-ce#675 (map.cc, proto.cc,
    // proto_instance.cc): `a` 1 - FAIL unless a new critter proto's team is
    // 1; 2 - the party member placed at the dude's tile: FAIL unless it's
    // the tile east of the dude (the first direction tried); 3 - kills the
    // nearest critter whose body ages; 4 - FAIL unless its body became a
    // blood stain; 5 - FAIL unless that blood stain is gone.
    DEV_AUTOTEST_ACTION_ORIGINAL_675,
    // Barter (sfall's fixes in barterAttemptTransaction): `a` 1 - gives the
    // dude an item of pid `b`, `c` 1 - turned on, 2 - turned on and off
    // (used); 2 - gives the dude items of pid `b` until he's overloaded;
    // 3 - FAIL unless the barter partner has items of pids `b` and `c` (0 -
    // none) and the dude has none of them.
    DEV_AUTOTEST_ACTION_BARTER_SALE,
    // Doses of drug pid `c` (sfall's fix for the drug pids of saved drug
    // effects): `a` 1 - the dude takes `b` doses; 2 - FAIL unless the dude
    // has `b` drug effects of that drug, or one more dose changes Strength.
    DEV_AUTOTEST_ACTION_DRUG_DOSES,
    // The dude's stack of item pid `a` wielded in the right hand (sfall's
    // "Unlimited Ammo" fix): FAIL unless one item is in the hand and the
    // rest (`b` in all) stays in the inventory.
    DEV_AUTOTEST_ACTION_WIELD_STACK,
    // Burst friendly fire (sfall's fix in _combat_safety_invalidate_weapon):
    // an attacker (Intelligence 10, weapon pid `a`) `b` hexes from the dude,
    // a critter of its team with no damage resistance between them: FAIL
    // unless the burst at the dude counts as unsafe.
    DEV_AUTOTEST_ACTION_BURST_FRIENDLY,
    // Moving away before an explosion (sfall's ai_move_away fix): `a` 1 - a
    // hostile critter `b` hexes from the dude with grenades of pid `c` in
    // hand; 2 - FAIL unless the distance to move away was negative and it
    // left its tile.
    DEV_AUTOTEST_ACTION_MOVE_AWAY,
    // Picking up a weapon by the AI (sfall's ai_retrieve_object fix): `a`
    // 1 - an unarmed hostile critter `b` hexes from the dude, a weapon of
    // pid `c` on the ground next to it and in its inventory a Stimpak with
    // the weapon's id, the dude armed; 2 - FAIL unless the weapon is in its
    // hand.
    DEV_AUTOTEST_ACTION_RETRIEVE_OBJECT,
    DEV_AUTOTEST_ACTION_NPC_PICKUP,
    DEV_AUTOTEST_ACTION_MULTIHEX_MOVE,
    DEV_AUTOTEST_ACTION_CHECK_MULTIHEX_PARTY,
    DEV_AUTOTEST_ACTION_EXPLODE_ON_LEAVE,
    DEV_AUTOTEST_ACTION_STEAL_PARTY_MEMBER,
    DEV_AUTOTEST_ACTION_CHECK_PARTY_WEIGHT,
    DEV_AUTOTEST_ACTION_MISS_OBJECT,
    DEV_AUTOTEST_ACTION_DROP_WORN_ARMOR,
    // Ends the turn and [c] frames later drags the map by ([a], [b]) while
    // others act.
    DEV_AUTOTEST_ACTION_PAN_DURING_ENEMY_TURN,
    // Talks to the first party member, or with `a` the one of pid
    // `0x01000000 + a` (the dialog loop runs inside); he's the party member
    // of the party actions below.
    DEV_AUTOTEST_ACTION_TALK_PARTY_MEMBER,
    // Taps at ([a], [b]) in the game dialog's lower window (the game's own
    // windows, for screens without the mobile UI).
    DEV_AUTOTEST_ACTION_TAP_DIALOG_WINDOW,
    // Logs the party member control data of the dialog speaker.
    DEV_AUTOTEST_ACTION_LOG_PARTY_CONTROL,
    // Types the text of the widget field (UTF-8).
    DEV_AUTOTEST_ACTION_TYPE_TEXT,
    // Sends event code [a] to the game's input (window buttons).
    DEV_AUTOTEST_ACTION_EVENT,
    // Sets global variable [a] to [b].
    DEV_AUTOTEST_ACTION_SET_GVAR,
    // `a` - stat, `b` - base value (the dude).
    DEV_AUTOTEST_ACTION_SET_STAT,
    // `a` - skill, `b` - unspent skill points put into it (the dude).
    DEV_AUTOTEST_ACTION_ADD_SKILL,
    // Plays game movie [a] (the step completes first, the movie loop runs
    // the script on).
    DEV_AUTOTEST_ACTION_PLAY_MOVIE,
    // Requires a decoded movie frame during the full-screen playback loop.
    DEV_AUTOTEST_ACTION_CHECK_MOVIE,
    // Puts dude [a] hexes from the nearest critter (out of combat).
    DEV_AUTOTEST_ACTION_MOVE_NEAR_CRITTER,
    // Sets dude's action points to [a] (combat).
    DEV_AUTOTEST_ACTION_SET_ACTION_POINTS,
    // Adds [a] experience points to dude.
    DEV_AUTOTEST_ACTION_ADD_EXPERIENCE,
    // Logs dude's level, skill points, owed perks, perks, some skills.
    DEV_AUTOTEST_ACTION_LOG_CHARACTER,
    // Opens character creation (the step completes first, the editor loop
    // runs the script on).
    DEV_AUTOTEST_ACTION_OPEN_CHARACTER_CREATION,
    // Combat speed [a] (preferences), player speedup [b],
    // `combat_speed_all_animations` [c]; [a] < 0 - back to the settings the
    // test started with.
    DEV_AUTOTEST_ACTION_SET_COMBAT_SPEED,
    // Starts dude's animation and logs how long it took when it ends: [a]
    // 0 - walks [b] hexes in direction [c], 1 - runs so, 2 - plays animation
    // type [b].
    DEV_AUTOTEST_ACTION_TIME_DUDE_ANIMATION,
    // Every later frame takes [a] ms longer (a slow device).
    DEV_AUTOTEST_ACTION_SLOW_FRAMES,
    // Logs how long sound effect `widget` of dude's animation plays
    // (`animationLoadSoundEffect`: combat speed makes it shorter).
    DEV_AUTOTEST_ACTION_LOG_ANIMATION_SOUND,
    // Moves the map view by ([a], [b]) screen pixels every frame for [c]
    // frames (a finger dragging it).
    DEV_AUTOTEST_ACTION_PAN_FRAMES,
    // Logs frames, their average time and the world's uploads to the GPU
    // since the previous one (what the steps between cost).
    DEV_AUTOTEST_ACTION_PERF_LOG,
    // Dude steps (as walking does, the roof hides) onto the nearest tile
    // under a roof, [a] = 1 - back to where the previous one started.
    DEV_AUTOTEST_ACTION_STEP_UNDER_ROOF,
    // A finger of its own (id 7): [a] 0 - down, 1 - up, 2 - down and up at
    // once (a system edge gesture re-sends touches so), at ([b], [c]) in
    // fractions of the window.
    DEV_AUTOTEST_ACTION_FINGER,
    // Taps mobile UI widget `widget` (down and up at once), then the game
    // stalls [a] ms before reading them.
    DEV_AUTOTEST_ACTION_MUI_TAP_THEN_STALL,
    // Mobile UI widget `widget` is on screen ([a] 1) or not ([a] 0).
    DEV_AUTOTEST_ACTION_CHECK_WIDGET,
    // Leaves the map for map file `widget`, elevation [a], tile [b] (-1 -
    // its entrance), as an exit grid does (`mapSetTransition`); logs the
    // party's size.
    DEV_AUTOTEST_ACTION_MAP_TRANSITION,
    // Touch recorder (touch_log.h): [a] 0 - on, touch.log emptied; 1 - logs
    // what touch.log got, fails without a dump for each of `widget` and
    // `target` (reasons) and, [c] 1, without a "game closed" one; 2 - what
    // the game closing writes (`touchLogExit`).
    DEV_AUTOTEST_ACTION_TOUCH_LOG,
    // Main menu pictures (`muiPictureTexture`): the high resolution one by
    // path, the 640x480 one by id, a missing path, the cached one again.
    DEV_AUTOTEST_ACTION_CHECK_MENU_PICTURES,
    // Opens elevator [a]'s panel as a script does (the step completes first,
    // the panel's loop runs the script on), logs the level it returned.
    DEV_AUTOTEST_ACTION_OPEN_ELEVATOR,
    // Requests the ending slides as a script does (they start from the
    // game's loop).
    DEV_AUTOTEST_ACTION_REQUEST_ENDGAME,
    // Dude dies (the game shows its death screen, then the main menu).
    DEV_AUTOTEST_ACTION_KILL_DUDE,
    // World map: area [a] known and visited, the party stands at it.
    DEV_AUTOTEST_ACTION_WORLDMAP_AREA,
    // What scripts reading the mouse get (sfall `get_mouse_x/y`,
    // `tile_under_cursor`, `obj_under_cursor`, `get_mouse_buttons`) and how
    // many times `HOOK_MOUSECLICK` ran.
    DEV_AUTOTEST_ACTION_LOG_POINTER,
    // Mobile UI: drags widget `widget` by ([a], [b]) fractions of the window
    // (sliders).
    DEV_AUTOTEST_ACTION_MUI_DRAG_BY,
    // Logs the preferences and the settings the settings screen edits.
    DEV_AUTOTEST_ACTION_LOG_PREFERENCES,
    // Logs the saves (count, quick ones); FAIL when there aren't [a] of them.
    DEV_AUTOTEST_ACTION_LOG_SAVES,
    // Offline update UI: state [a], tag [widget]. No network or APK install.
    DEV_AUTOTEST_ACTION_UPDATE_STATE,
    // Number of installer requests; closing an update window revokes intent.
    DEV_AUTOTEST_ACTION_CHECK_UPDATE_INSTALLS,
};

struct DevAutotestStep {
    DevAutotestAction action;
    float a;
    float b;
    float c;
    // Frames to wait after the action before capturing.
    int delay;
    const char* name;
    // Mobile UI widget id.
    const char* widget;
    // Mobile UI widget to drag to.
    const char* target;
};

static void devAutotestLog(const char* format, ...);
static void devAutotestCheckDudeRoundTrip();

static const DevAutotestStep kDevAutotestSteps[] = {
    { DEV_AUTOTEST_ACTION_CHECK_MODS, 0, 0, 0, 1, "00_mods" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 45, "01_zoom100" },
    { DEV_AUTOTEST_ACTION_ZOOM, 2.0f, -1, -1, 10, "02_zoom200" },
    { DEV_AUTOTEST_ACTION_ZOOM, 0.5f, -1, -1, 10, "03_zoom050" },
    { DEV_AUTOTEST_ACTION_ZOOM, 1.37f, 150, 100, 10, "04_zoom137_anchor" },
    { DEV_AUTOTEST_ACTION_PAN, 47.5f, 13.25f, 0, 10, "05_pan_small" },
    { DEV_AUTOTEST_ACTION_PAN, -420.0f, -260.0f, 0, 10, "06_pan_large" },
    { DEV_AUTOTEST_ACTION_ZOOM_AT_DUDE, 3.0f, 0, 0, 10, "07_zoom300_dude" },
    { DEV_AUTOTEST_ACTION_ZOOM, 1.0f, -1, -1, 10, "08_zoom100_again" },
    { DEV_AUTOTEST_ACTION_PINCH, 0.2f, 0.4f, 0.0f, 10, "09_pinch_out" },
    { DEV_AUTOTEST_ACTION_PINCH, 0.3f, 0.3f, 0.1f, 10, "10_two_finger_pan" },
    { DEV_AUTOTEST_ACTION_ZOOM, 2.0f, -1, -1, 10, "11_zoom200" },
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 10, "11b_center_dude" },
    { DEV_AUTOTEST_ACTION_WALK, 4, 0, 0, 120, "12_walk" },
    { DEV_AUTOTEST_ACTION_CHECK_WALK, 0, 0, 0, 1, "13_after_walk" },
    { DEV_AUTOTEST_ACTION_ZOOM, 3.0f, -1, -1, 10, "14_zoom300" },
    { DEV_AUTOTEST_ACTION_ARROW, 600, 150, 0, 30, "15_arrow_cursor_zoom300" },
    { DEV_AUTOTEST_ACTION_ZOOM, 2.5f, -1, -1, 10, "16_zoom250" },
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 10, "17_center_dude" },
    { DEV_AUTOTEST_ACTION_TOUCH_DRAG, -150, -60, 0, 10, "18_touch_drag" },
    { DEV_AUTOTEST_ACTION_ZOOM, 1.5f, -1, -1, 10, "19_zoom150" },
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 10, "19b_center_dude" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_TILE, 4, ROTATION_SW, 0, 120, "20_touch_walk" },
    { DEV_AUTOTEST_ACTION_CHECK_WALK, 0, 0, 0, 1, "21_after_touch_walk" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "21b_center_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_CRITTER, 0, 0, 0, 300, "21c_walk_to_critter" },
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 10, "21d_center_dude" },
    { DEV_AUTOTEST_ACTION_TOUCH_LONG_PRESS_CRITTER, 0, 0, 0, 20, "22_radial_menu" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_RADIAL, GAME_MOUSE_ACTION_MENU_ITEM_LOOK, 0, 0, 30, "23_radial_look" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 4, 0, 0, 1, "24a_give_knife" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 0, 0, 1, "24b_give_stimpak" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 30, "24c_open_inventory" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "24d_inventory" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "24e_inventory_tap", "inventory.pid.40" },
    { DEV_AUTOTEST_ACTION_MUI_LONG_PRESS, 45, 0, 0, 20, "24f_inventory_long_press", "inventory.pid.4" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "24f2_inventory_radial_look", "radial.3" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 20, "24g_inventory_drag_to_hand", "inventory.pid.4", "inventory.slot.right" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "24h_inventory_after_drag" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 30, "24i_close_inventory" },
    { DEV_AUTOTEST_ACTION_LOG_HIT_MODE, 0, 0, 0, 1, "24j_hit_mode" },
    { DEV_AUTOTEST_ACTION_HUD_TAP_MODE, 1, 0, 0, 20, "24k_aimed_hit_mode" },
    { DEV_AUTOTEST_ACTION_LOG_HIT_MODE, 0, 0, 0, 1, "24l_hit_mode_after" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 60, "25_combat_started" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "26_combat_state" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 40, "27_combat_select_target" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "28_after_select" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 30, "29_combat_aimed_attack" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "29b_called_shot", "calledshot.0" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "30_after_attack" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_AWAY_FROM_CRITTER, 0, 0, 0, 40, "31_combat_preview_move" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "32_after_preview_move" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_AWAY_FROM_CRITTER, 0, 0, 0, 90, "33_combat_move" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "34_after_move" },
    { DEV_AUTOTEST_ACTION_PAN_DURING_ENEMY_TURN, 150, 60, 20, 150, "34b_pan_enemy_turn" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "34c_after_enemy_turn" },
};

// World map of the mobile UI: "Enter" at a visited town (Klamath) opens its
// town map (Back returns), a finger moves the map, the towns' list opens
// over "Enter" and closes, the game menu (continue, settings, exit) opens
// and continues, a tap on the terrain sends the party there.
static const DevAutotestStep kDevAutotestWorldmapSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "w00_map" },
    { DEV_AUTOTEST_ACTION_WORLDMAP_AREA, CITY_KLAMATH, 0, 0, 1, "w00b_at_klamath" },
    { DEV_AUTOTEST_ACTION_OPEN_WORLDMAP, 0, 0, 0, 1, "w01_open" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "w02_worldmap" },
    { DEV_AUTOTEST_ACTION_LOG_WORLDMAP, 0, 0, 0, 1, "w03_state" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "w04_enter_shown", "worldmap.enter" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "w05_town_map", "worldmap.enter" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "w06_back_to_world", "townmap.back" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, -0.25f, -0.15f, 0, 20, "w07_moved", "worldmap.map" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "w08_towns", "worldmap.towns" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "w08b_enter_under_towns", "worldmap.enter" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "w09_towns_closed", "worldmap.towns.close" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "w10_menu", "worldmap.menu" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "w10b_menu_settings", "menu.settings" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "w10c_menu_no_save", "menu.save" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "w11_menu_closed", "menu.continue" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "w11b_menu_gone", "menu.continue" },
    { DEV_AUTOTEST_ACTION_FINGER, 0, 0.75f, 0.7f, 2, "w12_tap_down" },
    { DEV_AUTOTEST_ACTION_FINGER, 1, 0.75f, 0.7f, 150, "w13_travelling" },
    { DEV_AUTOTEST_ACTION_LOG_WORLDMAP, 0, 0, 0, 1, "w14_state" },
};

#define HUD_ELEMENT(name) static_cast<float>(static_cast<int>(HudElementId::name))

static const DevAutotestStep kDevAutotestHudSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 45, "h00_hud" },
    { DEV_AUTOTEST_ACTION_EQUIP_ITEM, 9, HAND_LEFT, 0, 20, "h01_smg" },
    { DEV_AUTOTEST_ACTION_LOG_HIT_MODE, 0, 0, 0, 1, "h02_hit_mode" },
    { DEV_AUTOTEST_ACTION_HUD_TAP_MODE, 2, 0, 0, 20, "h03_tap_mode_2" },
    { DEV_AUTOTEST_ACTION_LOG_HIT_MODE, 0, 0, 0, 1, "h04_hit_mode" },
    { DEV_AUTOTEST_ACTION_HUD_TAP_MODE, 1, 0, 0, 20, "h05_tap_mode_1" },
    { DEV_AUTOTEST_ACTION_LOG_HIT_MODE, 0, 0, 0, 1, "h06_hit_mode" },
    { DEV_AUTOTEST_ACTION_HUD_QUICK_TAP_MODE, 2, 0, 0, 20, "h06b_quick_tap_mode_2" },
    { DEV_AUTOTEST_ACTION_LOG_HIT_MODE, 0, 0, 0, 1, "h06c_hit_mode_burst" },
    { DEV_AUTOTEST_ACTION_HUD_QUICK_TAP_MODE, 0, 0, 0, 20, "h06d_quick_tap_mode_0" },
    { DEV_AUTOTEST_ACTION_LOG_HIT_MODE, 0, 0, 0, 1, "h06e_hit_mode_single" },
    { DEV_AUTOTEST_ACTION_EQUIP_ITEM, 7, HAND_LEFT, 0, 20, "h07_spear" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "h07b_center_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_LONG_PRESS_CRITTER, 0, 0, 0, 20, "h08_radial" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_RADIAL, GAME_MOUSE_ACTION_MENU_ITEM_LOOK, 0, 0, 20, "h09_toast" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Log), 0, 0, 20, "h10_log_open" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Log), 0, 0, 20, "h11_log_closed" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Sneak), 0, 0, 30, "h12_sneak" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 0, 0, 1, "h12b_give_stimpak" },
    // No party member yet: the skills list must stay usable (the HUD used
    // to close it every frame without the party orders button).
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Skills), 0, 0, 20, "h12c_skills_alone" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "h12d_sneak_from_list", "skills.0" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Highlight), 0, 0, 90, "h13_highlight" },
    { DEV_AUTOTEST_ACTION_HUD_LONG_PRESS, HUD_ELEMENT(Pipboy), 50, 0, 20, "h13a_pipboy_name" },
    { DEV_AUTOTEST_ACTION_FLOAT_TEXT, 0, 0, 0, 20, "h13f_float_text" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 97, 0, 0, 30, "h13f2_sulik_joins" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(PartyOrders), 0, 0, 20, "h13g_party_orders" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "h13h_order_auto_loot", "hud.orders.6" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Skills), 0, 0, 20, "h13j_skills" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "h13k_first_aid_target", "skills.4" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 60, "h13l_first_aid_on_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_LONG_PRESS_CRITTER, 0, 0, 0, 20, "h13m_radial" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_RADIAL, GAME_MOUSE_ACTION_MENU_ITEM_USE_SKILL, 0, 0, 20, "h13n_skills_on_critter" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "h13o_science_on_critter", "skills.6" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 8, 0, 0, 1, "h13p_give_8" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 29, 0, 0, 1, "h13p_give_29" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 46, 0, 0, 1, "h13p_give_46" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 74, 0, 0, 1, "h13p_give_74" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 4, 0, 0, 1, "h13p_give_4" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 10, 0, 0, 1, "h13p_give_10" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 47, 0, 0, 1, "h13p_give_47" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 48, 0, 0, 1, "h13p_give_48" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 49, 0, 0, 1, "h13p_give_49" },
    { DEV_AUTOTEST_ACTION_TOUCH_LONG_PRESS_CRITTER, 0, 0, 0, 20, "h13p_radial" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_RADIAL, GAME_MOUSE_ACTION_MENU_ITEM_INVENTORY, 0, 0, 20, "h13q_use_item_panel" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "h13q2_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "h13r_stimpak_on_critter", "useitem.pid.40" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "h13r2_log" },
    { DEV_AUTOTEST_ACTION_LOG_SLOT, 11, 0, 0, 1, "h13b_slot_before" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(QuickSave), 0, 0, 60, "h13c_quick_save" },
    { DEV_AUTOTEST_ACTION_LOG_SLOT, 11, 0, 0, 1, "h13d_slot_after" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(QuickSave), 0, 0, 60, "h13e_quick_save_2" },
    { DEV_AUTOTEST_ACTION_LOG_SLOT, 12, 0, 0, 1, "h13f_slot_2" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Inventory), 0, 0, 30, "h14_inventory" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 30, "h15_inventory_closed" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(SwapHands), 0, 0, 40, "h16_swap_hands" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 60, "h17_combat" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "h18_combat_state" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(EndTurn), 0, 0, 120, "h19_end_turn" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "h20_after_end_turn" },
};

static const DevAutotestStep kDevAutotestMagnetSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "m00_map" },
    { DEV_AUTOTEST_ACTION_ZOOM, 0.5f, -1, -1, 10, "m01_zoom050" },
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 10, "m02_center" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_TILE, 10, ROTATION_SW, 0, 8, "m03_walk" },
    { DEV_AUTOTEST_ACTION_LOG_MOVEMENT, 0, 0, 0, 1, "m04_walking" },
    // ~1.5 tiles aside at zoom 0.5 (radius is 24 dp = 32 px here).
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_MARKER, 14, 9, 0, 8, "m05_tap_near_marker" },
    { DEV_AUTOTEST_ACTION_LOG_MOVEMENT, 0, 0, 0, 150, "m06_running" },
    { DEV_AUTOTEST_ACTION_CHECK_WALK, 0, 0, 0, 1, "m07_arrived" },
    { DEV_AUTOTEST_ACTION_ZOOM, 3.0f, -1, -1, 10, "m08_zoom300" },
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 10, "m09_center" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_TILE, 3, ROTATION_NE, 0, 8, "m10_walk" },
    // Neighbor tile at zoom 300: not snapped, walks there.
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_MARKER, 96, 0, 0, 8, "m11_tap_neighbor" },
    { DEV_AUTOTEST_ACTION_LOG_MOVEMENT, 0, 0, 0, 150, "m12_not_running" },
    { DEV_AUTOTEST_ACTION_ZOOM, 1.0f, -1, -1, 10, "m13_zoom100" },
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 10, "m14_center" },
    { DEV_AUTOTEST_ACTION_PLACE_ITEM, 40, 4, ROTATION_SE, 10, "m15_item" },
    // Without highlight a tap next to an item walks.
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_ITEM, 12, 12, 0, 200, "m16_tap_near_item" },
    { DEV_AUTOTEST_ACTION_LOG_PLACED_ITEM, 0, 0, 0, 1, "m17_not_picked" },
    // With highlight: tap on the middle of a spear's box (thin sprite).
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 10, "m18_center" },
    { DEV_AUTOTEST_ACTION_PLACE_ITEM, 7, 4, ROTATION_NE, 10, "m19_spear" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Highlight), 0, 0, 20, "m20_highlight" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_ITEM, 0, 0, 1, 200, "m21_tap_spear" },
    { DEV_AUTOTEST_ACTION_LOG_PLACED_ITEM, 0, 0, 0, 1, "m22_spear_picked" },
    // With highlight: 25 px off (radius 24 dp = 32 px).
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 10, "m23_center" },
    { DEV_AUTOTEST_ACTION_PLACE_ITEM, 40, 4, ROTATION_SE, 10, "m24_item" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_ITEM, 18, 18, 0, 12, "m25_tap_near_item_highlight" },
    // Dude walks to the item: it's outlined green (map hints).
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 190, "m25b_walked" },
    { DEV_AUTOTEST_ACTION_LOG_PLACED_ITEM, 0, 0, 0, 1, "m26_picked" },
};

// Combat's tactical view: the HUD button turns it on (grid, tiles, the
// walk); a tap where the critter's sprite covers a tile picks the tile, a
// tap on the critter's tile picks the critter, a long press there opens its
// menu; the button turns it off; on again it hides after the combat and
// shows in the next one (kept until switched).
static const DevAutotestStep kDevAutotestTacticalSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "v00_map" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 60, "v01_combat" },
    { DEV_AUTOTEST_ACTION_CHECK_TACTICAL, 0, 0, 0, 1, "v02_off" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(TacticalView), 0, 0, 20, "v03_on" },
    { DEV_AUTOTEST_ACTION_CHECK_TACTICAL, 1, 0, 0, 1, "v04_shown" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "v05_center" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_BEHIND_CRITTER, 0, 1, 0, 20, "v06_tap_over_critter" },
    { DEV_AUTOTEST_ACTION_CHECK_SELECTION, 1, 0, 0, 1, "v07_tile_selected" },
    { DEV_AUTOTEST_ACTION_TOUCH_CRITTER_TILE, 0, 0, 0, 20, "v08_tap_critter_tile" },
    { DEV_AUTOTEST_ACTION_CHECK_SELECTION, 3, 0, 0, 1, "v09_critter_selected" },
    { DEV_AUTOTEST_ACTION_TOUCH_CRITTER_TILE, 0, 45, 0, 50, "v10_long_press_critter_tile" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "v11_menu", "radial.3" },
    // A tap beside the menu closes it (and taps nothing else).
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_SCREEN, 300, 150, 0, 20, "v12_menu_closed" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "v12b_no_menu", "radial.3" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(TacticalView), 0, 0, 20, "v13_off" },
    { DEV_AUTOTEST_ACTION_CHECK_TACTICAL, 0, 0, 0, 1, "v14_hidden" },
    // On again, then the turn ends - and the combat with it (the critter is
    // far): hidden, but kept on for the next combat.
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(TacticalView), 0, 0, 20, "v15_on_again" },
    { DEV_AUTOTEST_ACTION_CHECK_TACTICAL, 1, 0, 0, 1, "v16_shown" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(EndTurn), 0, 0, 120, "v17_end_turn" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "v18_combat_over" },
    { DEV_AUTOTEST_ACTION_CHECK_TACTICAL, 0, 1, 0, 1, "v19_hidden_kept" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 60, "v20_next_combat" },
    { DEV_AUTOTEST_ACTION_CHECK_TACTICAL, 1, 1, 0, 1, "v21_shown_again" },
};

// Combat: the critter picked (its outline pulses: two frames apart), then a
// tile behind it, partly covered by its sprite, chosen by a tap where it
// shows (the critter's selection doesn't keep it) and confirmed by a tap
// where the sprite covers it - the dude walks there.
// Screenshots for the README (not a test): a late game character - a
// neutral name, good stats, levels and their hit points, skill points
// spent, Advanced Power Armor, late weapons - at Navarro (the Enclave's
// base): the drill sergeant's welcome to a civilian (his head), then as a
// soldier with Marcus and Sulik: inventory with an item's action menu,
// character, normal combat with an enemy picked (its hit chance).
static const DevAutotestStep kDevAutotestPhotosSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "p00_start" },
    // "Избранный" in the game's charset.
    { DEV_AUTOTEST_ACTION_SET_NAME, 0, 0, 0, 1, "p01_name", "\xC8\xE7\xE1\xF0\xE0\xED\xED\xFB\xE9" },
    { DEV_AUTOTEST_ACTION_SET_STAT, STAT_STRENGTH, 10, 0, 1, "p01a_st" },
    { DEV_AUTOTEST_ACTION_SET_STAT, STAT_PERCEPTION, 8, 0, 1, "p01b_pe" },
    { DEV_AUTOTEST_ACTION_SET_STAT, STAT_ENDURANCE, 8, 0, 1, "p01c_en" },
    { DEV_AUTOTEST_ACTION_SET_STAT, STAT_CHARISMA, 4, 0, 1, "p01d_ch" },
    { DEV_AUTOTEST_ACTION_SET_STAT, STAT_INTELLIGENCE, 8, 0, 1, "p01e_in" },
    { DEV_AUTOTEST_ACTION_SET_STAT, STAT_AGILITY, 9, 0, 1, "p01f_ag" },
    { DEV_AUTOTEST_ACTION_SET_STAT, STAT_LUCK, 6, 0, 1, "p01g_lk" },
    { DEV_AUTOTEST_ACTION_ADD_EXPERIENCE, 250000, 0, 0, 30, "p02_levels" },
    // The character screen takes the levels (their skill points; the perks
    // owed are offered first - later).
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Character), 0, 0, 40, "p03_perks" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "p03a_later", "character.perkpanel.later" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p03b_closed", "character.nav.back" },
    { DEV_AUTOTEST_ACTION_ADD_SKILL, SKILL_SMALL_GUNS, 40, 0, 1, "p04a_small_guns" },
    { DEV_AUTOTEST_ACTION_ADD_SKILL, SKILL_ENERGY_WEAPONS, 30, 0, 1, "p04b_energy" },
    { DEV_AUTOTEST_ACTION_ADD_SKILL, SKILL_SPEECH, 15, 0, 1, "p04c_speech" },
    { DEV_AUTOTEST_ACTION_ADD_SKILL, SKILL_REPAIR, 15, 0, 1, "p04d_repair" },
    // Navarro's yard, by the drill sergeant (elevation 2, tile 25481): a
    // civilian there gets his famous welcome.
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 2, 25881, 0, 150, "p10_navarro", "navarro.map" },
    { DEV_AUTOTEST_ACTION_TALK_TO_PID, 250, 0, 0, 150, "p11_sergeant" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 60, "p12_sergeant_shouts" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 0, 0, 0, 120, "p13_after" },
    // Back in the yard as a soldier: Advanced Power Armor, late weapons,
    // Marcus (a minigun) and Sulik (a spear).
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 2, 25895, 0, 150, "p20_navarro_again", "navarro.map" },
    { DEV_AUTOTEST_ACTION_EQUIP_ITEM, 349, 0, 0, 10, "p21_armor" },
    { DEV_AUTOTEST_ACTION_EQUIP_ITEM, 392, HAND_LEFT, 0, 10, "p22_gauss" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 358, 20, 0, 1, "p23a_ammo" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 233, 0, 0, 1, "p23b_plasma" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 39, 20, 0, 1, "p23c_cells" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 350, 0, 0, 1, "p23d_bozar" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 144, 4, 0, 1, "p23e_super_stimpaks" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 8, 0, 1, "p23f_stimpaks" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 110, 2, 0, 1, "p23g_psycho" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 161, 3, ROTATION_W, 10, "p24a_marcus", "HCMARCUS.int" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 12, 0, 0, 1, "p24b_minigun" },
    { DEV_AUTOTEST_ACTION_WIELD_PARTY_ITEM, 12, 0, 0, 1, "p24c_marcus_wields" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 97, 3, ROTATION_SW, 10, "p24d_sulik", "Kcsulik.int" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 7, 0, 0, 1, "p24e_spear" },
    { DEV_AUTOTEST_ACTION_WIELD_PARTY_ITEM, 7, 0, 0, 30, "p24f_sulik_wields" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 40, "p25_inventory" },
    { DEV_AUTOTEST_ACTION_MUI_LONG_PRESS, 45, 0, 0, 30, "p25a_item_menu", "inventory.pid.233" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "p25b_menu_closed", "inventory.info" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p25c_inventory_closed", "inventory.back" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Character), 0, 0, 40, "p26_character" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p26a_later", "character.perkpanel.later" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p26b_closed", "character.nav.back" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 60, "p30_combat_started" },
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 30, "p31_combat" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 1, 0, 40, "p32_enemy_picked" },
};

static const DevAutotestStep kDevAutotestBehindCritterSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "c00_map" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 60, "c01_combat" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "c02_center" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 30, "c03_pick_critter" },
    { DEV_AUTOTEST_ACTION_CHECK_SELECTION, 3, 0, 0, 1, "c04_critter_selected" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 15, "c05_pulse_later" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_BEHIND_CRITTER, 0, 0, 0, 30, "c06_select_tile" },
    { DEV_AUTOTEST_ACTION_CHECK_SELECTION, 1, 0, 0, 1, "c07_tile_selected" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_BEHIND_CRITTER, 0, 1, 0, 150, "c08_confirm_over_critter" },
    { DEV_AUTOTEST_ACTION_CHECK_TOUCH_MOVED, 0, 0, 0, 1, "c09_moved" },
};

static const DevAutotestStep kDevAutotestUiSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "u00_map" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(QuickLoad), 0, 0, 30, "u01_quick_load_dialog" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "u02_no", "dialog.secondary" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(QuickLoad), 0, 0, 30, "u03_dialog_again" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 1, 0, 0, 20, "u04_quick_tap_no", "dialog.secondary" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 10, "u05_closed" },
};

static const DevAutotestStep kDevAutotestTalkSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "t00_map" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "t01_center_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 300, "t02_talk" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "t03_option_0", "talk.options.0" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "t04_review", "dialog.tab.review" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 30, "t05_review_closed" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 1, 0, 0, 90, "t06_option_0_quick", "talk.options.0" },
};

static const DevAutotestStep kDevAutotestBarterSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "b00_map" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 0, 0, 1, "b01_give_stimpak" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 0, 0, 1, "b02_give_stimpak" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 0, 0, 1, "b03_give_stimpak" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 4, 0, 0, 1, "b04_give_knife" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "b05_center_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 300, "b06_talk" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "b07_barter", "dialog.tab.barter" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "b08_tap_item", "barter.player.pid.40" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "b09_quantity_done", "quantity.done" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 40, "b10_drag_item", "barter.player.pid.40", "barter.offer" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "b10b_quantity_plus", "quantity.plus" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "b10c_quantity_done", "quantity.done" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "b11_filter_weapons", "barter.filter.player.1" },
    { DEV_AUTOTEST_ACTION_MUI_LONG_PRESS, 45, 0, 0, 20, "b11b_item_menu", "barter.player.0" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "b11c_look", "radial.3" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "b12_offer", "barter.offer.button" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "b13_back_to_talk", "dialog.tab.talk" },
};

// Ammo shown in rounds: a stack of three clips, the first not full, and one
// clip not full; under the finger, the quantity picker (rounds by clips,
// weight, price in a deal), the barter tables.
static const DevAutotestStep kDevAutotestAmmoSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "a00_map" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 29, 3, 10, 1, "a01_give_jhp" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 30, 1, 7, 1, "a02_give_ap" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 30, "a03_inventory" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "a03b_log" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 18, 40, "a04_drop_jhp", "inventory.pid.29", "inventory.info" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "a05_plus", "quantity.plus" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "a06_plus", "quantity.plus" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "a07_cancel", "quantity.cancel" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "a08_back", "inventory.back" },
    { DEV_AUTOTEST_ACTION_GIVE_CRITTER_ITEM, 30, 2, 5, 1, "a08b_trader_ammo" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "a09_center_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 300, "a10_talk" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "a11_barter", "dialog.tab.barter" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 18, 40, "a12_offer_jhp", "barter.player.pid.29", "barter.offer" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "a13_plus", "quantity.plus" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "a14_done", "quantity.done" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 18, 40, "a15_offer_ap", "barter.player.pid.30", "barter.offer" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "a16_log" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 18, 40, "a17_request", "barter.barterer.pid.30", "barter.request" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "a17b_plus", "quantity.plus" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "a18_done", "quantity.done" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "a19_back_to_talk", "dialog.tab.talk" },
};

// A conversation through several branches with barter in the middle; the
// logs of the dialog's state are compared between builds.
static const DevAutotestStep kDevAutotestDialogTraceSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "d00_map" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "d00b_windows" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d00c_state" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 2, 0, 1, "d01_give_stimpaks" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 41, 150, 0, 1, "d01b_give_money" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "d02_center_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 300, "d03_talk" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d03b_state" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "d03c_windows" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "d04_barter", "dialog.tab.barter" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d04b_state" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "d04c_windows" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d05_request_item", "barter.barterer.0" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d05b_quantity_done", "quantity.done" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d06_offer_too_little", "barter.offer.button" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d06b_state" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d07_offer_item", "barter.player.0" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d07b_quantity_done", "quantity.done" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d07c_offer_item", "barter.player.0" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d07d_quantity_done", "quantity.done" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d07e_state" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d08_offer", "barter.offer.button" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d08b_state" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d09_left_on_table", "barter.player.0" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d09b_quantity_done", "quantity.done" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d09c_state" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 90, "d10_back_to_talk" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d10b_state" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "d10c_windows" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "d11_ask", "talk.options.1" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d11b_state" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "d12_fine", "talk.options.0" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d12b_state" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "d12c_windows" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 300, "d13_talk_again" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d13b_state" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "d14_trial", "talk.options.2" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d14b_state" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "d15_option_0", "talk.options.0" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d15b_state" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "d15c_windows" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "d16_inventory" },
};

// A talk with a talking head (the Elder of Arroyo, `--dev-map=arvillag.map`),
// by the mobile UI's way or the game's windows; the logs are compared.
static const DevAutotestStep kDevAutotestHeadTraceSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "h00_map" },
    { DEV_AUTOTEST_ACTION_LOG_CRITTERS, 0, 0, 0, 1, "h00b_critters" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "h00d_windows" },
    { DEV_AUTOTEST_ACTION_TALK_TO_PID, 13, 0, 0, 30, "h01_talk" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 900, 0, 0, 60, "h01b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h01c_state" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "h01d_windows" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 0, 0, 0, 30, "h02_choose_0" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 60, "h02b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h02c_state" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 0, 0, 0, 30, "h03_choose_0" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 60, "h03b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h03c_state" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 1, 0, 0, 30, "h04_choose_1" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 60, "h04b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h04c_state" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 0, 0, 0, 30, "h05_choose_0" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 60, "h05b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h05c_state" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 0, 0, 0, 30, "h06_choose_0" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 60, "h06b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h06c_state" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 0, 0, 0, 30, "h07_choose_0" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 60, "h07b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h07c_state" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 0, 0, 0, 30, "h08_choose_0" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 60, "h08b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h08c_state" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 0, 0, 0, 30, "h09_choose_0" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 60, "h09b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h09c_state" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "h99_windows" },
};

// Barter with Clint (at the new game's start) by the mobile UI's way or the
// game's windows; the logs are compared.
static const DevAutotestStep kDevAutotestBarterTraceSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "r00_map" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 41, 20, 0, 1, "r01_give_money" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "r02_center_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 30, "r03_talk" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 900, 0, 0, 30, "r03b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "r03c_state" },
    { DEV_AUTOTEST_ACTION_DIALOG_BARTER, 0, 0, 0, 90, "r04_barter" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "r04b_state" },
    { DEV_AUTOTEST_ACTION_BARTER_OFFER, 0, 0, 0, 30, "r05_offer_nothing" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "r05b_state" },
    { DEV_AUTOTEST_ACTION_BARTER_TALK, 0, 0, 0, 30, "r06_talk" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 30, "r06b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "r06c_state" },
    { DEV_AUTOTEST_ACTION_DIALOG_BARTER, 0, 0, 0, 90, "r07_barter_again" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "r07b_state" },
    { DEV_AUTOTEST_ACTION_BARTER_TALK, 0, 0, 0, 30, "r08_talk" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 30, "r08b_talking" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 1, 0, 0, 30, "r09_ask" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 30, "r09b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "r09c_state" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 0, 0, 0, 90, "r10_fine" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "r10b_state" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "r11_inventory" },
};

// The game's interface bar (run with [touch] hud=0 and mobile_ui=0): hands,
// attack modes, combat's end buttons, by the game's keys.
static const DevAutotestStep kDevAutotestPcBarSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "a00_start" },
    { DEV_AUTOTEST_ACTION_EQUIP_ITEM, 9, HAND_LEFT, 0, 20, "a01_smg" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_N, 0, 0, 20, "a02_next_mode" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_B, 0, 0, 60, "a03_swap_hands" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_B, 0, 0, 60, "a04_swap_back" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 120, "a05_combat" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_SPACE, 0, 0, 240, "a06_end_turn" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_RETURN, 0, 0, 240, "a07_end_combat" },
};

static Object* gDevAutotestPlacedItem = nullptr;

// Trace scenarios: the items' state is logged after every step.
static bool gDevAutotestTraceItems = false;

// Windows there were at the first LOG_NEW_WINDOWS.
static std::set<int> gDevAutotestStartWindows;
static bool gDevAutotestStartWindowsTaken = false;
static Object* gDevAutotestCorpse = nullptr;
static Object* gDevAutotestPartyMember = nullptr;

// SET_PARTY_HP: the party member's healing drugs and hit points then.
static int gDevAutotestPartyHealing = 0;
static int gDevAutotestPartyHitPoints = 0;

// WALK_LIMITED: the dude's tile when it started.
static int gDevAutotestWalkStart = -1;

// Healing drugs (stimpaks, healing powder) the critter carries.
static int devAutotestHealingDrugs(Object* critter)
{
    int count = 0;
    for (int index = 0; index < critter->data.inventory.length; index++) {
        InventoryItem* inventoryItem = &(critter->data.inventory.items[index]);
        if (itemGetType(inventoryItem->item) == ITEM_TYPE_DRUG && itemIsHealing(inventoryItem->item)) {
            count += inventoryItem->quantity;
        }
    }
    return count;
}

// The party member of ADD_PARTY_MEMBER, else the first one of a loaded game.
static Object* devAutotestPartyMember()
{
    if (gDevAutotestPartyMember != nullptr) {
        return gDevAutotestPartyMember;
    }

    for (Object* member : get_all_party_members_objects(false)) {
        if (member != gDude) {
            return member;
        }
    }
    return nullptr;
}

static const DevAutotestStep* gDevAutotestSteps = kDevAutotestSteps;
static int gDevAutotestStepCount = sizeof(kDevAutotestSteps) / sizeof(kDevAutotestSteps[0]);

// Synthetic single finger touch: finger goes down at start, stays there for
// `holdFrames`, moves to end during `moveFrames` and goes up.
struct DevAutotestTouch {
    float startX;
    float startY;
    float endX;
    float endY;
    int holdFrames;
    int moveFrames;
    // Frame to capture the screen at while touching (-1 - none).
    int captureFrame;
    const char* captureName;
};

static DevAutotestTouch gDevAutotestTouch;
static int gDevAutotestTouchFrame = -1;
// Frames to wait before the touch starts.
static int gDevAutotestTouchDelay = 0;

static int gDevAutotestWalkTarget = -1;
static int gDevAutotestTouchStartTile = -1;

static constexpr int kDevAutotestPinchFrames = 20;

static bool gDevAutotestEnabled = false;
static std::string gDevAutotestOutputPath;
static int gDevAutotestStep = 0;
static int gDevAutotestDelay = 0;
static bool gDevAutotestStepStarted = false;

// TIME_DUDE_ANIMATION being timed.
static const char* gDevAutotestTimedName = nullptr;
static unsigned int gDevAutotestTimedStart = 0;
static int gDevAutotestTimedTile = -1;

static int gDevAutotestSlowFrameMs = 0;

// WALK_UNDER_ROOF: where dude went in from.
static int gDevAutotestRoofStartTile = -1;

// PAN_FRAMES in progress.
static float gDevAutotestPanX = 0.0f;
static float gDevAutotestPanY = 0.0f;
static int gDevAutotestPanFrames = 0;

// PERF_LOG: frames so far, the previous log's frame, time and uploads.
static int gDevAutotestFrames = 0;
static int gDevAutotestPerfFrame = 0;
static unsigned int gDevAutotestPerfTime = 0;
static long long gDevAutotestPerfUploads = 0;
static long long gDevAutotestPerfPixels = 0;
static long long gDevAutotestPerfDrawn = 0;
static const char* gDevAutotestPendingCapture = nullptr;
static int gDevAutotestPinchFrame = -1;

static void devAutotestPushFinger(Uint32 type, SDL_FingerID finger, float x, float y)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = type;
    event.tfinger.touchId = 1;
    event.tfinger.fingerId = finger;
    event.tfinger.x = x;
    event.tfinger.y = y;
    event.tfinger.pressure = 1.0f;
    SDL_PushEvent(&event);
}

// Moves two synthetic fingers symmetrically around a centroid. Returns true
// when the gesture is complete.
static bool devAutotestPinchTick(const DevAutotestStep* step)
{
    gDevAutotestPinchFrame++;

    float progress = static_cast<float>(gDevAutotestPinchFrame) / kDevAutotestPinchFrames;
    float span = step->a + (step->b - step->a) * std::min(progress, 1.0f);
    float centerX = 0.5f + step->c * std::min(progress, 1.0f);
    float centerY = 0.4f;

    Uint32 type = gDevAutotestPinchFrame == 0 ? SDL_FINGERDOWN : SDL_FINGERMOTION;
    if (gDevAutotestPinchFrame > kDevAutotestPinchFrames) {
        type = SDL_FINGERUP;
    }

    devAutotestPushFinger(type, 1, centerX - span / 2, centerY);
    devAutotestPushFinger(type, 2, centerX + span / 2, centerY);

    return type == SDL_FINGERUP;
}

static void devAutotestStartTouch(float startX, float startY, float endX, float endY, int holdFrames, int moveFrames, int captureFrame = -1, const char* captureName = nullptr)
{
    gDevAutotestTouch = { startX, startY, endX, endY, holdFrames, moveFrames, captureFrame, captureName };
    gDevAutotestTouchFrame = 0;
    devAutotestLog("  touch (%.0f, %.0f) -> (%.0f, %.0f), hold %d, move %d frames, world %d, map input %d\n", startX, startY, endX, endY, holdFrames, moveFrames,
        worldViewIsWorldAt(static_cast<int>(startX), static_cast<int>(startY)) ? 1 : 0,
        gameMouseIsMapInputEnabled() ? 1 : 0);
}

// Returns true when the touch is complete.
static bool devAutotestTouchTick()
{
    if (gDevAutotestTouchDelay > 0) {
        gDevAutotestTouchDelay--;
        return false;
    }

    const DevAutotestTouch& touch = gDevAutotestTouch;
    int frame = gDevAutotestTouchFrame++;

    if (frame == touch.captureFrame) {
        gDevAutotestPendingCapture = touch.captureName;
    }

    float width = static_cast<float>(screenGetWidth());
    float height = static_cast<float>(screenGetHeight());

    if (frame == 0) {
        devAutotestPushFinger(SDL_FINGERDOWN, 1, touch.startX / width, touch.startY / height);

        // Quick tap: finger up arrives with finger down.
        if (touch.holdFrames < 0) {
            devAutotestPushFinger(SDL_FINGERUP, 1, touch.endX / width, touch.endY / height);
            gDevAutotestTouchFrame = -1;
            return true;
        }
        return false;
    }

    if (frame <= touch.holdFrames) {
        return false;
    }

    if (frame <= touch.holdFrames + touch.moveFrames) {
        float progress = static_cast<float>(frame - touch.holdFrames) / touch.moveFrames;
        float x = touch.startX + (touch.endX - touch.startX) * progress;
        float y = touch.startY + (touch.endY - touch.startY) * progress;
        devAutotestPushFinger(SDL_FINGERMOTION, 1, x / width, y / height);
        return false;
    }

    devAutotestPushFinger(SDL_FINGERUP, 1, touch.endX / width, touch.endY / height);
    gDevAutotestTouchFrame = -1;
    return true;
}

// Returns the nearest visible critter other than dude.
// Script [fileName] (scripts.lst) on [critter], as create_object_sid does.
static void devAutotestAttachScript(Object* critter, const char* fileName)
{
    if (fileName == nullptr) {
        return;
    }

    int index = -1;
    char name[64];
    for (int candidate = 0; candidate < scriptsGetListLength(); candidate++) {
        if (scriptsGetFileName(candidate, name, sizeof(name)) == 0 && compat_stricmp(name, fileName) == 0) {
            index = candidate;
            break;
        }
    }

    if (index == -1) {
        devAutotestLog("  script %s: NOT FOUND\n", fileName);
        return;
    }

    if (critter->sid != -1) {
        scriptRemove(critter->sid);
        critter->sid = -1;
    }

    Script* script;
    if (scriptAdd(&(critter->sid), SCRIPT_TYPE_CRITTER) == -1 || scriptGetScript(critter->sid, &script) == -1) {
        return;
    }

    script->index = index;
    critter->id = scriptsNewObjectId();
    script->ownerId = critter->id;
    script->owner = critter;
    _scr_find_str_run_info(index, &(script->field_50), critter->sid);
    devAutotestLog("  script %s (index %d)\n", fileName, index);
}

static Object* devAutotestFindNearestCritter()
{
    Object* critter = nullptr;
    int bestDistance = 0;
    for (Object* object = objectFindFirstAtElevation(gElevation); object != nullptr; object = objectFindNextAtElevation()) {
        if (object == gDude || FrmId(object).objectType() != OBJ_TYPE_CRITTER || (object->flags & OBJECT_HIDDEN) != 0 || critterIsDead(object)) {
            continue;
        }

        int distance = objectGetDistanceBetween(gDude, object);
        if (critter == nullptr || distance < bestDistance) {
            critter = object;
            bestDistance = distance;
        }
    }
    return critter;
}

// The nearest living critter not in the party the dude can shoot at.
static Object* devAutotestFindNearestEnemyInSight()
{
    Object* critter = nullptr;
    int bestDistance = 0;
    for (Object* object = objectFindFirstAtElevation(gElevation); object != nullptr; object = objectFindNextAtElevation()) {
        if (object == gDude || FrmId(object).objectType() != OBJ_TYPE_CRITTER || (object->flags & OBJECT_HIDDEN) != 0 || critterIsDead(object)
            || objectIsPartyMember(object) || _combat_is_shot_blocked(gDude, gDude->tile, object->tile, object, nullptr)) {
            continue;
        }

        int distance = objectGetDistanceBetween(gDude, object);
        if (critter == nullptr || distance < bestDistance) {
            critter = object;
            bestDistance = distance;
        }
    }
    return critter;
}

// Returns screen position of the center of the object.
static void devAutotestGetObjectScreenPosition(Object* object, float* x, float* y)
{
    Rect rect;
    objectGetRect(object, &rect);
    int screenX;
    int screenY;
    worldViewWorldToScreen((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2, &screenX, &screenY);
    *x = static_cast<float>(screenX);
    *y = static_cast<float>(screenY);
}

// Returns screen position of the center of the tile.
static void devAutotestGetTileScreenPosition(int tile, float* x, float* y)
{
    int worldX;
    int worldY;
    tileToScreenXY(tile, &worldX, &worldY);
    int screenX;
    int screenY;
    worldViewWorldToScreen(worldX + 16, worldY + 8, &screenX, &screenY);
    *x = static_cast<float>(screenX);
    *y = static_cast<float>(screenY);
}

// Logs whether files only present in Restoration Project are visible.
static void devAutotestCheckMods()
{
    const char* paths[] = {
        "maps\\abbey.map",
        "art\\intrface\\twnabbey.frm",
    };

    for (const char* path : paths) {
        int size = 0;
        bool found = dbGetFileSize(path, &size) == 0;
        devAutotestLog("  mod file %s: %s (%d bytes)\n", path, found ? "found" : "MISSING", size);
    }
}

// Mobile UI inventory screen: ammo into a weapon, item into a bag, armor
// and hands, bag opened and closed, item taken out, dropping.
static const DevAutotestStep kDevAutotestInventorySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "i00_map" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 8, 0, 0, 1, "i01_give_pistol" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 29, 0, 0, 1, "i02_give_ammo" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 29, 0, 0, 1, "i02b_give_ammo" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 46, 0, 0, 1, "i03_give_bag" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 74, 0, 0, 1, "i04_give_jacket" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 4, 0, 0, 1, "i05_give_knife" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 30, "i06_open" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "i06b_log" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "i06c_turn", "inventory.figure", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "i07_info_pistol", "inventory.pid.8" },
    { DEV_AUTOTEST_ACTION_MUI_LONG_PRESS, 45, 0, 0, 20, "i07b_pistol_menu", "inventory.pid.8" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "i07c_unload", "radial.7" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "i07d_log" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "i08_ammo_into_pistol", "inventory.pid.29", "inventory.pid.8" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "i08b_log" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "i09_knife_into_bag", "inventory.pid.4", "inventory.pid.46" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "i10_wear_jacket", "inventory.pid.74", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "i11_pistol_to_hand", "inventory.pid.8", "inventory.slot.right" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "i11b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "i12_info_jacket", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_MUI_LONG_PRESS, 45, 0, 0, 20, "i13_bag_menu", "inventory.pid.46" },
    { DEV_AUTOTEST_ACTION_MUI_LONG_PRESS, 20, 0, 0, 30, "i14_open_bag", "radial.6" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "i15_take_knife_out", "inventory.list.0", "inventory.container" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "i16_close_bag", "inventory.container" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "i16b_log" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "i17_drop_knife", "inventory.pid.4", "inventory.info" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "i18_filter_ammo", "inventory.filter.5" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 10, "i19_drop_all", "inventory.drop_all" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "i20_drop_all_confirm", "inventory.drop_all" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "i21_filter_all", "inventory.filter.0" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "i21b_log" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 0, 0, 1, "i21c_give_stimpak" },
    { DEV_AUTOTEST_ACTION_MUI_LONG_PRESS, 45, 0, 0, 20, "i21d_stimpak_menu", "inventory.pid.40" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 3, "i21e_use_stimpak", "radial.6" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 10, "i21f_after_use" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "i21g_after_use" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 0, 0, 10, "i21h_give_drag_stimpak" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "i21i_before_drag_use" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "i21j_drag_stimpak_to_figure", "inventory.pid.40", "inventory.figure" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "i21k_after_drag_use" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 4, 0, 0, 10, "i21l_give_nonconsumable" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "i21m_drag_knife_to_figure", "inventory.pid.4", "inventory.figure" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "i21n_after_rejected_drag" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "i22_back", "inventory.back" },
};

// The game's inventory and loot windows (run with [touch] mobile_ui=0,
// out/dev/runpc.sh): they open, show and close by Esc.
static const DevAutotestStep kDevAutotestPcInventorySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "w00_map" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "w00b_windows" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 8, 0, 0, 1, "w01_give_pistol" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 46, 0, 0, 1, "w02_give_bag" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 30, "w03_inventory" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "w03b_windows" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_ESCAPE, 0, 0, 30, "w04_closed" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "w04b_windows" },
    { DEV_AUTOTEST_ACTION_KILL_CRITTER, 0, 0, 0, 60, "w05_kill" },
    { DEV_AUTOTEST_ACTION_GIVE_CORPSE_ITEM, 29, 0, 0, 1, "w06_corpse_ammo" },
    { DEV_AUTOTEST_ACTION_CENTER_CORPSE, 0, 0, 0, 10, "w07_center" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CORPSE, 0, 0, 0, 240, "w08_loot" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "w08b_windows" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_A, 0, 0, 30, "w09_take_all" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_ESCAPE, 0, 0, 30, "w10_closed" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "w10b_windows" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "w11_inventory" },
};

// Mobile UI loot screen: moving items both ways, filtered "Take all", a
// bag opened on the looted side, "Give all".
static const DevAutotestStep kDevAutotestLootSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "l00_map" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 4, 0, 0, 1, "l01_give_knife" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 0, 0, 1, "l02_give_stimpak" },
    { DEV_AUTOTEST_ACTION_KILL_CRITTER, 0, 0, 0, 60, "l03_kill" },
    { DEV_AUTOTEST_ACTION_GIVE_CORPSE_ITEM, 8, 0, 0, 1, "l04_corpse_pistol" },
    { DEV_AUTOTEST_ACTION_GIVE_CORPSE_ITEM, 29, 0, 0, 1, "l05_corpse_ammo" },
    { DEV_AUTOTEST_ACTION_GIVE_CORPSE_ITEM, 46, 0, 0, 1, "l06_corpse_bag" },
    { DEV_AUTOTEST_ACTION_GIVE_CORPSE_ITEM, 40, 0, 0, 1, "l07_corpse_stimpak" },
    { DEV_AUTOTEST_ACTION_CENTER_CORPSE, 0, 0, 0, 10, "l08_center" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CORPSE, 0, 0, 0, 240, "l09_open_loot" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "l09b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "l10_take_pistol", "loot.right.pid.8" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "l11_give_knife", "loot.left.pid.4", "loot.right" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "l12_filter_ammo", "loot.filter.right.5" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "l13_take_ammo", "loot.take_all" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "l14_filter_all", "loot.filter.right.0" },
    { DEV_AUTOTEST_ACTION_MUI_LONG_PRESS, 45, 0, 0, 20, "l15_bag_menu", "loot.right.pid.46" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "l16_open_bag", "radial.6" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "l17_close_bag", "loot.container.right" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "l18_take_all", "loot.take_all" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "l18b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "l19_filter_drugs", "loot.filter.left.4" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "l20_give_all", "loot.give_all" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "l20b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "l21_back", "loot.back" },
};

// Test save for the phone: Arroyo village, varied inventory, two party
// members (run with --dev-map=arvillag.map).
static const DevAutotestStep kDevAutotestLoadSaveSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "v00_start" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 9, 0, 0, 90, "v01_loaded" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "v02_log" },
    { DEV_AUTOTEST_ACTION_LOG_PARTY, 0, 0, 0, 1, "v03_party" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(PartyOrders), 0, 0, 20, "v04_orders" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 12, "v05_holster", "hud.orders.0" },
    { DEV_AUTOTEST_ACTION_LOG_PARTY, 0, 0, 0, 1, "v06_party_after" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 200, "v07_talk_nearest" },
};

// Own empty SAVEGAME, test game's quick saves: one page from page 1. Check actual engine
// round trips as well as making a quick save permanent, new saves and session
// quickload.
static const DevAutotestStep kDevAutotestSaveHistorySteps[] = {
    { DEV_AUTOTEST_ACTION_SET_NAME, 0, 0, 0, 1, "h00_name", "ANCHOR" },
    { DEV_AUTOTEST_ACTION_SAVE_GAME, 0, 0, 0, 10, "h01_manual", "Manual anchor" },
    { DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION, 0, 0, 0, 1, "h02_anchor", "ANCHOR" },
    { DEV_AUTOTEST_ACTION_SET_NAME, 0, 0, 0, 1, "h03_name", "QUICK" },
    { DEV_AUTOTEST_ACTION_QUICK_SAVE, 0, 0, 0, 10, "h04_quick" },
    { DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION, 10, 0, 0, 1, "h05_quick_anchor", "QUICK" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 0, 0, 0, 20, "h06_old_manual" },
    { DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION, 0, 0, 0, 1, "h07_loaded_anchor", "ANCHOR" },
    { DEV_AUTOTEST_ACTION_SET_NAME, 0, 0, 0, 1, "h08_dirty", "DIRTY" },
    { DEV_AUTOTEST_ACTION_QUICK_LOAD, 0, 0, 0, 20, "h09_quick_load_manual" },
    { DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION, 0, 0, 0, 1, "h10_restored", "ANCHOR" },
    { DEV_AUTOTEST_ACTION_QUICK_SAVE, 0, 0, 0, 10, "h11_next_quick" },
    { DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION, 11, 0, 0, 1, "h12_independent_ring", "ANCHOR" },
    { DEV_AUTOTEST_ACTION_SET_NAME, 0, 0, 0, 1, "h13_name", "MANUAL" },
    { DEV_AUTOTEST_ACTION_SAVE_GAME, 1, 0, 0, 10, "h14_new_manual", "Latest manual" },
    { DEV_AUTOTEST_ACTION_SET_NAME, 0, 0, 0, 1, "h15_dirty", "DIRTY" },
    { DEV_AUTOTEST_ACTION_QUICK_LOAD, 0, 0, 0, 20, "h16_quick_load_new_manual" },
    { DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION, 1, 0, 0, 1, "h17_restored", "MANUAL" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 20, "h18_menu" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "h19_load", "menu.load" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "h20_select_quick", "loadsave.slots.11" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "h21_permanent", "loadsave.permanent" },
    { DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION, 1, 0, 0, 1, "h22_permanent_keeps_anchor", "MANUAL" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "h23_load_permanent", "loadsave.action" },
    { DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION, 2, 0, 0, 1, "h24_permanent_is_original_snapshot", "QUICK" },
    { DEV_AUTOTEST_ACTION_CHECK_LOADED_WORLD, 0, 0, 0, 20, "h25_world" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 20, "h26_menu" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "h27_save", "menu.save" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "h28_save_selected", "loadsave.slots.3" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "h29_new_not_overwrite", "loadsave.action" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "h30_save", "loadsave.action" },
    { DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION, 3, 0, 0, 1, "h31_new_manual_anchor", "QUICK" },
};

// Native menu and save/load flow: create a save, select an existing one, return to New, load the original.
static const DevAutotestStep kDevAutotestMuiLoadSaveSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "m00_start" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "m01_menu" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "m02_save", "menu.save" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 10, "m03_description", "loadsave.description" },
    { DEV_AUTOTEST_ACTION_TYPE_TEXT, 0, 0, 0, 10, "m04_type", " TEST" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "m05_done", "loadsave.description.done" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 100, "m06_save_game", "loadsave.action" },
    { DEV_AUTOTEST_ACTION_LOG_SLOT, 1, 0, 0, 1, "m07_saved" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "m08_menu_again" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "m09_save_again", "menu.save" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "m10_existing", "loadsave.slots.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "m11_new_save", "loadsave.action" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "m13_back", "loadsave.back" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "m14_load", "menu.load" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "m15_slot", "loadsave.slots.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 100, "m16_load_game", "loadsave.action" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "m17_loaded" },
};

// Settings screen from the game menu: a change marks its row and section,
// volumes preview live, Back with changes asks (No keeps the settings), Apply
// sets them, Reset all (confirmed) and Yes on Back set the defaults.
// runtest.sh puts fallout2.cfg back after the test.
// The settings' "Game files" section: its actions ask first (Android does
// them in the app's import screen).
static const DevAutotestStep kDevAutotestGameFilesSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "g00_start" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "g01_menu" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "g02_settings", "menu.settings" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "g03_files", "prefs.sections.4" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "g04_export_asks", "prefs.files.export" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "g05_no", "dialog.secondary" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "g06_delete_asks", "prefs.files.delete" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "g07_no", "dialog.secondary" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "g08_game", "prefs.sections.0" },
};

static const DevAutotestStep kDevAutotestPreferencesSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "p00_start" },
    { DEV_AUTOTEST_ACTION_LOG_PREFERENCES, 0, 0, 0, 1, "p01_before" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "p02_menu" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p03_settings", "menu.settings" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "p04_violence", "prefs.rows.251.3" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "p05_combat", "prefs.sections.1" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, 0.1f, 0, 0, 40, "p06_speed_dragged", "prefs.rows.265" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "p07_all_animations", "prefs.rows.267" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "p08_sound", "prefs.sections.2" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, -0.1f, 0, 0, 40, "p09_master_dragged", "prefs.rows.273" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "p10_display", "prefs.sections.3" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, 0.5f, 0, 0, 40, "p11_brightness_dragged", "prefs.rows.277" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p12_back_asks", "prefs.back" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "p13_no", "dialog.secondary" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "p14_closed", "prefs.apply" },
    { DEV_AUTOTEST_ACTION_LOG_PREFERENCES, 0, 0, 0, 1, "p15_unchanged" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p16_settings_again", "menu.settings" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "p17_combat", "prefs.sections.1" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, 0.1f, 0, 0, 40, "p18_speed_dragged", "prefs.rows.265" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "p19_all_animations", "prefs.rows.267" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "p20_applied", "prefs.apply" },
    { DEV_AUTOTEST_ACTION_LOG_PREFERENCES, 0, 0, 0, 1, "p21_applied" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p22_reset_asks", "prefs.reset" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p23_reset", "dialog.primary" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p24_back_asks", "prefs.back" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "p25_yes", "dialog.primary" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "p26_closed", "prefs.apply" },
    { DEV_AUTOTEST_ACTION_LOG_PREFERENCES, 0, 0, 0, 1, "p27_defaults" },
};

// Own SAVEGAME, 16 saves (manual 1, quick 11-20, manual 21-25): the number of
// quick saves grows (no question), goes off (asked: they become permanent)
// and back on - no save lost on the way.
static const DevAutotestStep kDevAutotestQuickSavesSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "q00_start" },
    { DEV_AUTOTEST_ACTION_LOG_SAVES, 16, 0, 0, 1, "q01_saves" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "q02_menu" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "q03_settings", "menu.settings" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, 0, -0.45f, 0, 40, "q04_scrolled", "prefs.rows.253" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, 0.15f, 0, 0, 40, "q05_more", "prefs.rows.360" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "q06_applied", "prefs.apply" },
    { DEV_AUTOTEST_ACTION_LOG_SAVES, 16, 0, 0, 1, "q07_saves" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, -0.6f, 0, 0, 40, "q08_off", "prefs.rows.360" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "q09_asks", "prefs.apply" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "q10_yes", "dialog.primary" },
    { DEV_AUTOTEST_ACTION_LOG_SAVES, 16, 0, 0, 1, "q11_saves" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, 0.03f, 0, 0, 40, "q12_on", "prefs.rows.360" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "q13_applied", "prefs.apply" },
    { DEV_AUTOTEST_ACTION_LOG_SAVES, 16, 0, 0, 1, "q14_saves" },
};

// The settings' About section: notices, the licenses scrolled through.
static const DevAutotestStep kDevAutotestAboutSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "a00_start" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "a01_menu" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "a02_settings", "menu.settings" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "a03_about", "prefs.sections.5" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, 0, -0.5f, 0, 40, "a05_scrolled", "prefs.about" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, 0, -0.5f, 0, 40, "a06_scrolled", "prefs.about" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "a07_source_button", "prefs.about.source" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "a08_licenses", "prefs.about.licenses" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "a09_licenses_open", "licenses.back" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, 0, -0.5f, 0, 30, "a10_licenses_scrolled", "licenses.text" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 30, "a11_licenses_closed" },
};

// A single update action, progress, closing while downloading, restored
// readiness, deleting the file, automatic install only in the active dialog,
// cancellation of the system installer, permission and retry states.
static const DevAutotestStep kDevAutotestUpdateSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "up00_title" },
    { DEV_AUTOTEST_ACTION_UPDATE_STATE, 0, 0, 0, 3, "up01_idle" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "up02_no_banner", "mainmenu.update" },
    { DEV_AUTOTEST_ACTION_UPDATE_STATE, 3, 0, 0, 3, "up03_available", "v1.3.0-mobile.99" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "up04_banner", "mainmenu.update" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "up05_notes", "mainmenu.update" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "up06_action", "update.download" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 8, "up07_downloading", "update.download" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "up08_progress", "update.progress" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "up09_closed", "update.back" },
    { DEV_AUTOTEST_ACTION_UPDATE_STATE, 5, 0, 0, 3, "up10_ready", "v1.3.0-mobile.99" },
    { DEV_AUTOTEST_ACTION_CHECK_UPDATE_INSTALLS, 0, 0, 0, 1, "up11_no_surprise_install" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "up12_ready_dialog", "mainmenu.update" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "up13_install_button", "update.install" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "up13b_no_delete_in_dialog", "update.delete" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "up13c_dialog_closed", "update.back" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "up13d_settings", "mainmenu.settings" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "up13e_ready_about", "prefs.sections.5" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "up13f_update_dot", "prefs.about.update.notice" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 8, "up14_deleted", "prefs.about.update.delete" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "up14b_no_cached_file", "prefs.about.update.delete" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "up14c_no_apply", "prefs.apply" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "up14d_main_menu", "prefs.back" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "up14e_update_dialog", "mainmenu.update" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "up15_no_install", "update.install" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 8, "up16_download_again", "update.download" },
    { DEV_AUTOTEST_ACTION_UPDATE_STATE, 5, 0, 0, 8, "up17_installing", "v1.3.0-mobile.99" },
    { DEV_AUTOTEST_ACTION_CHECK_UPDATE_INSTALLS, 1, 0, 0, 1, "up18_one_install" },
    { DEV_AUTOTEST_ACTION_UPDATE_STATE, 5, 0, 0, 8, "up19_installer_cancelled", "v1.3.0-mobile.99" },
    { DEV_AUTOTEST_ACTION_CHECK_UPDATE_INSTALLS, 1, 0, 0, 1, "up20_not_reopened" },
    { DEV_AUTOTEST_ACTION_UPDATE_STATE, 8, 0, 0, 8, "up21_permission", "v1.3.0-mobile.99" },
    { DEV_AUTOTEST_ACTION_UPDATE_STATE, 7, 0, 0, 8, "up22_error", "v1.3.0-mobile.99" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 8, "up22b_retry_download", "update.download" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 8, "up22c_cancel_download", "update.cancel" },
    { DEV_AUTOTEST_ACTION_UPDATE_STATE, 7, 1, 0, 8, "up22d_check_error", "v1.3.0-mobile.99" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 8, "up22e_retry_check", "update.download" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "up22f_check_does_not_download", "update.progress" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 30, "up23_back" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "up24_settings", "mainmenu.settings" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "up25_about", "prefs.sections.5" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "up26_notice", "prefs.about.notice" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "up26b_update_dot", "prefs.about.update.notice" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "up27_no_apply", "prefs.apply" },
};

static const DevAutotestStep kDevAutotestUpdateInGameSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "ug00_game" },
    { DEV_AUTOTEST_ACTION_UPDATE_STATE, 5, 0, 0, 3, "ug01_ready", "v1.3.0-mobile.99" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 30, "ug02_menu" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "ug03_settings", "menu.settings" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "ug04_about", "prefs.sections.5" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "ug05_main_menu_hint", "prefs.about.update.ingame" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 8, "ug06_manual_check", "prefs.about.update" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "ug07_no_install", "update.install" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "ug08_no_download", "update.download" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "ug08b_no_delete", "prefs.about.update.delete" },
    { DEV_AUTOTEST_ACTION_CHECK_UPDATE_INSTALLS, 0, 0, 0, 1, "ug09_no_installer" },
};

// Own SAVEGAME with a copy of the test save in slot 1: cancel once, then delete it.
static const DevAutotestStep kDevAutotestMuiDeleteSaveSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "d00_start" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "d01_menu" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d02_load", "menu.load" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "d03_slot", "loadsave.slots.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "d04_delete", "loadsave.delete" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "d05_cancel", "dialog.secondary" },
    { DEV_AUTOTEST_ACTION_LOG_SLOT, 1, 0, 0, 1, "d06_still_saved" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "d07_delete_again", "loadsave.delete" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d08_confirm", "dialog.primary" },
    { DEV_AUTOTEST_ACTION_LOG_SLOT, 1, 0, 0, 1, "d09_deleted" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "d10_back", "loadsave.back" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d11_save", "menu.save" },
};

// Own SAVEGAME with a broken copy of the test save: loading it shows the
// game's error and goes back to the main menu, as the game's window does.
static const DevAutotestStep kDevAutotestMuiLoadFailedSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "b00_start" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "b01_menu" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "b02_load", "menu.load" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "b03_slot", "loadsave.slots.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "b04_load_game", "loadsave.action" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "b05_error", "dialog.primary" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "b06_main_menu" },
};

// Run from the title screen without --dev-new-game. The Load button opens
// the real main-menu load path and exercises its palette/preview state.
static const DevAutotestStep kDevAutotestMuiMainMenuLoadSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "t00_title" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 50, "t01_load", "mainmenu.load" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "t02_slot", "loadsave.slots.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "t03_back", "loadsave.back" },
};

// Go through the actual title-screen Load action and capture the world after
// the picker closes. This catches a palette left black by the menu fade.
static const DevAutotestStep kDevAutotestMuiMainMenuResumeSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "r00_title" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 50, "r01_picker", "mainmenu.load" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 20, "r02_newest_selected" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "r03_load", "loadsave.action" },
    { DEV_AUTOTEST_ACTION_CHECK_LOADED_WORLD, 0, 0, 0, 90, "r04_world" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_TILE, 4, ROTATION_SW, 0, 120, "r05_touch_walk" },
    { DEV_AUTOTEST_ACTION_CHECK_TOUCH_MOVED, 0, 0, 0, 1, "r06_moved" },
};

static const DevAutotestStep kDevAutotestMuiMainMenuEmptySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "e00_title" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 50, "e01_load_empty", "mainmenu.load" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "e02_back", "loadsave.back" },
};

// The game's own party member control screens (run with [touch]
// mobile_ui=0): talk to Sulik from the test save, combat control, custom.
static const DevAutotestStep kDevAutotestPcPartySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "p00_start" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 9, 0, 0, 90, "p01_loaded" },
    { DEV_AUTOTEST_ACTION_TALK_PARTY_MEMBER, 0, 0, 0, 150, "p02_talk" },
    { DEV_AUTOTEST_ACTION_TAP_DIALOG_WINDOW, 600, 123, 0, 90, "p03_party_control" },
    { DEV_AUTOTEST_ACTION_TAP_DIALOG_WINDOW, 446, 164, 0, 90, "p04_custom" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_ESCAPE, 0, 0, 60, "p05_back_to_control" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_ESCAPE, 0, 0, 60, "p06_back_to_talk" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "p07_state" },
};

// The game's own character screen (run with [touch] mobile_ui=0).
static const DevAutotestStep kDevAutotestPcCharacterSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "c00_start" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 9, 0, 0, 90, "c01_loaded" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_C, 0, 0, 60, "c02_character" },
};

// Mobile character screen: level up with an owed perk (test save).
static const DevAutotestStep kDevAutotestCharacterSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "k00_start" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 9, 0, 0, 90, "k01_loaded" },
    { DEV_AUTOTEST_ACTION_LOG_CHARACTER, 0, 0, 0, 1, "k01b_log" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Character), 0, 0, 40, "k01c_no_points" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k01d_no_points_skills", "character.tab.skills" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "k01e_closed", "character.nav.back" },
    { DEV_AUTOTEST_ACTION_ADD_EXPERIENCE, 7000, 0, 0, 60, "k02_xp" },
    { DEV_AUTOTEST_ACTION_LOG_CHARACTER, 0, 0, 0, 1, "k02b_log" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Character), 0, 0, 60, "k03_open" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k04_pick", "character.perkpanel.list.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "k05_taken", "character.perkpanel.take" },
    { DEV_AUTOTEST_ACTION_LOG_CHARACTER, 0, 0, 0, 1, "k05b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k06_skills", "character.tab.skills" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 10, "k07_plus", "character.skills.0.plus" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k07b_plus", "character.skills.0.plus" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k07c_minus", "character.skills.1.minus" },
    { DEV_AUTOTEST_ACTION_LOG_CHARACTER, 0, 0, 0, 1, "k07d_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k08_unarmed", "character.skills.3" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k09_perks", "character.tab.perks" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k09b_perk", "character.folder.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k10_karma", "character.folder.karma" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k11_kills", "character.folder.kills" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k12_stats", "character.tab.stats" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k13_carry", "character.derived.2" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k14_hp", "character.hp" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "k15_reset", "character.reset" },
    { DEV_AUTOTEST_ACTION_LOG_CHARACTER, 0, 0, 0, 1, "k15b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k16_perks_again", "character.tab.perks" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k16b_perks_folder", "character.folder.perks" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "k17_owed", "character.perks.owed" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "k18_later", "character.perkpanel.later" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "k19_inventory", "character.nav.inventory" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "k19b_to_character", "inventory.character" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "k19c_to_inventory", "character.nav.inventory" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "k20_closed" },
    { DEV_AUTOTEST_ACTION_LOG_CHARACTER, 0, 0, 0, 1, "k20b_log" },
};

// Before the vault suit (a new game) there's no Pip-Boy, as in combat: the
// HUD's button shows the game's box, the screens' tab keeps the screen (a
// notice says why).
static const DevAutotestStep kDevAutotestPipboyNotWornSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 45, "q00_map" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Pipboy), 0, 0, 20, "q01_hud_pipboy" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 10, "q02_box_closed", "dialog.primary" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Character), 0, 0, 180, "q03_character" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "q04_tab_pipboy", "character.nav.pipboy" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "q05_still_character", "character.nav.pipboy" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "q06_no_box", "dialog.primary" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 30, "q07_closed" },
};

// Game screen tabs in combat: inventory cost badge, not enough action points
// and no Pip-Boy keep the screen.
static const DevAutotestStep kDevAutotestCombatScreensSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 45, "x00_map" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 60, "x01_combat" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "x01b_state" },
    { DEV_AUTOTEST_ACTION_SET_ACTION_POINTS, 2, 0, 0, 5, "x01c_ap_2" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 5, "x01d_hud_unavailable" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Pipboy), 0, 0, 20, "x01e_hud_pipboy_blocked" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 10, "x01e_dialog_closed", "dialog.primary" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Inventory), 0, 0, 20, "x01f_hud_inventory_blocked" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Character), 0, 0, 180, "x02_character" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "x03_pipboy_blocked", "character.nav.pipboy" },
    { DEV_AUTOTEST_ACTION_SET_ACTION_POINTS, 2, 0, 0, 180, "x04_ap_2" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "x05_inventory_blocked", "character.nav.inventory" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 180, "x05b_map", "character.nav.map" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 5, "x05c_map_unavailable" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "x05d_map_inventory_blocked", "automap.inventory" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "x05e_character", "automap.character" },
    { DEV_AUTOTEST_ACTION_SET_ACTION_POINTS, 10, 0, 0, 5, "x06_ap_10" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "x07_inventory", "character.nav.inventory" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "x07b_state" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "x08_character_again", "inventory.character" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "x09_map", "character.nav.back" },
};

// The game's Pip-Boy window (PC mode): its sections.
static const DevAutotestStep kDevAutotestPcPipboySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "p00_start" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 9, 0, 0, 90, "p01_loaded" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_P, 0, 0, 60, "p02_pipboy" },
    { DEV_AUTOTEST_ACTION_EVENT, 500, 0, 0, 30, "p03_status" },
    { DEV_AUTOTEST_ACTION_EVENT, 506, 0, 0, 30, "p04_location" },
    { DEV_AUTOTEST_ACTION_EVENT, 500, 0, 0, 20, "p05_status_again" },
    { DEV_AUTOTEST_ACTION_EVENT, 501, 0, 0, 30, "p06_automaps" },
    { DEV_AUTOTEST_ACTION_EVENT, 506, 0, 0, 30, "p07_automap_location" },
    { DEV_AUTOTEST_ACTION_EVENT, 502, 0, 0, 30, "p08_archives" },
    { DEV_AUTOTEST_ACTION_EVENT, 503, 0, 0, 30, "p09_alarm" },
};

// Mobile Pip-Boy (test save with some quests and holodisks set).
static const DevAutotestStep kDevAutotestPipboySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "y00_start" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 9, 0, 0, 90, "y01_loaded" },
    { DEV_AUTOTEST_ACTION_SET_GVAR, 183, 3, 0, 1, "y01b_gvar" },
    { DEV_AUTOTEST_ACTION_SET_GVAR, 191, 1, 0, 1, "y01c_gvar" },
    { DEV_AUTOTEST_ACTION_SET_GVAR, 100, 1, 0, 1, "y01d_gvar" },
    { DEV_AUTOTEST_ACTION_SET_GVAR, 101, 2, 0, 1, "y01e_gvar" },
    { DEV_AUTOTEST_ACTION_SET_GVAR, 550, 1, 0, 1, "y01f_gvar" },
    { DEV_AUTOTEST_ACTION_SET_GVAR, 221, 1, 0, 1, "y01g_holodisk" },
    { DEV_AUTOTEST_ACTION_SET_GVAR, 222, 1, 0, 1, "y01h_holodisk" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Pipboy), 0, 0, 60, "y02_pipboy" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "y03_den", "pipboy.quests.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "y04_data", "pipboy.section.data" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "y05_data_2", "pipboy.data.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "y06_maps", "pipboy.section.maps" },
    { DEV_AUTOTEST_ACTION_PINCH, 0.15f, 0.4f, 0.0f, 20, "y06b_pinch_in" },
    { DEV_AUTOTEST_ACTION_PINCH, 0.2f, 0.2f, -0.05f, 20, "y06c_two_finger_pan" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "y07_videos", "pipboy.section.videos" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "y07b_playing", "pipboy.videos.play" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "y07c_paused", "pipboy.videos.pause" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 40, "y07d_still_paused" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "y07e_resumed", "pipboy.videos.pause" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "y07f_fullscreen", "pipboy.videos.fullscreen" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "y07g_collapsed", "pipboy.videos.fullscreen" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "y07h_stopped", "pipboy.videos.stop" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "y08_rest", "pipboy.section.rest" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "y09_rest_hour", "pipboy.rest.2" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 60, "y10_rested" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "y11_to_inventory", "pipboy.inventory" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "y12_to_pipboy", "inventory.pipboy" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "y13_closed", "pipboy.back" },
};

// Mobile map screen (automap of the current map).
static const DevAutotestStep kDevAutotestAutomapSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "a00_start" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 9, 0, 0, 90, "a01_loaded" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Automap), 0, 0, 60, "a02_map" },
    { DEV_AUTOTEST_ACTION_PINCH, 0.15f, 0.4f, 0.0f, 20, "a03_pinch_in" },
    { DEV_AUTOTEST_ACTION_PINCH, 0.2f, 0.2f, 0.08f, 20, "a04_two_finger_pan" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "a05_details", "automap.details" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "a06_sensor", "automap.sensor" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "a07_to_pipboy", "automap.pipboy" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "a08_to_map", "pipboy.map" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "a09_closed", "automap.back" },
};

// Game movies through the mobile UI player; a tap skips.
static const DevAutotestStep kDevAutotestMovieSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "v00_start" },
    { DEV_AUTOTEST_ACTION_PLAY_MOVIE, 3, 0, 0, 1, "v01_start_movie" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 60, "v02_playing" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "v03_playing_later" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "v04_skipped", "movie.screen" },
};

// Real title-menu paths, including the premade-character selector.
static const DevAutotestStep kDevAutotestTitleIntroSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "vi00_title" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "vi01_intro_tap", "mainmenu.intro" },
    { DEV_AUTOTEST_ACTION_CHECK_MOVIE, 0, 0, 0, 1, "vi02_playing" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "vi03_frame" },
};

static const DevAutotestStep kDevAutotestNewGameElderSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "ve00_title" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "ve01_new_game_tap", "mainmenu.new" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "ve02_take_character", "selector.play" },
    { DEV_AUTOTEST_ACTION_CHECK_MOVIE, 0, 0, 0, 1, "ve03_playing" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "ve04_frame" },
};

// Title screen of the mobile UI: settings and back, the character selector
// (tabs, a swipe, the own character's card, back), Continue loads the save
// made last.
static const DevAutotestStep kDevAutotestMainMenuSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 40, "mm00_title" },
    { DEV_AUTOTEST_ACTION_CHECK_MENU_PICTURES, 0, 0, 0, 1, "mm00b_picture_resources" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "mm01_continue_shown", "mainmenu.continue" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "mm02_settings", "mainmenu.settings" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "mm03_back_to_title", "prefs.back" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "mm04_selector", "mainmenu.new" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "mm05_second", "selector.tabs.1" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, -0.2f, 0, 0, 40, "mm06_swiped_to_third", "selector.picture" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "mm07_own", "selector.tabs.3" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "mm08_create_shown", "selector.create" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "mm09_creating", "selector.create" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "mm10_editor_shown", "character.done" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "mm11_cancelled", "character.nav.back" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "mm12_own_again", "selector.create" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "mm13_first", "selector.tabs.0" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "mm14_modifying", "selector.modify" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "mm15_cancelled", "character.nav.back" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "mm16_premade_again", "selector.play" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "mm17_back_to_title", "selector.back" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "mm18_continue", "mainmenu.continue" },
    { DEV_AUTOTEST_ACTION_CHECK_LOADED_WORLD, 0, 0, 0, 30, "mm19_world" },
};

// Elevator panel of the mobile UI: Back stays, then a level's button moves
// the gauge and returns that level.
static const DevAutotestStep kDevAutotestElevatorSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "el00_start" },
    { DEV_AUTOTEST_ACTION_OPEN_ELEVATOR, 0, 0, 0, 1, "el01_open" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 20, "el02_panel" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "el03_back", "elevator.back" },
    { DEV_AUTOTEST_ACTION_OPEN_ELEVATOR, 0, 0, 0, 1, "el04_open_again" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 20, "el05_panel_again" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 12, "el06_travelling", "elevator.level.2" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 200, "el07_arrived" },
};

// Scripts reading the mouse with touch-native input (touch architecture
// step 3): after a tap on the ground and a long press on a critter the
// pointer is where the finger was, the click hook ran for both (pressed and
// released), the long press's button is held while the finger is down.
static const DevAutotestStep kDevAutotestModBridgeSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "b00_start" },
    { DEV_AUTOTEST_ACTION_LOG_POINTER, 0, 0, 0, 1, "b01_before" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_TILE, 3, ROTATION_SE, 0, 60, "b02_tap_ground" },
    { DEV_AUTOTEST_ACTION_LOG_POINTER, 0, 0, 0, 1, "b03_after_tap" },
    { DEV_AUTOTEST_ACTION_TOUCH_LONG_PRESS_CRITTER, 0, 0, 0, 20, "b04_long_press_critter" },
    { DEV_AUTOTEST_ACTION_LOG_POINTER, 0, 0, 0, 1, "b05_after_long_press" },
};

// Death screen: the picture with its subtitle, a tap goes on to the main
// menu.
static const DevAutotestStep kDevAutotestDeathSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 20, "de00_start" },
    { DEV_AUTOTEST_ACTION_KILL_DUDE, 0, 0, 0, 30, "de01_death_screen" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "de02_main_menu", "scene.screen" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "de03_menu_shown", "mainmenu.new" },
};

// Ending slides: two endings' conditions set, the slides with their
// subtitles, a tap skips one; then the credits roll, a tap stops them.
static const DevAutotestStep kDevAutotestEndgameSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 20, "eg00_start" },
    { DEV_AUTOTEST_ACTION_SET_GVAR, 408, 1, 0, 1, "eg01_gvar" },
    { DEV_AUTOTEST_ACTION_SET_GVAR, 700, 1, 0, 1, "eg02_gvar" },
    { DEV_AUTOTEST_ACTION_REQUEST_ENDGAME, 0, 0, 0, 90, "eg03_slide" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 120, "eg04_subtitle" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "eg05_next_slide", "scene.screen" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 200, "eg06_after_slides", "scene.screen" },
};

// Called shot panel of the mobile UI (aimed attack at the nearest critter).
static const DevAutotestStep kDevAutotestCalledShotSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "s00_start" },
    { DEV_AUTOTEST_ACTION_EQUIP_ITEM, 9, HAND_LEFT, 0, 20, "s00b_smg" },
    { DEV_AUTOTEST_ACTION_MOVE_NEAR_CRITTER, 5, 0, 0, 20, "s00c_near_critter" },
    { DEV_AUTOTEST_ACTION_HUD_TAP_MODE, 1, 0, 0, 20, "s01_aimed_mode" },
    { DEV_AUTOTEST_ACTION_LOG_HIT_MODE, 0, 0, 0, 1, "s02_hit_mode" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 60, "s03_combat" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 20, "s03b_center_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 40, "s04_select_target" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 30, "s05_panel" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "s06_cancelled", "calledshot.cancel" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 30, "s07_panel_again" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "s08_head", "calledshot.0" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "s09_after_attack" },
};

// Combat speed: dude walks, runs and punches in combat (4 hexes there and
// back, the time logged when each ends) with the game's rule, with
// `combat_speed_all_animations`, and so on a slow device (animations catch
// up on frames the game's loop was late for).
#define COMBAT_SPEED_ROUND(prefix, speed, all)                                                     \
    { DEV_AUTOTEST_ACTION_SET_COMBAT_SPEED, speed, 1, all, 1, prefix "_settings" },                \
        { DEV_AUTOTEST_ACTION_SET_ACTION_POINTS, 99, 0, 0, 1, prefix "_ap" },                      \
        { DEV_AUTOTEST_ACTION_TIME_DUDE_ANIMATION, 0, 4, ROTATION_SW, 200, prefix "_walk" },       \
        { DEV_AUTOTEST_ACTION_TIME_DUDE_ANIMATION, 1, 4, ROTATION_NE, 200, prefix "_run" },        \
        { DEV_AUTOTEST_ACTION_TIME_DUDE_ANIMATION, 2, ANIM_THROW_PUNCH, 0, 120, prefix "_punch" }, \
    {                                                                                              \
        DEV_AUTOTEST_ACTION_LOG_ANIMATION_SOUND, 0, 0, 0, 1, prefix "_burst_sound", "WAH2XXX2"     \
    }

static const DevAutotestStep kDevAutotestCombatSpeedSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "v00_start" },
    { DEV_AUTOTEST_ACTION_MOVE_NEAR_CRITTER, 8, 0, 0, 20, "v01_near_critter" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 60, "v02_combat" },
    COMBAT_SPEED_ROUND("v10_slowest", 0, 0),
    COMBAT_SPEED_ROUND("v20_game", 50, 0),
    COMBAT_SPEED_ROUND("v30_all", 50, 1),
    { DEV_AUTOTEST_ACTION_SLOW_FRAMES, 50, 0, 0, 1, "v40_slow_device" },
    COMBAT_SPEED_ROUND("v41_all_slow", 50, 1),
    { DEV_AUTOTEST_ACTION_SLOW_FRAMES, 0, 0, 0, 1, "v50_normal_device" },
    { DEV_AUTOTEST_ACTION_SET_COMBAT_SPEED, -1, 0, 0, 1, "v51_settings_back" },
};

#undef COMBAT_SPEED_ROUND

// Camera over a loaded save (`--dev-load-game=<slot>`: the test save in
// Arroyo on the Mac, the player's on a device): dragging at 1x and zoomed out
// to the map's edges, pinching, walking, stepping under a roof and out; each
// part's frame time, map pixels drawn and uploaded to the GPU logged
// (PERF_LOG after each). A benchmark: compare before and after changes.
static const DevAutotestStep kDevAutotestPerfMapSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 60, "m00_start" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 60, "m02_start" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m03_idle" },
    { DEV_AUTOTEST_ACTION_PAN_FRAMES, 12, 0, 60, 62, "m04_drag_right" },
    { DEV_AUTOTEST_ACTION_PAN_FRAMES, 0, 9, 60, 62, "m05_drag_down" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m06_drag_1x" },
    { DEV_AUTOTEST_ACTION_PINCH, 0.4f, 0.2f, 0.0f, 20, "m07_pinch_in" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m08_pinch" },
    { DEV_AUTOTEST_ACTION_ZOOM, 0.5f, -1, -1, 20, "m09_zoom_out" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m10_settle" },
    { DEV_AUTOTEST_ACTION_PAN_FRAMES, -16, -12, 90, 92, "m11_drag_to_edge" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m12_drag_0.5x_edge" },
    { DEV_AUTOTEST_ACTION_PAN_FRAMES, 16, 12, 90, 92, "m13_drag_back" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m14_drag_0.5x" },
    { DEV_AUTOTEST_ACTION_ZOOM, 1.0f, -1, -1, 20, "m15_zoom_in" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m16_settle" },
    { DEV_AUTOTEST_ACTION_WALK, 8, 0, 0, 150, "m17_walk" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m18_walk" },
    { DEV_AUTOTEST_ACTION_STEP_UNDER_ROOF, 0, 0, 0, 20, "m19_under_roof" },
    { DEV_AUTOTEST_ACTION_STEP_UNDER_ROOF, 1, 0, 0, 20, "m20_out_of_roof" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m21_roofs" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 60, "m19_idle_again" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m20_idle" },
};

// A finger down on the map, the game menu opens under it (Esc: Android's
// back gesture), the finger is lifted over the menu: the map must forget it,
// or every later touch is its second finger (touch looked dead or jumped to
// where it was). After closing the menu a tap on the map walks.
static const DevAutotestStep kDevAutotestStuckFingerSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "f00_start" },
    { DEV_AUTOTEST_ACTION_FINGER, 0, 0.35f, 0.3f, 5, "f01_finger_down" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 30, "f02_menu" },
    { DEV_AUTOTEST_ACTION_FINGER, 1, 0.35f, 0.3f, 10, "f03_finger_up" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "f04_continue", "menu.continue" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_TILE, 4, ROTATION_SW, 0, 120, "f05_tap_walk" },
    { DEV_AUTOTEST_ACTION_CHECK_TOUCH_MOVED, 0, 0, 0, 1, "f06_moved" },
};

// A tap on the HUD's menu button read after the game stalled 600 ms is too
// old: dropped, the menu doesn't open. A tap right after works.
static const DevAutotestStep kDevAutotestStaleTouchSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "g00_start" },
    { DEV_AUTOTEST_ACTION_MUI_TAP_THEN_STALL, 600, 0, 0, 20, "g01_tap_during_stall", "hud.0" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "g02_menu_not_opened", "menu.continue" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "g03_tap", "hud.0" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "g04_menu_opened", "menu.continue" },
};

// Armor off in the inventory: the dude on the map looks without it once the
// inventory closes (not only after opening it again).
static const DevAutotestStep kDevAutotestArmorLookSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "a00_start" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 74, 0, 0, 10, "a01_jacket" },
    { DEV_AUTOTEST_ACTION_ZOOM_AT_DUDE, 1.6f, 0, 0, 20, "a01b_zoom" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "a02_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "a03_wear", "inventory.pid.74", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "a04_closed_wearing" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "a05_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "a06_take_off", "inventory.slot.armor", "inventory.list" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "a07_closed_without" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "a08_open_again" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "a09_closed_again" },
    // With a weapon in hand, the inventory left through the map tab.
    { DEV_AUTOTEST_ACTION_EQUIP_ITEM, 9, HAND_LEFT, 0, 20, "a10_smg" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "a11_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "a12_wear", "inventory.pid.74", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "a13_closed_wearing" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "a14_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "a15_take_off", "inventory.slot.armor", "inventory.list" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 150, "a16_map_tab", "inventory.map" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "a17_back_on_map" },
    // Worn again, then dropped on the ground from its slot.
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "a18_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "a19_wear", "inventory.pid.74", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "a20_closed_wearing" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "a21_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 60, "a22_drop", "inventory.slot.armor", "inventory.info" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "a23_closed_dropped" },
};

// Armor changed while the dude walks: the inventory can't stop a walk (it is
// a reserved animation), the walk goes on after it and must not put the old
// look back (2026-10-01: the dude kept the armor's look after taking it off).
static const DevAutotestStep kDevAutotestArmorWalkSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "w00_start" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 74, 0, 0, 10, "w01_jacket" },
    { DEV_AUTOTEST_ACTION_ZOOM_AT_DUDE, 1.6f, 0, 0, 20, "w02_zoom" },
    { DEV_AUTOTEST_ACTION_WALK, 8, 0, 0, 4, "w03_walk" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "w04_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "w05_wear", "inventory.pid.74", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 200, "w06_walked_wearing" },
    { DEV_AUTOTEST_ACTION_WALK, 8, 0, 0, 4, "w07_walk" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "w08_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "w09_take_off", "inventory.slot.armor", "inventory.list" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 200, "w10_walked_without" },
};

// Armor changes between quick saves and quick loads, some while the dude
// walks: the look on the map stays what he wears ("look ... ok" lines).
static const DevAutotestStep kDevAutotestArmorSaveLoadSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "l00_start" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 74, 0, 0, 5, "l01_jacket" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 1, 0, 0, 5, "l02_leather" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "l03_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "l04_wear_jacket", "inventory.pid.74", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "l05_closed" },
    { DEV_AUTOTEST_ACTION_QUICK_SAVE, 0, 0, 0, 30, "l06_saved_jacket" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "l07_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "l08_take_off", "inventory.slot.armor", "inventory.list" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "l09_closed_without" },
    { DEV_AUTOTEST_ACTION_QUICK_LOAD, 0, 0, 0, 60, "l10_loaded_jacket" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "l11_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "l12_wear_leather", "inventory.pid.1", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "l13_closed_leather" },
    { DEV_AUTOTEST_ACTION_QUICK_LOAD, 0, 0, 0, 60, "l14_loaded_jacket" },
    { DEV_AUTOTEST_ACTION_WALK, 8, 0, 0, 4, "l15_walk" },
    { DEV_AUTOTEST_ACTION_QUICK_SAVE, 0, 0, 0, 120, "l16_saved_walking" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "l17_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "l18_take_off", "inventory.slot.armor", "inventory.list" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "l19_closed_without" },
    { DEV_AUTOTEST_ACTION_QUICK_LOAD, 0, 0, 0, 60, "l20_loaded_walking_save" },
    { DEV_AUTOTEST_ACTION_WALK, 6, 0, 0, 4, "l21_walk" },
    { DEV_AUTOTEST_ACTION_QUICK_LOAD, 0, 0, 0, 120, "l22_loaded_while_walking" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(QuickSave), 0, 0, 60, "l23_hud_save" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "l24_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "l25_wear_leather", "inventory.pid.1", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "l26_closed_leather" },
    { DEV_AUTOTEST_ACTION_QUICK_LOAD, 0, 0, 0, 60, "l27_loaded" },
};

// Party members' items on the barter table go back to them when the barter
// is called off (`barterReturnPlayerTable`): Sulik's jacket that he can't
// carry anymore (the dude gave him his own meanwhile) goes to the dude, his
// rock goes back to him; a party member far away can't be switched to.
static const DevAutotestStep kDevAutotestBarterPartySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "p00_map" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 97, 27, ROTATION_NW, 10, "p01_sulik", "Kcsulik.int" },
    { DEV_AUTOTEST_ACTION_LOG_CRITTERS, 0, 0, 0, 1, "p01b_critters" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 74, 0, 0, 1, "p02_sulik_jacket" },
    { DEV_AUTOTEST_ACTION_FILL_PARTY_WEIGHT, 19, 2, 0, 1, "p03_sulik_rocks" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 74, 0, 0, 1, "p04_dude_jacket" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "p05_center_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 300, "p06_talk" },
    { DEV_AUTOTEST_ACTION_MOVE_PARTY_MEMBER, 2, 0, 0, 10, "p06b_sulik_near" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "p07_barter", "dialog.tab.barter" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p08_sulik_tab", "barter.party.1" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 40, "p09_offer_jacket", "barter.player.pid.74", "barter.offer" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p10_dude_tab", "barter.party.0" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 40, "p11_jacket_to_sulik", "barter.player.pid.74", "barter.party.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "p12_called_off", "dialog.tab.talk" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "p13_barter", "dialog.tab.barter" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p14_sulik_tab", "barter.party.1" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 40, "p15_offer_rock", "barter.player.pid.19", "barter.offer" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "p16_quantity_done", "quantity.done" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "p17_called_off", "dialog.tab.talk" },
    { DEV_AUTOTEST_ACTION_MOVE_PARTY_MEMBER, 40, 0, 0, 10, "p18_sulik_away" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "p19_barter", "dialog.tab.barter" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "p20_far_tab", "barter.party.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "p21_talk", "dialog.tab.talk" },
};

// The game's floating texts over the map at a fixed size (mui_floating_text.cc):
// over the dude's head at zoom 1, 0.5 and 3, kept on screen near an edge.
static const DevAutotestStep kDevAutotestFloatTextSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "f00_start" },
    { DEV_AUTOTEST_ACTION_FLOAT_TEXT, 1, 0, 0, 20, "f01_text", "\xcf\xf0\xe8\xe2\xe5\xf2, \xed\xe5\xe7\xed\xe0\xea\xee\xec\xe5\xf6! Long words wrap into lines over the speaker's head." },
    { DEV_AUTOTEST_ACTION_ZOOM_AT_DUDE, 0.5f, 0, 0, 20, "f02_zoom50" },
    { DEV_AUTOTEST_ACTION_ZOOM_AT_DUDE, 3.0f, 0, 0, 20, "f03_zoom300" },
    { DEV_AUTOTEST_ACTION_ZOOM_AT_DUDE, 1.0f, 0, 0, 20, "f04_zoom100" },
    { DEV_AUTOTEST_ACTION_PAN, 0, -260, 0, 20, "f05_dude_near_top" },
};

// A party member in combat below its run away hit points with stimpaks
// heals and fights on (sfall's fix, combat_ai.cc); it used to flee for the
// rest of combat with them unused (Vic, 2026-10-13).
static const DevAutotestStep kDevAutotestFleeHealSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "f00_start" },
    { DEV_AUTOTEST_ACTION_MOVE_NEAR_CRITTER, 8, 0, 0, 20, "f01_near_critter" },
    { DEV_AUTOTEST_ACTION_LOG_CRITTERS, 0, 0, 0, 1, "f02_critters" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 97, 2, ROTATION_SE, 10, "f03_sulik", "Kcsulik.int" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 40, 5, 0, 1, "f04_stimpaks" },
    { DEV_AUTOTEST_ACTION_SET_PARTY_HP, 3, 0, 0, 1, "f05_hurt" },
    { DEV_AUTOTEST_ACTION_LOG_PARTY, 0, 0, 0, 1, "f06_party" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 30, "f07_combat" },
    { DEV_AUTOTEST_ACTION_KEEP_COMBAT, 0, 0, 0, 90, "f08_keep_combat" },
    { DEV_AUTOTEST_ACTION_END_TURN_WAIT, 1800, 0, 0, 10, "f09_next_turn" },
    { DEV_AUTOTEST_ACTION_LOG_PARTY, 0, 0, 0, 1, "f10_party" },
    { DEV_AUTOTEST_ACTION_CHECK_PARTY_HEALED, 0, 0, 0, 1, "f11_healed" },
};

// A hurt party member asked to heal himself in a talk (party.h obj_heal:
// use_obj_on_obj of a stimpak on himself) uses the stimpak at once, as with
// sfall: his answer is by his health after it. It used to wait for the end
// of the talk, one stimpak however many times asked (Vic, 2026-10-13). Vic
// of the test save.
static const DevAutotestStep kDevAutotestTalkHealSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "h00_start" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 9, 0, 0, 90, "h00b_loaded" },
    { DEV_AUTOTEST_ACTION_SET_PARTY_HP, 19, 62, 0, 1, "h01_vic_hurt" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 40, 5, 0, 1, "h02_stimpaks" },
    { DEV_AUTOTEST_ACTION_SET_PARTY_HP, 19, 0, 0, 1, "h03_vic_state" },
    { DEV_AUTOTEST_ACTION_TALK_PARTY_MEMBER, 62, 0, 0, 150, "h04_talk" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h05_options" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 1, 0, 0, 240, "h06_heal" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h07_reply" },
    { DEV_AUTOTEST_ACTION_LOG_PARTY, 0, 0, 0, 1, "h08_party" },
    { DEV_AUTOTEST_ACTION_CHECK_PARTY_HEALED, 0, 0, 0, 1, "h09_healed" },
};

// The critical hit table has sfall's fixes (combat.cc criticalsInit, sfall's
// OverrideCriticalTable=2): right arm messages of men, women, geckos, aliens
// and giant ants, the super mutants' fix on the right arm and the left leg's
// knockdown message kept (sfall 2023-2024).
#define CHECK_CRITICAL(killType, location, effect, member, value, name)                                                                   \
    {                                                                                                                                     \
        DEV_AUTOTEST_ACTION_CHECK_CRITICAL, static_cast<float>((killType) * 100 + (location) * 10 + (effect)), (member), (value), 1, name \
    }

static const DevAutotestStep kDevAutotestCritTableSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 10, "c00_start" },
    CHECK_CRITICAL(KILL_TYPE_MAN, HIT_LOCATION_RIGHT_ARM, 2, CRIT_DATA_MEMBER_MESSAGE_ID, 5010, "c01_man_arm"),
    CHECK_CRITICAL(KILL_TYPE_MAN, HIT_LOCATION_RIGHT_ARM, 2, CRIT_DATA_MEMBER_MASSIVE_CRITICAL_MESSAGE_ID, 5014, "c02_man_arm_massive"),
    CHECK_CRITICAL(KILL_TYPE_WOMAN, HIT_LOCATION_RIGHT_ARM, 2, CRIT_DATA_MEMBER_MESSAGE_ID, 5110, "c03_woman_arm"),
    CHECK_CRITICAL(KILL_TYPE_WOMAN, HIT_LOCATION_RIGHT_ARM, 3, CRIT_DATA_MEMBER_MASSIVE_CRITICAL_MESSAGE_ID, 5114, "c04_woman_arm_massive"),
    CHECK_CRITICAL(KILL_TYPE_SUPER_MUTANT, HIT_LOCATION_RIGHT_ARM, 1, CRIT_DATA_MEMBER_MASSIVE_CRITICAL_MESSAGE_ID, 5306, "c05_mutant_arm"),
    CHECK_CRITICAL(KILL_TYPE_SUPER_MUTANT, HIT_LOCATION_LEFT_LEG, 1, CRIT_DATA_MEMBER_MASSIVE_CRITICAL_MESSAGE_ID, 5312, "c06_mutant_leg"),
    CHECK_CRITICAL(KILL_TYPE_GECKO, HIT_LOCATION_RIGHT_ARM, 2, CRIT_DATA_MEMBER_MASSIVE_CRITICAL_MESSAGE_ID, 5014, "c07_gecko_arm"),
    CHECK_CRITICAL(KILL_TYPE_ALIEN, HIT_LOCATION_RIGHT_ARM, 2, CRIT_DATA_MEMBER_MASSIVE_CRITICAL_MESSAGE_ID, 5014, "c08_alien_arm"),
    CHECK_CRITICAL(KILL_TYPE_GIANT_ANT, HIT_LOCATION_RIGHT_ARM, 2, CRIT_DATA_MEMBER_MASSIVE_CRITICAL_MESSAGE_ID, 5014, "c09_ant_arm"),
    // The 2020 fixes are there too.
    CHECK_CRITICAL(KILL_TYPE_MAN, HIT_LOCATION_UNCALLED, 2, CRIT_DATA_MEMBER_MESSAGE_ID, 5019, "c10_man_uncalled"),
};

#undef CHECK_CRITICAL

// A critter gets its action points when its turn comes (sfall's fix,
// combat.cc _combat_set_move): a critter attacking the dude with bonus action
// points (combat start data) has them in its own turn, the dude has his
// maximum; before, everyone got the bonus at the start of the round.
static const DevAutotestStep kDevAutotestApTurnSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "a00_start" },
    { DEV_AUTOTEST_ACTION_MOVE_NEAR_CRITTER, 8, 0, 0, 20, "a01_near_critter" },
    { DEV_AUTOTEST_ACTION_START_COMBAT_ATTACKED, 5, 0, 0, 30, "a02_attacked" },
    { DEV_AUTOTEST_ACTION_CHECK_DUDE_AP, 0, 0, 0, 1, "a03_dude_ap" },
};

// Bonus Move points aren't replenished by loading in combat (sfall's fix,
// combat.cc _combat_turn): the dude with the perk spends one of his two free
// move points, quick saves, loads: one left.
static const DevAutotestStep kDevAutotestBonusMoveLoadSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "b00_start" },
    { DEV_AUTOTEST_ACTION_ADD_DUDE_PERK, PERK_BONUS_MOVE, 0, 0, 1, "b01_perk" },
    { DEV_AUTOTEST_ACTION_MOVE_NEAR_CRITTER, 8, 0, 0, 20, "b02_near_critter" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 30, "b03_combat" },
    { DEV_AUTOTEST_ACTION_KEEP_COMBAT, 0, 0, 0, 30, "b04_keep_combat" },
    { DEV_AUTOTEST_ACTION_CHECK_FREE_MOVE, 2, 0, 0, 1, "b05_two" },
    { DEV_AUTOTEST_ACTION_SET_FREE_MOVE, 1, 0, 0, 1, "b06_spent_one" },
    { DEV_AUTOTEST_ACTION_QUICK_SAVE, 0, 0, 0, 20, "b07_saved" },
    { DEV_AUTOTEST_ACTION_SET_FREE_MOVE, 0, 0, 0, 1, "b08_spent_all" },
    { DEV_AUTOTEST_ACTION_QUICK_LOAD, 0, 0, 0, 90, "b09_loaded" },
    { DEV_AUTOTEST_ACTION_CHECK_FREE_MOVE, 1, 0, 0, 1, "b10_one_left" },
};

// Bonus Move in walks (sfall's fixes, actions.cc, animation.cc): with no
// action points and two free move points the dude walks to a critter 3
// hexes away to use a stimpak on it (the walk counted only the action points:
// he couldn't go); a walk to a free tile with a distance of 2 out of 10 move
// points stops after 2 hexes (the distance was ignored for the dude).
static const DevAutotestStep kDevAutotestBonusMoveWalkSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "w00_start" },
    { DEV_AUTOTEST_ACTION_ADD_DUDE_PERK, PERK_BONUS_MOVE, 0, 0, 1, "w01_perk" },
    { DEV_AUTOTEST_ACTION_MOVE_NEAR_CRITTER, 3, 0, 0, 20, "w02_near_critter" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 30, "w03_combat" },
    { DEV_AUTOTEST_ACTION_KEEP_COMBAT, 0, 0, 0, 30, "w04_keep_combat" },
    { DEV_AUTOTEST_ACTION_SET_ACTION_POINTS, 0, 0, 0, 1, "w05_no_ap" },
    { DEV_AUTOTEST_ACTION_SET_FREE_MOVE, 2, 0, 0, 1, "w06_free_move" },
    { DEV_AUTOTEST_ACTION_USE_ITEM_ON_CRITTER, 40, 0, 0, 150, "w07_use_stimpak" },
    { DEV_AUTOTEST_ACTION_CHECK_NEAR_CRITTER, 1, 0, 0, 1, "w08_reached" },
    { DEV_AUTOTEST_ACTION_SET_ACTION_POINTS, 10, 0, 0, 1, "w09_ap" },
    { DEV_AUTOTEST_ACTION_SET_FREE_MOVE, 0, 0, 0, 1, "w10_no_free_move" },
    { DEV_AUTOTEST_ACTION_WALK_LIMITED, 5, 2, 0, 150, "w11_walk" },
    { DEV_AUTOTEST_ACTION_CHECK_WALKED, 2, 0, 0, 1, "w12_walked" },
};

// Knockouts (sfall's "NPC turns into a container" / knockout fixes,
// critter.cc, animation.cc, combat.cc): Klint stands up standing without an
// animation; woken in combat outside his turn he's marked prone; knocked down
// losing his turn he stands up when combat ends; knocked out when the map is
// left he stays knocked down, his wake up event removed.
static const DevAutotestStep kDevAutotestKnockoutsSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "k00_start" },
    { DEV_AUTOTEST_ACTION_MOVE_NEAR_CRITTER, 4, 0, 0, 20, "k01_near_critter" },
    { DEV_AUTOTEST_ACTION_KNOCKOUT, 1, 0, 0, 30, "k02_standup_standing" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 30, "k03_combat" },
    { DEV_AUTOTEST_ACTION_KEEP_COMBAT, 0, 0, 0, 10, "k04_keep_combat" },
    { DEV_AUTOTEST_ACTION_KNOCKOUT, 2, 0, 0, 10, "k05_woken" },
    { DEV_AUTOTEST_ACTION_KNOCKOUT, 3, 0, 0, 10, "k06_knocked_down" },
    { DEV_AUTOTEST_ACTION_KNOCKOUT, 4, 0, 0, 10, "k07_nobody_hits" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::EndTurn), 0, 0, 240, "k08_combat_over" },
    { DEV_AUTOTEST_ACTION_KNOCKOUT, 5, 0, 0, 10, "k09_stood_up" },
    { DEV_AUTOTEST_ACTION_KNOCKOUT, 6, 0, 0, 10, "k10_map_left" },
};

// A party member's corpse (sfall's fix in partyFixMultipleMembers, party_member.cc):
// Sulik joins and dies; after a quick save and load his corpse is still on
// the map.
static const DevAutotestStep kDevAutotestPartyCorpseSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "pc00_start" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 97, 3, ROTATION_SW, 30, "pc01_sulik_joins", "Kcsulik.int" },
    { DEV_AUTOTEST_ACTION_PARTY_CORPSE, 1, 0, 0, 30, "pc02_killed" },
    { DEV_AUTOTEST_ACTION_QUICK_SAVE, 0, 0, 0, 30, "pc03_saved" },
    { DEV_AUTOTEST_ACTION_QUICK_LOAD, 0, 0, 0, 60, "pc04_loaded" },
    { DEV_AUTOTEST_ACTION_PARTY_CORPSE, 2, 0, 0, 10, "pc05_corpse" },
};

// Critters heal while the dude is away from a map with dead_bodies_age=No in
// maps.txt (sfall's fix in _map_age_dead_critters, map.cc): a critter of
// Broken Hills' second map at 1 hit point heals in a day away.
static const DevAutotestStep kDevAutotestMapAgeHealSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "a00_start" },
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 0, -1, 0, 120, "a01_broken_hills_2", "broken2.map" },
    { DEV_AUTOTEST_ACTION_MAP_AGE, 1, 0, 0, 10, "a02_wounded" },
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 0, -1, 0, 120, "a03_away", "broken1.map" },
    { DEV_AUTOTEST_ACTION_MAP_AGE, 2, 24, 0, 10, "a04_a_day" },
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 0, -1, 0, 120, "a05_back", "broken2.map" },
    { DEV_AUTOTEST_ACTION_MAP_AGE, 3, 0, 0, 10, "a06_healed" },
};

// Where the original differs from fallout2-ce#675: a new critter proto's
// team, the first direction tried to place a party member, the days after
// which bodies become blood stains (6) and blood stains are removed (14) -
// Broken Hills (dead_bodies_age=Yes), a body, 7 and then 15 days away.
static const DevAutotestStep kDevAutotestOriginal675Steps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "o00_start" },
    { DEV_AUTOTEST_ACTION_ORIGINAL_675, 1, 0, 0, 1, "o01_proto_team" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 97, 3, ROTATION_SW, 30, "o02_sulik_joins", "Kcsulik.int" },
    { DEV_AUTOTEST_ACTION_ORIGINAL_675, 2, 0, 0, 10, "o03_placed" },
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 0, -1, 0, 120, "o04_broken_hills", "broken1.map" },
    { DEV_AUTOTEST_ACTION_ORIGINAL_675, 3, 0, 0, 10, "o05_body" },
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 0, -1, 0, 120, "o06_away", "broken2.map" },
    { DEV_AUTOTEST_ACTION_MAP_AGE, 2, 7 * 24, 0, 10, "o07_a_week" },
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 0, -1, 0, 120, "o08_back", "broken1.map" },
    { DEV_AUTOTEST_ACTION_ORIGINAL_675, 4, 0, 0, 10, "o09_blood" },
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 0, -1, 0, 120, "o10_away_again", "broken2.map" },
    { DEV_AUTOTEST_ACTION_MAP_AGE, 2, 15 * 24, 0, 10, "o11_fifteen_days" },
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 0, -1, 0, 120, "o12_back_again", "broken1.map" },
    { DEV_AUTOTEST_ACTION_ORIGINAL_675, 5, 0, 0, 10, "o13_no_blood" },
};

// Barter (sfall's fixes in barterAttemptTransaction, inventory.cc): a used
// Geiger counter (turned on and off) and an active Stealth Boy are sold (the
// Stealth Boy turned off), then a knife while the dude is overloaded.
static const DevAutotestStep kDevAutotestBarterFixesSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "f00_map" },
    { DEV_AUTOTEST_ACTION_BARTER_SALE, 1, 52, 2, 1, "f01_used_geiger" },
    { DEV_AUTOTEST_ACTION_BARTER_SALE, 1, 54, 1, 1, "f02_active_stealth_boy" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 4, 0, 0, 1, "f03_give_knife" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "f04_center_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 300, "f05_talk" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "f06_barter", "dialog.tab.barter" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 40, "f07_offer_geiger", "barter.player.pid.52", "barter.offer" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 40, "f08_offer_stealth_boy", "barter.player.pid.210", "barter.offer" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "f09_offer", "barter.offer.button" },
    { DEV_AUTOTEST_ACTION_BARTER_SALE, 3, 52, 54, 1, "f10_sold" },
    { DEV_AUTOTEST_ACTION_BARTER_SALE, 2, 12, 0, 1, "f11_overloaded" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 40, "f12_offer_knife", "barter.player.pid.4", "barter.offer" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "f13_offer", "barter.offer.button" },
    { DEV_AUTOTEST_ACTION_BARTER_SALE, 3, 4, 0, 1, "f14_sold_overloaded" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "f15_back_to_talk", "dialog.tab.talk" },
};

// Drug doses after save/load (sfall's fix, item.cc, sfall_ext.cc): two
// doses of Buffout (its limit), a quick save and load; the loaded effects
// still count, so a third dose doesn't raise Strength.
static const DevAutotestStep kDevAutotestDrugSaveSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "d00_start" },
    { DEV_AUTOTEST_ACTION_DRUG_DOSES, 1, 2, 87, 10, "d01_two_doses" },
    { DEV_AUTOTEST_ACTION_QUICK_SAVE, 0, 0, 0, 30, "d02_saved" },
    { DEV_AUTOTEST_ACTION_QUICK_LOAD, 0, 0, 0, 60, "d03_loaded" },
    { DEV_AUTOTEST_ACTION_DRUG_DOSES, 2, 4, 87, 10, "d04_third_dose" },
};

// "Unlimited Ammo" (sfall's fix in inventoryEquipFunc): a stack of two
// spears wielded puts one in the hand, the other stays in the inventory (the
// starting dude has no pistol animations to wield a gun, and without RPU no
// knife ones either).
static const DevAutotestStep kDevAutotestWieldStackSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "u00_start" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 7, 2, 0, 1, "u01_two_spears" },
    { DEV_AUTOTEST_ACTION_WIELD_STACK, 7, 2, 0, 30, "u02_wielded" },
};

// Burst friendly fire (sfall's fix in combat.cc): an attacker with a 10mm SMG
// sees its team mate in the line of fire and doesn't burst.
static const DevAutotestStep kDevAutotestBurstFriendlySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "y00_start" },
    { DEV_AUTOTEST_ACTION_BURST_FRIENDLY, 9, 4, 0, 30, "y01_burst" },
};

// Moving away before an explosion (sfall's fix in combat_ai.cc, combat.cc): a
// hostile critter with frag grenades 2 hexes from the dude (its distance
// preference "on your own") attacks: it steps out of the blast first; without
// the fix it throws from where it stands.
static const DevAutotestStep kDevAutotestMoveAwaySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "m00_start" },
    { DEV_AUTOTEST_ACTION_MOVE_AWAY, 1, 2, 25, 10, "m01_grenadier" },
    { DEV_AUTOTEST_ACTION_START_COMBAT_ATTACKED, 0, 0, 0, 60, "m02_attacked" },
    { DEV_AUTOTEST_ACTION_MOVE_AWAY, 2, 2, 25, 10, "m03_moved_away" },
};

// The AI picking up a weapon (sfall's fix in combat_ai.cc): an unarmed
// critter with a Stimpak of the same id as a knife on the ground next to it
// attacks the dude (a spear in hand) and wields the knife. Without the fix the
// pickup returns the Stimpak, which it wields (RPU) or not (the unmodded
// game). Not a party member's pid: the NPC armor mod (RPU) swaps a party
// member's unanimated item in hand for another weapon, hiding the bug.
static const DevAutotestStep kDevAutotestRetrieveObjectSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "j00_start" },
    { DEV_AUTOTEST_ACTION_RETRIEVE_OBJECT, 1, 3, 4, 10, "j01_unarmed" },
    { DEV_AUTOTEST_ACTION_START_COMBAT_ATTACKED, 0, 0, 0, 60, "j02_attacked" },
    { DEV_AUTOTEST_ACTION_RETRIEVE_OBJECT, 2, 3, 4, 10, "j03_wielded" },
};

// An NPC over its weight picking up an item (sfall's fix in
// proto_instance.cc): out of combat its failed pickup shows the NPC's
// message, not the player's; in combat an unarmed critter loaded with armor
// attacks the dude (a spear in hand) and picks up the knife next to it.
// Without the fix the knife stays on the ground (the pickup fails until its
// action points run out).
static const DevAutotestStep kDevAutotestNpcPickupSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "k00_start" },
    { DEV_AUTOTEST_ACTION_NPC_PICKUP, 1, 3, 4, 10, "k01_overloaded" },
    { DEV_AUTOTEST_ACTION_START_COMBAT_ATTACKED, 0, 0, 0, 60, "k02_attacked" },
    { DEV_AUTOTEST_ACTION_NPC_PICKUP, 2, 3, 4, 10, "k03_picked_up" },
};

// A multihex critter closing in on the dude in combat (sfall's fix in
// combat_ai.cc): it stops next to him, not over him. Without the fix it
// runs to the dude's own tile and ends one tile from him, its body over
// his.
static const DevAutotestStep kDevAutotestMultihexMoveSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "m00_start" },
    { DEV_AUTOTEST_ACTION_MULTIHEX_MOVE, 1, 5, 0, 10, "m01_multihex" },
    { DEV_AUTOTEST_ACTION_MULTIHEX_MOVE, 2, 5, 0, 60, "m02_attacked" },
    { DEV_AUTOTEST_ACTION_MULTIHEX_MOVE, 3, 5, 0, 10, "m03_closed_in" },
};

// The same with a team mate behind it shooting at the dude through it: the
// AI retargets its tile to a side one (_cai_retargetTileFromFriendlyFire),
// next to the dude. sfall's fix checks the tiles around a multihex critter
// there; without it the critter goes to that tile, its body over the dude.
//
// The AI's info (its last target) starts afresh with the combat: the
// gunman's target is set in the dude's first turn, the critter far enough
// not to reach the dude before its second (it starts the combat, then the
// dude is first in each round).
static const DevAutotestStep kDevAutotestMultihexRetargetSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "n00_start" },
    { DEV_AUTOTEST_ACTION_MULTIHEX_MOVE, 1, 11, 1, 10, "n01_multihex" },
    { DEV_AUTOTEST_ACTION_MULTIHEX_MOVE, 2, 11, 1, 60, "n02_attacked" },
    { DEV_AUTOTEST_ACTION_MULTIHEX_MOVE, 4, 11, 1, 1, "n03_gunman_target" },
    { DEV_AUTOTEST_ACTION_END_TURN_WAIT, 1800, 0, 0, 10, "n04_next_turn" },
    { DEV_AUTOTEST_ACTION_END_TURN_WAIT, 1800, 0, 0, 10, "n05_its_second_turn" },
    { DEV_AUTOTEST_ACTION_MULTIHEX_MOVE, 3, 11, 1, 10, "n06_closed_in" },
};

// A multihex party member (a Lesser Scorpion) placed as on entering a map
// or an elevation (partyMemberSyncPosition, sfall's fix in
// party_member.cc): the tiles around it are free, not the dude's. Without
// the fix it is placed beside the dude like the others, its body over his.
// Checked right after the placement: the scorpion attacks the dude anyway
// (its AI), which would move it. The villagers are removed first, and the
// scorpion comes from another elevation.
static const DevAutotestStep kDevAutotestMultihexPartySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "p00_start" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 6, 4, ROTATION_SE, 1, "p01_scorpion" },
    { DEV_AUTOTEST_ACTION_CHECK_MULTIHEX_PARTY, 0, 0, 0, 10, "p02_placed" },
};

// Explosives going off as the dude leaves the map kill a critter with a
// script (sfall's fix in actions.cc): its destroy_p_proc runs. An Arroyo
// warrior's (ACWarior.int, the same with RPU and without) sets global var 7
// whoever the killer (sfall passes none; Klint's, for one, does nothing
// then); without the fix the critter dies with its script removed first and
// the var stays 0. Lit dynamite on its tile, 1 HP, then to an empty desert
// map (desert1 is map 0, the world map) and back: the critter lies dead.
static const DevAutotestStep kDevAutotestExplodeOnLeaveSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "e00_start" },
    { DEV_AUTOTEST_ACTION_EXPLODE_ON_LEAVE, 1, 0, 0, 10, "e01_dynamite" },
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 0, -1, 0, 120, "e02_desert", "desert2.map" },
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 0, -1, 0, 120, "e03_back", "artemple.map" },
    { DEV_AUTOTEST_ACTION_EXPLODE_ON_LEAVE, 2, 0, 0, 10, "e04_destroy_p_proc" },
};

// A party member's equipped items count in its weight on the barter and
// steal screens (sfall's fix in item.cc/inventory.cc): Sulik in metal armor
// with a sledgehammer, loaded up to what he can carry with them, is offered
// the dude's sledgehammer in barter (or it is planted on him). Without the
// fix the screens count only his inventory without what they took off him,
// he takes it and is overloaded after.
static const DevAutotestStep kDevAutotestPartyWeightSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "w00_start" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 97, 2, ROTATION_SE, 10, "w01_sulik", "Kcsulik.int" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 2, 0, 0, 1, "w02_metal_armor" },
    { DEV_AUTOTEST_ACTION_WIELD_PARTY_ITEM, 2, 0, 0, 1, "w03_wears_it" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 6, 0, 0, 1, "w04_sledgehammer" },
    { DEV_AUTOTEST_ACTION_WIELD_PARTY_ITEM, 6, 0, 0, 1, "w05_wields_it" },
    { DEV_AUTOTEST_ACTION_FILL_PARTY_WEIGHT, 19, 0, 0, 1, "w06_full" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 6, 0, 0, 1, "w07_dude_sledgehammer" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "w08_center" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 300, "w09_talk" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "w10_barter", "dialog.tab.barter" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 40, "w11_offer_sledgehammer", "barter.player.pid.6", "barter.offer" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "w12_offered", "barter.offer.button" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "w13_talk", "dialog.tab.talk" },
    { DEV_AUTOTEST_ACTION_CHECK_PARTY_WEIGHT, 0, 0, 0, 1, "w14_not_overloaded" },
};

static const DevAutotestStep kDevAutotestPartyWeightStealSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "v00_start" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 97, 2, ROTATION_SE, 10, "v01_sulik", "Kcsulik.int" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 2, 0, 0, 1, "v02_metal_armor" },
    { DEV_AUTOTEST_ACTION_WIELD_PARTY_ITEM, 2, 0, 0, 1, "v03_wears_it" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 6, 0, 0, 1, "v04_sledgehammer" },
    { DEV_AUTOTEST_ACTION_WIELD_PARTY_ITEM, 6, 0, 0, 1, "v05_wields_it" },
    { DEV_AUTOTEST_ACTION_FILL_PARTY_WEIGHT, 19, 0, 0, 1, "v06_full" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 6, 0, 0, 1, "v07_dude_sledgehammer" },
    { DEV_AUTOTEST_ACTION_STEAL_PARTY_MEMBER, 0, 0, 0, 1, "v08_steal" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "v08b_shown" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 40, "v09_plant_sledgehammer", "loot.left.pid.6", "loot.right" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "v10_closed", "loot.back" },
    { DEV_AUTOTEST_ACTION_CHECK_PARTY_WEIGHT, 0, 0, 0, 1, "v11_not_overloaded" },
};

// A miss on an object that isn't a critter (sfall's fix in combat.cc):
// its damage_p_proc doesn't run. An item on the ground with the Arroyo
// Elder's script (AHElder.int, whose damage_p_proc sets global var 7, the
// same with RPU and without) gets the dude's miss, then a hit: the var
// stays 0 after the miss and is 1 after the hit. Without the fix the miss
// already sets it.
static const DevAutotestStep kDevAutotestMissObjectSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "o00_start" },
    { DEV_AUTOTEST_ACTION_MISS_OBJECT, 0, 0, 0, 10, "o01_miss_then_hit" },
};

// A script drops the armor the dude or a party member wears (drop_obj,
// objectDrop; sfall's fix in proto_instance.cc): its stats go with it.
// Leather armor on (the dude's as the inventory screen puts it on), then
// dropped: the armor class is back to what it was. Without the fix they
// keep the armor's.
static const DevAutotestStep kDevAutotestDropArmorSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "a00_start" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 97, 2, ROTATION_SE, 10, "a01_sulik", "Kcsulik.int" },
    { DEV_AUTOTEST_ACTION_DROP_WORN_ARMOR, 1, 0, 0, 10, "a02_dude_drops" },
    { DEV_AUTOTEST_ACTION_DROP_WORN_ARMOR, 1, 1, 0, 10, "a03_sulik_drops" },
};

// Logs the state (items, the dude's look, the HUD) of a loaded game
// (--dev-load-game=<slot>): for checking a player's save.
static const DevAutotestStep kDevAutotestStateSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 60, "t00_state" },
    { DEV_AUTOTEST_ACTION_LOG_PARTY, 0, 0, 0, 1, "t00b_party" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "t01_inventory" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "t02_closed" },
};

// Klamath's trapper camp and its rat caves with Sulik in the party: down,
// up and down again (the caves then load from the saved map). On the phone
// such a return crashed (2026-10-01): the touch HUD drawn while the map's
// file was read read protos, which broke the read's progress count (see
// `gFileReadProgressReporting` in db.cc). Run with --dev-map=klatrap.map.
static const DevAutotestStep kDevAutotestMapReturnSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "r00_trappers" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 97, 2, ROTATION_SE, 10, "r01_sulik", "Kcsulik.int" },
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 0, -1, 0, 120, "r02_caves", "klaratcv.map" },
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 0, -1, 0, 120, "r03_up", "klatrap.map" },
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 0, -1, 0, 120, "r04_caves_again", "klaratcv.map" },
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 0, -1, 0, 120, "r05_up_again", "klatrap.map" },
};

// Touch recorder: very short taps at the left edge (a system edge gesture
// re-sending touches looks like that) and a tap read late are dumped to
// touch.log.
static const DevAutotestStep kDevAutotestTouchLogSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "k00_start" },
    { DEV_AUTOTEST_ACTION_TOUCH_LOG, 0, 0, 0, 1, "k01_recorder_on" },
    { DEV_AUTOTEST_ACTION_FINGER, 2, 0.01f, 0.5f, 10, "k02_tap" },
    { DEV_AUTOTEST_ACTION_FINGER, 2, 0.01f, 0.52f, 10, "k03_tap" },
    { DEV_AUTOTEST_ACTION_FINGER, 2, 0.01f, 0.5f, 10, "k04_tap" },
    { DEV_AUTOTEST_ACTION_FINGER, 2, 0.01f, 0.48f, 10, "k05_tap" },
    { DEV_AUTOTEST_ACTION_MUI_TAP_THEN_STALL, 600, 0, 0, 240, "k06_tap_during_stall", "hud.0" },
    { DEV_AUTOTEST_ACTION_FINGER, 2, 0.5f, 0.3f, 10, "k07_tap" },
    { DEV_AUTOTEST_ACTION_TOUCH_LOG, 2, 0, 0, 1, "k08_game_closed" },
    { DEV_AUTOTEST_ACTION_TOUCH_LOG, 1, 0, 1, 1, "k09_dumps", "burst of very short taps", "touches read late" },
};

// Mobile character screen in creation mode (over the new game's dude).
static const DevAutotestStep kDevAutotestCreationSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "n00_start" },
    { DEV_AUTOTEST_ACTION_OPEN_CHARACTER_CREATION, 0, 0, 0, 40, "n01_open" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 10, "n01b_wait" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 10, "n02_str_plus", "character.special.0.plus" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "n03_luck_minus", "character.special.6.minus" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "n04_skills", "character.tab.skills" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "n05_tag", "character.skills.0.tag" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "n06_traits", "character.tab.perks" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "n07_trait", "character.traits.1.toggle" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "n08_name", "character.name" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_BACKSPACE, 0, 0, 5, "n08b_erase" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_BACKSPACE, 0, 0, 5, "n08c_erase" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_BACKSPACE, 0, 0, 5, "n08d_erase" },
    { DEV_AUTOTEST_ACTION_TYPE_TEXT, 0, 0, 0, 20, "n08e_typed", "\xd0\x9d\xd0\xb0\xd1\x80\xd0\xb5\xd0\xba Q" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "n09_name_done", "character.name.done" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "n10_age", "character.age.plus" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "n11_gender", "character.gender" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "n12_done", "character.done" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "n13_dialog_closed", "dialog.primary" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "n14_cancelled" },
};

// Party member control tab of the mobile talk screen (test save, Sulik).
static const DevAutotestStep kDevAutotestPartySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "q00_start" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 9, 0, 0, 90, "q01_loaded" },
    { DEV_AUTOTEST_ACTION_TALK_PARTY_MEMBER, 0, 0, 0, 150, "q02_talk" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 120, "q02b_wait" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "q03_party_tab", "dialog.tab.party" },
    { DEV_AUTOTEST_ACTION_LOG_PARTY_CONTROL, 0, 0, 0, 1, "q03b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "q04_berserk", "talk.party.disposition.4" },
    { DEV_AUTOTEST_ACTION_LOG_PARTY_CONTROL, 0, 0, 0, 1, "q04b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "q05_distance", "talk.party.setting.3" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "q06_charge", "talk.party.values.1" },
    { DEV_AUTOTEST_ACTION_LOG_PARTY_CONTROL, 0, 0, 0, 1, "q06b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "q07_best_weapon", "talk.party.weapon" },
    { DEV_AUTOTEST_ACTION_LOG_PARTY_CONTROL, 0, 0, 0, 1, "q07b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "q08_talk_tab", "dialog.tab.talk" },
};

static const DevAutotestStep kDevAutotestTestSaveSteps[] = {
    // Starting in the village plays the vault suit movie inside the map's
    // load: skipped, then the load is waited for (party members can't join
    // and a save made then is half loaded).
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_ESCAPE, 0, 0, 10, "s00_skip_movie" },
    { DEV_AUTOTEST_ACTION_WAIT_MAP_LOADED, 1800, 0, 0, 60, "s00_village" },
    { DEV_AUTOTEST_ACTION_SET_NAME, 0, 0, 0, 1, "s01_name", "\xD2\xE5\xF1\xF2" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 8, 1, 0, 1, "s02_give_8" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 9, 1, 0, 1, "s02_give_9" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 10, 1, 0, 1, "s02_give_10" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 6, 1, 0, 1, "s02_give_6" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 4, 1, 0, 1, "s02_give_4" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 29, 60, 0, 1, "s02_give_29" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 30, 30, 0, 1, "s02_give_30" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 34, 40, 0, 1, "s02_give_34" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 1, 1, 0, 1, "s02_give_1" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 74, 1, 0, 1, "s02_give_74" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 6, 0, 1, "s02_give_40" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 144, 2, 0, 1, "s02_give_144" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 273, 3, 0, 1, "s02_give_273" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 109, 2, 0, 1, "s02_give_109" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 48, 1, 0, 1, "s02_give_48" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 87, 1, 0, 1, "s02_give_87" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 259, 2, 0, 1, "s02_give_259" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 110, 1, 0, 1, "s02_give_110" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 49, 1, 0, 1, "s02_give_49" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 47, 1, 0, 1, "s02_give_47" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 84, 1, 0, 1, "s02_give_84" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 127, 1, 0, 1, "s02_give_127" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 79, 3, 0, 1, "s02_give_79" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 51, 2, 0, 1, "s02_give_51" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 52, 1, 0, 1, "s02_give_52" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 106, 4, 0, 1, "s02_give_106" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 73, 1, 0, 1, "s02_give_73" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 90, 1, 0, 1, "s02_give_90" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 46, 1, 0, 1, "s02_give_46" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 41, 750, 0, 1, "s02_give_41" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 97, 2, ROTATION_SE, 10, "s03_sulik", "Kcsulik.int" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 6, 1, 0, 1, "s03b_sulik_sledgehammer" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 273, 2, 0, 1, "s03c_sulik_powder" },
    { DEV_AUTOTEST_ACTION_WIELD_PARTY_ITEM, 6, 0, 0, 1, "s03d_sulik_wields" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 62, 2, ROTATION_SW, 10, "s04_vic", "DCVic.int" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 8, 1, 0, 1, "s04b_vic_pistol" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 29, 24, 0, 1, "s04c_vic_ammo" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 84, 1, 0, 1, "s04d_vic_lockpicks" },
    { DEV_AUTOTEST_ACTION_WIELD_PARTY_ITEM, 8, 0, 0, 1, "s04e_vic_wields" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "s05_ready" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "s05b_log" },
    { DEV_AUTOTEST_ACTION_LOG_PARTY, 0, 0, 0, 1, "s05c_party" },
    { DEV_AUTOTEST_ACTION_SAVE_GAME, 9, 0, 0, 30, "s06_save", "\xD2\xC5\xD1\xD2: \xE8\xED\xE2\xE5\xED\xF2\xE0\xF0\xFC, 2 \xF1\xEF\xF3\xF2\xED\xE8\xEA\xE0" },
};

// Scenarios changing saves run in a SAVEGAME of their own: the test game's
// saves wait in "SAVEGAME.autotest" meanwhile and come back when the test
// exits (after a run that was killed - when the next one starts).
enum class DevAutotestSaves {
    // The test game's saves.
    Keep,
    // No saves.
    Empty,
    // Only a copy of the test save (slot 10) in slot 1.
    TestSaveInSlot1,
    // The same, its SAVE.DAT cut after the header and the thumbnail (a
    // save that can't be loaded).
    BrokenTestSaveInSlot1,
    // Copies of the test save in slots 1, 11-20 and 21-25.
    QuickRing,
};

static DevAutotestSaves gDevAutotestSaves = DevAutotestSaves::Keep;

static std::string devAutotestSaveRoot()
{
    char path[COMPAT_MAX_PATH];
    snprintf(path, sizeof(path), "%s\\SAVEGAME", settings.system.master_patches_path.c_str());
    compat_windows_path_to_native(path);
    compat_resolve_path(path);
    return path;
}

static void devAutotestRestoreSaves()
{
    std::string root = devAutotestSaveRoot();
    std::string kept = root + ".autotest";

    saveStorage::Info info;
    if (!saveStorage::inspect(kept, info) || !info.exists) {
        return;
    }

    if (saveStorage::removeTree(root) && saveStorage::move(kept, root)) {
        devAutotestLog("  test game's saves restored\n");
    } else {
        devAutotestLog("FAIL: test game's saves are left in %s\n", kept.c_str());
    }
}

static void devAutotestPrepareSaves()
{
    devAutotestRestoreSaves();

    if (gDevAutotestSaves != DevAutotestSaves::Keep) {
        std::string root = devAutotestSaveRoot();
        std::string kept = root + ".autotest";
        bool prepared = saveStorage::move(root, kept) && saveStorage::makeDirectory(root);
        if (prepared && gDevAutotestSaves != DevAutotestSaves::Empty) {
            prepared = saveStorage::copyTree(kept + "/SLOT10", root + "/SLOT01");
        }

        for (int slot = 11; prepared && gDevAutotestSaves == DevAutotestSaves::QuickRing && slot <= 25; slot++) {
            char name[32];
            snprintf(name, sizeof(name), "/SLOT%02d", slot);
            prepared = saveStorage::copyTree(kept + "/SLOT10", root + name);
        }

        if (prepared && gDevAutotestSaves == DevAutotestSaves::BrokenTestSaveInSlot1) {
            std::string path = root + "/SLOT01/SAVE.DAT";
            std::string data;
            FILE* stream = fopen(path.c_str(), "rb");
            if (stream != nullptr) {
                data.resize(40000);
                data.resize(fread(&data[0], 1, data.size(), stream));
                fclose(stream);
            }
            prepared = data.size() == 40000 && saveStorage::writeFile(path, data);
        }

        atexit(devAutotestRestoreSaves);

        if (!prepared) {
            devAutotestLog("FAIL: own SAVEGAME not prepared\n");
            exit(EXIT_FAILURE);
        }

        devAutotestLog("  own SAVEGAME%s\n", gDevAutotestSaves == DevAutotestSaves::Empty ? ", empty" : gDevAutotestSaves == DevAutotestSaves::QuickRing ? " with the test save in slots 1, 11-25"
                                                                                                                                                         : " with the test save in slot 1");
    }

    lsgMobileRefreshSlots();
}

void devAutotestSetScenario(const char* name)
{
    if (strcmp(name, "worldmap") == 0) {
        gDevAutotestSteps = kDevAutotestWorldmapSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestWorldmapSteps) / sizeof(kDevAutotestWorldmapSteps[0]);
    } else if (strcmp(name, "loadsave") == 0) {
        gDevAutotestSteps = kDevAutotestLoadSaveSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestLoadSaveSteps) / sizeof(kDevAutotestLoadSaveSteps[0]);
    } else if (strcmp(name, "savehistory") == 0) {
        gDevAutotestSteps = kDevAutotestSaveHistorySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestSaveHistorySteps) / sizeof(kDevAutotestSaveHistorySteps[0]);
        gDevAutotestSaves = DevAutotestSaves::Empty;
    } else if (strcmp(name, "gamefiles") == 0) {
        gDevAutotestSteps = kDevAutotestGameFilesSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestGameFilesSteps) / sizeof(kDevAutotestGameFilesSteps[0]);
    } else if (strcmp(name, "preferences") == 0) {
        gDevAutotestSteps = kDevAutotestPreferencesSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPreferencesSteps) / sizeof(kDevAutotestPreferencesSteps[0]);
    } else if (strcmp(name, "muiloadsave") == 0) {
        gDevAutotestSteps = kDevAutotestMuiLoadSaveSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMuiLoadSaveSteps) / sizeof(kDevAutotestMuiLoadSaveSteps[0]);
        gDevAutotestSaves = DevAutotestSaves::Empty;
    } else if (strcmp(name, "about") == 0) {
        gDevAutotestSteps = kDevAutotestAboutSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestAboutSteps) / sizeof(kDevAutotestAboutSteps[0]);
    } else if (strcmp(name, "quicksaves") == 0) {
        gDevAutotestSteps = kDevAutotestQuickSavesSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestQuickSavesSteps) / sizeof(kDevAutotestQuickSavesSteps[0]);
        gDevAutotestSaves = DevAutotestSaves::QuickRing;
    } else if (strcmp(name, "muiloadsavedelete") == 0) {
        gDevAutotestSteps = kDevAutotestMuiDeleteSaveSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMuiDeleteSaveSteps) / sizeof(kDevAutotestMuiDeleteSaveSteps[0]);
        gDevAutotestSaves = DevAutotestSaves::TestSaveInSlot1;
    } else if (strcmp(name, "muiloadfailed") == 0) {
        gDevAutotestSteps = kDevAutotestMuiLoadFailedSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMuiLoadFailedSteps) / sizeof(kDevAutotestMuiLoadFailedSteps[0]);
        gDevAutotestSaves = DevAutotestSaves::BrokenTestSaveInSlot1;
    } else if (strcmp(name, "muiloadsavemain") == 0) {
        gDevAutotestSteps = kDevAutotestMuiMainMenuLoadSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMuiMainMenuLoadSteps) / sizeof(kDevAutotestMuiMainMenuLoadSteps[0]);
    } else if (strcmp(name, "muiloadsavemainresume") == 0) {
        gDevAutotestSteps = kDevAutotestMuiMainMenuResumeSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMuiMainMenuResumeSteps) / sizeof(kDevAutotestMuiMainMenuResumeSteps[0]);
    } else if (strcmp(name, "muiloadsavemainempty") == 0) {
        gDevAutotestSteps = kDevAutotestMuiMainMenuEmptySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMuiMainMenuEmptySteps) / sizeof(kDevAutotestMuiMainMenuEmptySteps[0]);
        gDevAutotestSaves = DevAutotestSaves::Empty;
    } else if (strcmp(name, "pccharacter") == 0) {
        gDevAutotestSteps = kDevAutotestPcCharacterSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPcCharacterSteps) / sizeof(kDevAutotestPcCharacterSteps[0]);
    } else if (strcmp(name, "character") == 0) {
        gDevAutotestSteps = kDevAutotestCharacterSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestCharacterSteps) / sizeof(kDevAutotestCharacterSteps[0]);
    } else if (strcmp(name, "combatscreens") == 0) {
        gDevAutotestSteps = kDevAutotestCombatScreensSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestCombatScreensSteps) / sizeof(kDevAutotestCombatScreensSteps[0]);
    } else if (strcmp(name, "pcpipboy") == 0) {
        gDevAutotestSteps = kDevAutotestPcPipboySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPcPipboySteps) / sizeof(kDevAutotestPcPipboySteps[0]);
    } else if (strcmp(name, "pipboy") == 0) {
        gDevAutotestSteps = kDevAutotestPipboySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPipboySteps) / sizeof(kDevAutotestPipboySteps[0]);
    } else if (strcmp(name, "automap") == 0) {
        gDevAutotestSteps = kDevAutotestAutomapSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestAutomapSteps) / sizeof(kDevAutotestAutomapSteps[0]);
    } else if (strcmp(name, "movie") == 0) {
        gDevAutotestSteps = kDevAutotestMovieSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMovieSteps) / sizeof(kDevAutotestMovieSteps[0]);
    } else if (strcmp(name, "titleintro") == 0) {
        gDevAutotestSteps = kDevAutotestTitleIntroSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestTitleIntroSteps) / sizeof(kDevAutotestTitleIntroSteps[0]);
    } else if (strcmp(name, "modbridge") == 0) {
        gDevAutotestSteps = kDevAutotestModBridgeSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestModBridgeSteps) / sizeof(kDevAutotestModBridgeSteps[0]);
    } else if (strcmp(name, "death") == 0) {
        gDevAutotestSteps = kDevAutotestDeathSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestDeathSteps) / sizeof(kDevAutotestDeathSteps[0]);
    } else if (strcmp(name, "endgame") == 0) {
        gDevAutotestSteps = kDevAutotestEndgameSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestEndgameSteps) / sizeof(kDevAutotestEndgameSteps[0]);
    } else if (strcmp(name, "elevator") == 0) {
        gDevAutotestSteps = kDevAutotestElevatorSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestElevatorSteps) / sizeof(kDevAutotestElevatorSteps[0]);
    } else if (strcmp(name, "update") == 0) {
        gDevAutotestSteps = kDevAutotestUpdateSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestUpdateSteps) / sizeof(kDevAutotestUpdateSteps[0]);
    } else if (strcmp(name, "updateingame") == 0) {
        gDevAutotestSteps = kDevAutotestUpdateInGameSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestUpdateInGameSteps) / sizeof(kDevAutotestUpdateInGameSteps[0]);
    } else if (strcmp(name, "mainmenu") == 0) {
        gDevAutotestSteps = kDevAutotestMainMenuSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMainMenuSteps) / sizeof(kDevAutotestMainMenuSteps[0]);
    } else if (strcmp(name, "newgameelder") == 0) {
        gDevAutotestSteps = kDevAutotestNewGameElderSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestNewGameElderSteps) / sizeof(kDevAutotestNewGameElderSteps[0]);
    } else if (strcmp(name, "pipboynotworn") == 0) {
        gDevAutotestSteps = kDevAutotestPipboyNotWornSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPipboyNotWornSteps) / sizeof(kDevAutotestPipboyNotWornSteps[0]);
    } else if (strcmp(name, "armorlook") == 0) {
        gDevAutotestSteps = kDevAutotestArmorLookSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestArmorLookSteps) / sizeof(kDevAutotestArmorLookSteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "armorwalk") == 0) {
        gDevAutotestSteps = kDevAutotestArmorWalkSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestArmorWalkSteps) / sizeof(kDevAutotestArmorWalkSteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "armorsaveload") == 0) {
        gDevAutotestSteps = kDevAutotestArmorSaveLoadSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestArmorSaveLoadSteps) / sizeof(kDevAutotestArmorSaveLoadSteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "barterparty") == 0) {
        gDevAutotestSteps = kDevAutotestBarterPartySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestBarterPartySteps) / sizeof(kDevAutotestBarterPartySteps[0]);
        gDevAutotestTraceItems = true;
        // Party members on the player side (off in the test game's config;
        // runtest.sh puts the config back).
        settings.qol.party_loot_and_barter = true;
    } else if (strcmp(name, "floattext") == 0) {
        gDevAutotestSteps = kDevAutotestFloatTextSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestFloatTextSteps) / sizeof(kDevAutotestFloatTextSteps[0]);
    } else if (strcmp(name, "state") == 0) {
        gDevAutotestSteps = kDevAutotestStateSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestStateSteps) / sizeof(kDevAutotestStateSteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "mapreturn") == 0) {
        gDevAutotestSteps = kDevAutotestMapReturnSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMapReturnSteps) / sizeof(kDevAutotestMapReturnSteps[0]);
    } else if (strcmp(name, "touchlog") == 0) {
        gDevAutotestSteps = kDevAutotestTouchLogSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestTouchLogSteps) / sizeof(kDevAutotestTouchLogSteps[0]);
    } else if (strcmp(name, "staletouch") == 0) {
        gDevAutotestSteps = kDevAutotestStaleTouchSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestStaleTouchSteps) / sizeof(kDevAutotestStaleTouchSteps[0]);
    } else if (strcmp(name, "stuckfinger") == 0) {
        gDevAutotestSteps = kDevAutotestStuckFingerSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestStuckFingerSteps) / sizeof(kDevAutotestStuckFingerSteps[0]);
    } else if (strcmp(name, "perfmap") == 0) {
        gDevAutotestSteps = kDevAutotestPerfMapSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPerfMapSteps) / sizeof(kDevAutotestPerfMapSteps[0]);
    } else if (strcmp(name, "combatspeed") == 0) {
        gDevAutotestSteps = kDevAutotestCombatSpeedSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestCombatSpeedSteps) / sizeof(kDevAutotestCombatSpeedSteps[0]);
    } else if (strcmp(name, "calledshot") == 0) {
        gDevAutotestSteps = kDevAutotestCalledShotSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestCalledShotSteps) / sizeof(kDevAutotestCalledShotSteps[0]);
    } else if (strcmp(name, "creation") == 0) {
        gDevAutotestSteps = kDevAutotestCreationSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestCreationSteps) / sizeof(kDevAutotestCreationSteps[0]);
    } else if (strcmp(name, "party") == 0) {
        gDevAutotestSteps = kDevAutotestPartySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPartySteps) / sizeof(kDevAutotestPartySteps[0]);
    } else if (strcmp(name, "droparmor") == 0) {
        gDevAutotestSteps = kDevAutotestDropArmorSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestDropArmorSteps) / sizeof(kDevAutotestDropArmorSteps[0]);
    } else if (strcmp(name, "missobject") == 0) {
        gDevAutotestSteps = kDevAutotestMissObjectSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMissObjectSteps) / sizeof(kDevAutotestMissObjectSteps[0]);
    } else if (strcmp(name, "partyweight") == 0) {
        gDevAutotestSteps = kDevAutotestPartyWeightSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPartyWeightSteps) / sizeof(kDevAutotestPartyWeightSteps[0]);
    } else if (strcmp(name, "partyweightsteal") == 0) {
        gDevAutotestSteps = kDevAutotestPartyWeightStealSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPartyWeightStealSteps) / sizeof(kDevAutotestPartyWeightStealSteps[0]);
    } else if (strcmp(name, "explodeleave") == 0) {
        gDevAutotestSteps = kDevAutotestExplodeOnLeaveSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestExplodeOnLeaveSteps) / sizeof(kDevAutotestExplodeOnLeaveSteps[0]);
    } else if (strcmp(name, "multihexparty") == 0) {
        gDevAutotestSteps = kDevAutotestMultihexPartySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMultihexPartySteps) / sizeof(kDevAutotestMultihexPartySteps[0]);
    } else if (strcmp(name, "multihexmove") == 0) {
        gDevAutotestSteps = kDevAutotestMultihexMoveSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMultihexMoveSteps) / sizeof(kDevAutotestMultihexMoveSteps[0]);
    } else if (strcmp(name, "multihexretarget") == 0) {
        gDevAutotestSteps = kDevAutotestMultihexRetargetSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMultihexRetargetSteps) / sizeof(kDevAutotestMultihexRetargetSteps[0]);
    } else if (strcmp(name, "npcpickup") == 0) {
        gDevAutotestSteps = kDevAutotestNpcPickupSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestNpcPickupSteps) / sizeof(kDevAutotestNpcPickupSteps[0]);
    } else if (strcmp(name, "retrieveobj") == 0) {
        gDevAutotestSteps = kDevAutotestRetrieveObjectSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestRetrieveObjectSteps) / sizeof(kDevAutotestRetrieveObjectSteps[0]);
    } else if (strcmp(name, "moveaway") == 0) {
        gDevAutotestSteps = kDevAutotestMoveAwaySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMoveAwaySteps) / sizeof(kDevAutotestMoveAwaySteps[0]);
    } else if (strcmp(name, "burstfriendly") == 0) {
        gDevAutotestSteps = kDevAutotestBurstFriendlySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestBurstFriendlySteps) / sizeof(kDevAutotestBurstFriendlySteps[0]);
    } else if (strcmp(name, "wieldstack") == 0) {
        gDevAutotestSteps = kDevAutotestWieldStackSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestWieldStackSteps) / sizeof(kDevAutotestWieldStackSteps[0]);
    } else if (strcmp(name, "drugsave") == 0) {
        gDevAutotestSteps = kDevAutotestDrugSaveSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestDrugSaveSteps) / sizeof(kDevAutotestDrugSaveSteps[0]);
    } else if (strcmp(name, "barterfixes") == 0) {
        gDevAutotestSteps = kDevAutotestBarterFixesSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestBarterFixesSteps) / sizeof(kDevAutotestBarterFixesSteps[0]);
    } else if (strcmp(name, "orig675") == 0) {
        gDevAutotestSteps = kDevAutotestOriginal675Steps;
        gDevAutotestStepCount = sizeof(kDevAutotestOriginal675Steps) / sizeof(kDevAutotestOriginal675Steps[0]);
    } else if (strcmp(name, "mapageheal") == 0) {
        gDevAutotestSteps = kDevAutotestMapAgeHealSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMapAgeHealSteps) / sizeof(kDevAutotestMapAgeHealSteps[0]);
    } else if (strcmp(name, "partycorpse") == 0) {
        gDevAutotestSteps = kDevAutotestPartyCorpseSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPartyCorpseSteps) / sizeof(kDevAutotestPartyCorpseSteps[0]);
    } else if (strcmp(name, "knockouts") == 0) {
        gDevAutotestSteps = kDevAutotestKnockoutsSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestKnockoutsSteps) / sizeof(kDevAutotestKnockoutsSteps[0]);
    } else if (strcmp(name, "bonusmovewalk") == 0) {
        gDevAutotestSteps = kDevAutotestBonusMoveWalkSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestBonusMoveWalkSteps) / sizeof(kDevAutotestBonusMoveWalkSteps[0]);
    } else if (strcmp(name, "bonusmoveload") == 0) {
        gDevAutotestSteps = kDevAutotestBonusMoveLoadSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestBonusMoveLoadSteps) / sizeof(kDevAutotestBonusMoveLoadSteps[0]);
    } else if (strcmp(name, "apturn") == 0) {
        gDevAutotestSteps = kDevAutotestApTurnSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestApTurnSteps) / sizeof(kDevAutotestApTurnSteps[0]);
    } else if (strcmp(name, "crittable") == 0) {
        gDevAutotestSteps = kDevAutotestCritTableSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestCritTableSteps) / sizeof(kDevAutotestCritTableSteps[0]);
    } else if (strcmp(name, "talkheal") == 0) {
        gDevAutotestSteps = kDevAutotestTalkHealSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestTalkHealSteps) / sizeof(kDevAutotestTalkHealSteps[0]);
    } else if (strcmp(name, "fleeheal") == 0) {
        gDevAutotestSteps = kDevAutotestFleeHealSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestFleeHealSteps) / sizeof(kDevAutotestFleeHealSteps[0]);
    } else if (strcmp(name, "pcparty") == 0) {
        gDevAutotestSteps = kDevAutotestPcPartySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPcPartySteps) / sizeof(kDevAutotestPcPartySteps[0]);
    } else if (strcmp(name, "testsave") == 0) {
        gDevAutotestSteps = kDevAutotestTestSaveSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestTestSaveSteps) / sizeof(kDevAutotestTestSaveSteps[0]);
    } else if (strcmp(name, "loot") == 0) {
        gDevAutotestSteps = kDevAutotestLootSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestLootSteps) / sizeof(kDevAutotestLootSteps[0]);
    } else if (strcmp(name, "pcbar") == 0) {
        gDevAutotestSteps = kDevAutotestPcBarSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPcBarSteps) / sizeof(kDevAutotestPcBarSteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "hudtrace") == 0) {
        gDevAutotestSteps = kDevAutotestHudSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestHudSteps) / sizeof(kDevAutotestHudSteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "combattrace") == 0) {
        gDevAutotestSteps = kDevAutotestCombatScreensSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestCombatScreensSteps) / sizeof(kDevAutotestCombatScreensSteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "calledtrace") == 0) {
        gDevAutotestSteps = kDevAutotestCalledShotSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestCalledShotSteps) / sizeof(kDevAutotestCalledShotSteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "pcinventory") == 0) {
        gDevAutotestSteps = kDevAutotestPcInventorySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPcInventorySteps) / sizeof(kDevAutotestPcInventorySteps[0]);
    } else if (strcmp(name, "inventorytrace") == 0) {
        // The inventory scenario, with everything it changes after every step.
        gDevAutotestSteps = kDevAutotestInventorySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestInventorySteps) / sizeof(kDevAutotestInventorySteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "loottrace") == 0) {
        gDevAutotestSteps = kDevAutotestLootSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestLootSteps) / sizeof(kDevAutotestLootSteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "ammo") == 0) {
        gDevAutotestSteps = kDevAutotestAmmoSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestAmmoSteps) / sizeof(kDevAutotestAmmoSteps[0]);
    } else if (strcmp(name, "inventory") == 0) {
        gDevAutotestSteps = kDevAutotestInventorySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestInventorySteps) / sizeof(kDevAutotestInventorySteps[0]);
    } else if (strcmp(name, "bartertrace") == 0) {
        gDevAutotestSteps = kDevAutotestBarterTraceSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestBarterTraceSteps) / sizeof(kDevAutotestBarterTraceSteps[0]);
    } else if (strcmp(name, "headtrace") == 0) {
        gDevAutotestSteps = kDevAutotestHeadTraceSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestHeadTraceSteps) / sizeof(kDevAutotestHeadTraceSteps[0]);
    } else if (strcmp(name, "dialogtrace") == 0) {
        gDevAutotestSteps = kDevAutotestDialogTraceSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestDialogTraceSteps) / sizeof(kDevAutotestDialogTraceSteps[0]);
    } else if (strcmp(name, "barter") == 0) {
        gDevAutotestSteps = kDevAutotestBarterSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestBarterSteps) / sizeof(kDevAutotestBarterSteps[0]);
    } else if (strcmp(name, "talk") == 0) {
        gDevAutotestSteps = kDevAutotestTalkSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestTalkSteps) / sizeof(kDevAutotestTalkSteps[0]);
    } else if (strcmp(name, "ui") == 0) {
        gDevAutotestSteps = kDevAutotestUiSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestUiSteps) / sizeof(kDevAutotestUiSteps[0]);
    } else if (strcmp(name, "tactical") == 0) {
        gDevAutotestSteps = kDevAutotestTacticalSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestTacticalSteps) / sizeof(kDevAutotestTacticalSteps[0]);
    } else if (strcmp(name, "photos") == 0) {
        gDevAutotestSteps = kDevAutotestPhotosSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPhotosSteps) / sizeof(kDevAutotestPhotosSteps[0]);
    } else if (strcmp(name, "behindcritter") == 0) {
        gDevAutotestSteps = kDevAutotestBehindCritterSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestBehindCritterSteps) / sizeof(kDevAutotestBehindCritterSteps[0]);
    } else if (strcmp(name, "magnet") == 0) {
        gDevAutotestSteps = kDevAutotestMagnetSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMagnetSteps) / sizeof(kDevAutotestMagnetSteps[0]);
    } else if (strcmp(name, "hud") == 0) {
        gDevAutotestSteps = kDevAutotestHudSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestHudSteps) / sizeof(kDevAutotestHudSteps[0]);
    }
}

void devAutotestInit(const char* outputPath)
{
    gDevAutotestEnabled = true;
    gDevAutotestOutputPath = outputPath;

    // On a device the game makes it (folders adb makes aren't writable for
    // the app).
    saveStorage::makeDirectory(gDevAutotestOutputPath);

    devAutotestLog("autotest started, zoom range %.2f..%.2f, filter %d\n",
        settings.world_view.zoom_min,
        settings.world_view.zoom_max,
        settings.world_view.filter);
}

bool devAutotestIsEnabled()
{
    return gDevAutotestEnabled;
}

// FNV-1a of [count] values.
static unsigned int devAutotestChecksum(const int* values, int count)
{
    unsigned int hash = 2166136261u;
    for (int index = 0; index < count; index++) {
        unsigned int value = static_cast<unsigned int>(values[index]);
        for (int byte = 0; byte < 4; byte++) {
            hash ^= (value >> (byte * 8)) & 0xFF;
            hash *= 16777619u;
        }
    }
    return hash;
}

static void devAutotestLogDialog()
{
    bool inDialog = GameMode::isInGameMode(GameMode::kDialog);
    devAutotestLog("  dialog: in dialog %d, talking %d, barter %d\n",
        inDialog ? 1 : 0, gameDialogIsTalking() ? 1 : 0, gameDialogIsInBarter() ? 1 : 0);

    if (inDialog) {
        devAutotestLog("  reply: %s\n", gameDialogGetReplyText());
        int optionCount = gameDialogGetOptionCount();
        for (int index = 0; index < optionCount; index++) {
            devAutotestLog("  option %d: %s (reaction %d)\n", index, gameDialogGetOptionText(index), gameDialogGetOptionReaction(index));
        }
    }

    devAutotestLog("  dude: caps %d, items %d, weight %d\n", itemGetTotalCaps(gDude), gDude->data.inventory.length, objectGetInventoryWeight(gDude));

    Object* speaker = inDialog ? gGameDialogSpeaker : nullptr;
    if (speaker != nullptr) {
        std::vector<int> locals;
        Script* script;
        if (scriptGetScript(speaker->sid, &script) != -1) {
            for (int index = 0; index < script->localVarsCount; index++) {
                ProgramValue value;
                if (scriptGetLocalVar(speaker->sid, index, value) == 0) {
                    locals.push_back(value.opcode);
                    locals.push_back(value.integerValue);
                }
            }
        }
        devAutotestLog("  speaker pid %d: caps %d, items %d, locals %d (%08x)\n",
            speaker->pid, itemGetTotalCaps(speaker), speaker->data.inventory.length,
            static_cast<int>(locals.size() / 2), devAutotestChecksum(locals.data(), static_cast<int>(locals.size())));
    }

    BarterView view;
    if (barterGetView(&view)) {
        devAutotestLog("  barter: offer %d, request %d, player table %d, barterer table %d\n",
            view.offerValue, view.requestValue,
            view.playerTable->data.inventory.length, view.bartererTable->data.inventory.length);
    }

    devAutotestLog("  global vars %08x, map vars %08x\n",
        devAutotestChecksum(gGameGlobalVars, gGameGlobalVarsLength),
        devAutotestChecksum(gMapGlobalVars, gMapGlobalVarsLength));
}

// Items of [owner] as "pid x quantity", with the contents of containers.
static std::string devAutotestItemsText(Object* owner)
{
    std::string text;
    Inventory* inventory = &(owner->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        InventoryItem* inventoryItem = &(inventory->items[index]);
        Object* item = inventoryItem->item;
        char entry[64];
        snprintf(entry, sizeof(entry), " %d x%d", item->pid, inventoryItem->quantity);
        text += entry;
        if ((item->flags & OBJECT_IN_RIGHT_HAND) != 0) {
            text += "R";
        }
        if ((item->flags & OBJECT_IN_LEFT_HAND) != 0) {
            text += "L";
        }
        if ((item->flags & OBJECT_WORN) != 0) {
            text += "W";
        }
        if (itemGetType(item) == ITEM_TYPE_WEAPON) {
            snprintf(entry, sizeof(entry), "(ammo %d pid %d)", ammoGetQuantity(item), weaponGetAmmoTypeProtoId(item).pid());
            text += entry;
        }
        if (item->data.inventory.length != 0) {
            text += " [" + devAutotestItemsText(item) + " ]";
        }
    }
    return text;
}

// The interface bar's state (the touch HUD shows it).
static void devAutotestLogInterface()
{
    InterfaceItemAction actions[INTERFACE_ITEM_ACTION_COUNT];
    int count = interfaceGetAvailableItemActions(actions, INTERFACE_ITEM_ACTION_COUNT);
    std::string available;
    for (int index = 0; index < count; index++) {
        available += " " + std::to_string(static_cast<int>(actions[index]));
    }

    InterfaceItemButtonInfo info;
    bool hasInfo = interfaceGetItemButtonInfo(&info);
    int quantity = -1;
    int capacity = -1;
    interfaceGetCurrentItemAmmo(&quantity, &capacity);
    HitMode hitMode = HIT_MODE_PUNCH;
    bool aiming = false;
    int hitModeRc = interfaceGetCurrentHitMode(&hitMode, &aiming);

    devAutotestLog("  bar: enabled %d, hidden %d, end buttons %d, hand %d, action %d, available%s, hit mode %d%s, ammo %d/%d\n",
        interfaceBarEnabled() ? 1 : 0, interfaceBarIsHidden() ? 1 : 0, interfaceBarEndButtonsVisible() ? 1 : 0,
        static_cast<int>(interfaceGetCurrentHand()), static_cast<int>(interfaceGetCurrentItemAction()),
        available.empty() ? " none" : available.c_str(),
        hitModeRc == 0 ? static_cast<int>(hitMode) : -1, aiming ? " aiming" : "", quantity, capacity);
    if (hasInfo) {
        devAutotestLog("  bar item: fid %08x, text %d, aiming %d, ap %d, disabled %d\n",
            info.itemFrmId.fid(), info.textFrmId.fid(), info.aiming ? 1 : 0, info.actionPoints, info.disabled ? 1 : 0);
    }
}

// Everything the inventory and loot screens change (trace scenarios log it
// after every step).
static void devAutotestLogItems()
{
    devAutotestLog("  items:%s\n", devAutotestItemsText(gDude).c_str());

    Object* right = critterGetItem2(gDude);
    Object* left = critterGetItem1(gDude);
    Object* armor = critterGetArmor(gDude);
    devAutotestLog("  dude: right %d, left %d, armor %d, ac %d, hp %d/%d, ap %d, weight %d/%d, caps %d, fid %08x\n",
        right != nullptr ? right->pid : -1, left != nullptr ? left->pid : -1, armor != nullptr ? armor->pid : -1,
        critterGetStat(gDude, STAT_ARMOR_CLASS), critterGetHitPoints(gDude), critterGetStat(gDude, STAT_MAXIMUM_HIT_POINTS),
        gDude->data.critter.combat.ap, objectGetInventoryWeight(gDude), critterGetStat(gDude, STAT_CARRY_WEIGHT),
        itemGetTotalCaps(gDude), gDude->fid);

    // The look on the map is what the dude wears (the art; the weapon part
    // follows the hands).
    const FrmId dudeFrmId = FrmId(gDude);
    const FrmId expectedFrmId = inventoryComputeCritterFrmId(gDude, gDude, right, left, armor,
        interfaceGetCurrentHand(), dudeFrmId.animationType(), dudeFrmId.rotation());
    int lookArt = static_cast<int>(dudeFrmId.frameId<CritterFrameId>());
    int expectedArt = static_cast<int>(expectedFrmId.frameId<CritterFrameId>());
    // An open inventory shows the new look only once closed.
    InventoryView inventoryView;
    bool inventoryOpen = inventoryGetView(&inventoryView);
    devAutotestLog("  look art %d, worn %d: %s\n", lookArt, expectedArt,
        lookArt == expectedArt ? "ok" : (inventoryOpen ? "inventory open" : "MISMATCH"));

    for (Object* member : get_all_party_members_objects(false)) {
        if (member == gDude) {
            continue;
        }
        Object* memberArmor = critterGetArmor(member);
        devAutotestLog("  party pid %d: armor %d, fid %08x, items:%s\n",
            member->pid, memberArmor != nullptr ? memberArmor->pid : -1, member->fid, devAutotestItemsText(member).c_str());
    }

    BarterView barter;
    if (barterGetView(&barter)) {
        devAutotestLog("  barter: party index %d, player table:%s\n", barter.partyIndex, devAutotestItemsText(barter.playerTable).c_str());
    }

    LootView loot;
    if (lootGetView(&loot)) {
        devAutotestLog("  loot target pid %d:%s\n", loot.target->pid, devAutotestItemsText(loot.target).c_str());
    }

    std::string ground;
    for (Object* object = objectFindFirstAtLocation(gDude->elevation, gDude->tile); object != nullptr; object = objectFindNextAtLocation()) {
        if (FrmId(object).objectType() == OBJ_TYPE_ITEM) {
            char entry[32];
            snprintf(entry, sizeof(entry), " %d", object->pid);
            ground += entry;
        }
    }
    devAutotestLog("  ground:%s\n", ground.empty() ? " none" : ground.c_str());
    devAutotestLog("  global vars %08x\n", devAutotestChecksum(gGameGlobalVars, gGameGlobalVarsLength));
}

void devAutotestTick()
{
    if (!gDevAutotestEnabled) {
        return;
    }

    static bool savesPrepared = false;
    if (!savesPrepared) {
        savesPrepared = true;
        devAutotestPrepareSaves();
    }

    if (gDevAutotestSlowFrameMs > 0) {
        SDL_Delay(gDevAutotestSlowFrameMs);
    }

    gDevAutotestFrames++;

    if (gDevAutotestPanFrames > 0) {
        gDevAutotestPanFrames--;
        worldViewPanBy(gDevAutotestPanX, gDevAutotestPanY);
    }

    if (gDevAutotestTimedName != nullptr && !animationIsBusy(gDude)) {
        devAutotestLog("  %s: %u ms, %d hexes\n",
            gDevAutotestTimedName,
            getTicksSince(gDevAutotestTimedStart),
            tileDistanceBetween(gDevAutotestTimedTile, gDude->tile));
        gDevAutotestTimedName = nullptr;
    }

    if (gDevAutotestStep >= gDevAutotestStepCount) {
        devAutotestLog("autotest finished\n");
        exit(EXIT_SUCCESS);
    }

    const DevAutotestStep* step = &(gDevAutotestSteps[gDevAutotestStep]);

    if (step->action == DEV_AUTOTEST_ACTION_PINCH && gDevAutotestPinchFrame < kDevAutotestPinchFrames + 1) {
        if (gDevAutotestPinchFrame == -1) {
            int mouseX;
            int mouseY;
            mouseGetPosition(&mouseX, &mouseY);
            int worldX;
            int worldY;
            worldViewScreenToWorld(mouseX, mouseY, &worldX, &worldY);
            devAutotestLog("%s: before pinch zoom %.3f, mouse (%d, %d), mouse tile %d\n", step->name, worldViewGetZoom(), mouseX, mouseY, tileFromScreenXY(worldX, worldY));
        }
        devAutotestPinchTick(step);
        return;
    }

    if (step->action == DEV_AUTOTEST_ACTION_WAIT_TALKING && !gDevAutotestStepStarted) {
        static int waited = 0;
        bool talking = gameDialogIsTalking() && gameDialogGetOptionCount() > 0;
        if (!talking && waited < static_cast<int>(step->a)) {
            waited++;
            return;
        }
        if (!talking) {
            devAutotestLog("  no talk after %d frames\n", waited);
        }
        waited = 0;
    }

    if (step->action == DEV_AUTOTEST_ACTION_END_TURN_WAIT && !gDevAutotestStepStarted) {
        static int waited = 0;
        static int round = -1;
        if (round == -1) {
            round = _combatNumTurns;
            gameCommandPost(GameCommandType::EndTurn);
            return;
        }
        bool next = isInCombat() && _combatNumTurns > round && _combat_whose_turn() == gDude;
        if (isInCombat() && !next && waited < static_cast<int>(step->a)) {
            waited++;
            return;
        }
        Object* turn = _combat_whose_turn();
        devAutotestLog("  %s after %d frames (round %d, was %d; turn of pid %d)\n", next ? "the dude's next turn" : (isInCombat() ? "STILL WAITING" : "combat is over"), waited,
            _combatNumTurns, round, turn != nullptr ? turn->pid : -1);
        if (!next) {
            devAutotestLog("FAIL: no next turn in combat\n");
            exit(EXIT_FAILURE);
        }
        waited = 0;
        round = -1;
    }

    if (step->action == DEV_AUTOTEST_ACTION_WAIT_MAP_LOADED && !gDevAutotestStepStarted) {
        static int waited = 0;
        if (mapIsLoading() && waited < static_cast<int>(step->a)) {
            waited++;
            return;
        }
        devAutotestLog("  map %s after %d frames\n", mapIsLoading() ? "still loading" : "loaded", waited);
        if (mapIsLoading()) {
            devAutotestLog("FAIL: the map didn't finish loading\n");
        }
        waited = 0;
    }

    if (!gDevAutotestStepStarted) {
        gDevAutotestStepStarted = true;
        gDevAutotestDelay = step->delay;

        Rect visibleRect;
        switch (step->action) {
        case DEV_AUTOTEST_ACTION_ZOOM:
            worldViewSetZoom(step->a,
                step->b < 0 ? screenGetWidth() / 2 : step->b,
                step->c < 0 ? screenGetVisibleHeight() / 2 : step->c);
            break;
        case DEV_AUTOTEST_ACTION_ZOOM_AT_DUDE:
            if (gDude != nullptr) {
                int worldX;
                int worldY;
                tileToScreenXY(gDude->tile, &worldX, &worldY);
                int screenX;
                int screenY;
                worldViewWorldToScreen(worldX + 16, worldY + 8, &screenX, &screenY);
                worldViewSetZoom(step->a, screenX, screenY);
            }
            break;
        case DEV_AUTOTEST_ACTION_PAN:
            worldViewPanBy(step->a, step->b);
            break;
        case DEV_AUTOTEST_ACTION_WALK:
            if (gDude != nullptr) {
                gDevAutotestWalkTarget = tileGetTileInDirection(gDude->tile, ROTATION_SE, static_cast<int>(step->a));
                int worldX;
                int worldY;
                tileToScreenXY(gDevAutotestWalkTarget, &worldX, &worldY);
                int screenX;
                int screenY;
                worldViewWorldToScreen(worldX + 16, worldY + 8, &screenX, &screenY);
                devAutotestLog("  walk from %d to %d via screen (%d, %d)\n", gDude->tile, gDevAutotestWalkTarget, screenX, screenY);
                dudeMoveToTile(gDevAutotestWalkTarget, -1);
            }
            break;
        case DEV_AUTOTEST_ACTION_CHECK_WALK:
            if (gDude != nullptr) {
                devAutotestLog("  dude at %d, target %d: %s\n", gDude->tile, gDevAutotestWalkTarget, gDude->tile == gDevAutotestWalkTarget ? "ok" : "MISMATCH");
            }
            break;
        case DEV_AUTOTEST_ACTION_CHECK_TOUCH_MOVED:
            devAutotestLog("  dude moved by touch from %d to %d\n", gDevAutotestTouchStartTile, gDude != nullptr ? gDude->tile : -1);
            if (gDude == nullptr || gDevAutotestTouchStartTile < 0 || gDude->tile == gDevAutotestTouchStartTile) {
                devAutotestLog("autotest failed: map touch did not move the player\n");
                exit(EXIT_FAILURE);
            }
            break;
        case DEV_AUTOTEST_ACTION_CENTER_DUDE:
            if (gDude != nullptr) {
                tileSetCenter(gDude->tile, TILE_SET_CENTER_REFRESH_WINDOW);
            }
            break;
        case DEV_AUTOTEST_ACTION_ARROW:
            _mouse_set_position(static_cast<int>(step->a), static_cast<int>(step->b));
            gameMouseSetMode(GAME_MOUSE_MODE_ARROW);
            break;
        case DEV_AUTOTEST_ACTION_TALK_PARTY_MEMBER: {
            Object* member = nullptr;
            for (Object* candidate : get_all_party_members_objects(false)) {
                if (candidate != gDude && (step->a == 0 || candidate->pid == (0x1000000 | static_cast<int>(step->a)))) {
                    member = candidate;
                    break;
                }
            }
            if (member != nullptr) {
                gDevAutotestPartyMember = member;
                devAutotestLog("  talk to party member pid %d\n", member->pid);
                gDevAutotestPendingCapture = step->name;
                gDevAutotestStep++;
                gDevAutotestStepStarted = false;
                actionTalk(gDude, member);
                return;
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_CRITTERS:
            // `a` - 1: every elevation (with the critters' scripts).
            for (int elevation = 0; elevation < ELEVATION_COUNT; elevation++) {
                if (step->a == 0 && elevation != gDude->elevation) {
                    continue;
                }
                for (Object* object = objectFindFirstAtElevation(elevation); object != nullptr; object = objectFindNextAtElevation()) {
                    if (object != gDude && FrmId(object).objectType() == OBJ_TYPE_CRITTER) {
                        char scriptName[COMPAT_MAX_PATH] = "-";
                        Script* script;
                        if (object->sid != -1 && scriptGetScript(object->sid, &script) != -1) {
                            scriptsGetFileName(script->index & 0xFFFFFF, scriptName, sizeof(scriptName));
                        }
                        devAutotestLog("  critter pid %d (%d): %s, elevation %d, tile %d, distance %d, sid %d, script %s\n",
                            object->pid, object->pid & 0xFFFFFF, objectGetName(object), elevation, object->tile,
                            objectGetDistanceBetween(gDude, object), object->sid, scriptName);
                    }
                }
            }
            break;
        case DEV_AUTOTEST_ACTION_TALK_TO_PID: {
            int pid = 0x01000000 | static_cast<int>(step->a);
            Object* critter = nullptr;
            // The dude's elevation first, then the others.
            for (int pass = 0; pass <= ELEVATION_COUNT && critter == nullptr; pass++) {
                int elevation = pass == 0 ? gDude->elevation : pass - 1;
                for (Object* object = objectFindFirstAtElevation(elevation); object != nullptr; object = objectFindNextAtElevation()) {
                    if (object->pid == pid) {
                        critter = object;
                        break;
                    }
                }
            }
            if (critter != nullptr) {
                // Next to the critter: no walk, the same start every run.
                int tile = tileGetTileInDirection(critter->tile, ROTATION_SW, 1);
                Rect rect;
                objectSetLocation(gDude, tile, critter->elevation, &rect);
                devAutotestLog("  talk to pid %d at tile %d from tile %d\n", pid, critter->tile, tile);
                gDevAutotestPendingCapture = step->name;
                gDevAutotestStep++;
                gDevAutotestStepStarted = false;
                actionTalk(gDude, critter);
                return;
            }
            devAutotestLog("  no critter pid %d\n", pid);
            break;
        }
        case DEV_AUTOTEST_ACTION_DIALOG_BARTER:
            if (muiIsEnabled()) {
                gameDialogRequestBarter();
            } else {
                enqueueInputEvent(KEY_LOWERCASE_B);
            }
            break;
        case DEV_AUTOTEST_ACTION_BARTER_OFFER:
            if (muiIsEnabled()) {
                barterRequest(BarterRequest::Offer);
            } else {
                enqueueInputEvent(KEY_LOWERCASE_M);
            }
            break;
        case DEV_AUTOTEST_ACTION_BARTER_TALK:
            if (muiIsEnabled()) {
                barterRequest(BarterRequest::Talk);
            } else {
                enqueueInputEvent(KEY_LOWERCASE_T);
            }
            break;
        case DEV_AUTOTEST_ACTION_CHOOSE_OPTION:
            if (muiIsEnabled()) {
                gameDialogChooseOption(static_cast<int>(step->a));
            } else {
                enqueueInputEvent(KEY_1 + static_cast<int>(step->a));
            }
            break;
        case DEV_AUTOTEST_ACTION_LOG_PARTY_CONTROL: {
            PartyControlView view;
            if (gameDialogGetPartyControlView(&view)) {
                devAutotestLog("  party control: weapon pid %d, armor pid %d, hp %d/%d, disposition %d, settings %d %d %d %d %d %d\n",
                    view.weapon != nullptr ? view.weapon->pid : -1,
                    view.armor != nullptr ? view.armor->pid : -1,
                    view.hitPoints, view.maximumHitPoints, view.disposition,
                    view.settings[0], view.settings[1], view.settings[2], view.settings[3], view.settings[4], view.settings[5]);
            } else {
                devAutotestLog("  party control: none\n");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_TYPE_TEXT: {
            SDL_Event event;
            SDL_zero(event);
            event.type = SDL_TEXTINPUT;
            SDL_strlcpy(event.text.text, step->widget, sizeof(event.text.text));
            SDL_PushEvent(&event);
            break;
        }
        case DEV_AUTOTEST_ACTION_EVENT:
            enqueueInputEvent(static_cast<int>(step->a));
            break;
        case DEV_AUTOTEST_ACTION_COMMAND:
            gameCommandPost(static_cast<GameCommandType>(static_cast<int>(step->a)));
            break;
        case DEV_AUTOTEST_ACTION_BACK:
            muiRequestBack();
            break;
        case DEV_AUTOTEST_ACTION_LOG_DIALOG:
            devAutotestLogDialog();
            break;
        case DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS: {
            // The first one takes the windows there are.
            if (!gDevAutotestStartWindowsTaken) {
                gDevAutotestStartWindowsTaken = true;
                std::string windows;
                for (int id = 1; id < 100; id++) {
                    Window* window = windowGetWindow(id);
                    if (window != nullptr) {
                        gDevAutotestStartWindows.insert(id);
                        char entry[64];
                        snprintf(entry, sizeof(entry), " %d(%dx%d%s)", id, window->width, window->height, (window->flags & WINDOW_HIDDEN) != 0 ? " hidden" : "");
                        windows += entry;
                    }
                }
                devAutotestLog("  game windows at start: %d%s\n", static_cast<int>(gDevAutotestStartWindows.size()), windows.c_str());
                break;
            }

            std::string windows;
            for (int id = 1; id < 100; id++) {
                Window* window = windowGetWindow(id);
                if (window == nullptr || gDevAutotestStartWindows.count(id) != 0) {
                    continue;
                }
                char entry[64];
                snprintf(entry, sizeof(entry), " %d(%dx%d%s)", id, window->width, window->height, (window->flags & WINDOW_HIDDEN) != 0 ? " hidden" : "");
                windows += entry;
            }
            devAutotestLog("  new game windows:%s\n", windows.empty() ? " none" : windows.c_str());
            break;
        }
        case DEV_AUTOTEST_ACTION_ADD_SKILL: {
            int rc = 0;
            int added = 0;
            for (; added < static_cast<int>(step->b); added++) {
                rc = skillAdd(gDude, static_cast<Skill>(static_cast<int>(step->a)));
                if (rc != 0) {
                    break;
                }
            }
            devAutotestLog("  skill %d: %d points in (rc %d), %d%%, %d left\n", static_cast<int>(step->a), added, rc,
                skillGetValue(gDude, static_cast<Skill>(static_cast<int>(step->a))), pcGetStat(PC_STAT_UNSPENT_SKILL_POINTS));
            break;
        }
        case DEV_AUTOTEST_ACTION_SET_STAT:
            critterSetBaseStat(gDude, static_cast<Stat>(static_cast<int>(step->a)), static_cast<int>(step->b));
            break;
        case DEV_AUTOTEST_ACTION_SET_GVAR:
            gameSetGlobalVar(static_cast<GameGlobalVar>(static_cast<int>(step->a)), static_cast<int>(step->b));
            break;
        case DEV_AUTOTEST_ACTION_MOVE_NEAR_CRITTER: {
            Object* critter = devAutotestFindNearestCritter();
            if (critter != nullptr) {
                int tile = tileGetTileInDirection(critter->tile, ROTATION_NE, static_cast<int>(step->a));
                Rect rect;
                objectSetLocation(gDude, tile, gDude->elevation, &rect);
                tileSetCenter(gDude->tile, TILE_SET_CENTER_REFRESH_WINDOW);
                devAutotestLog("  dude moved to tile %d near critter at %d\n", gDude->tile, critter->tile);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_SET_COMBAT_SPEED: {
            static bool saved = false;
            static int combatSpeed;
            static bool playerSpeedup;
            static bool allAnimations;
            if (!saved) {
                saved = true;
                combatSpeed = settings.preferences.combat_speed;
                playerSpeedup = settings.preferences.player_speedup;
                allAnimations = settings.enhancements.combat_speed_all_animations;
            }

            if (step->a < 0) {
                settings.preferences.combat_speed = combatSpeed;
                settings.preferences.player_speedup = playerSpeedup;
                settings.enhancements.combat_speed_all_animations = allAnimations;
            } else {
                settings.preferences.combat_speed = static_cast<int>(step->a);
                settings.preferences.player_speedup = step->b != 0;
                settings.enhancements.combat_speed_all_animations = step->c != 0;
            }
            devAutotestLog("  combat speed %d, player speedup %d, all animations %d\n",
                settings.preferences.combat_speed,
                settings.preferences.player_speedup ? 1 : 0,
                settings.enhancements.combat_speed_all_animations ? 1 : 0);
            break;
        }
        case DEV_AUTOTEST_ACTION_TIME_DUDE_ANIMATION: {
            int kind = static_cast<int>(step->a);
            int tile = tileGetTileInDirection(gDude->tile, static_cast<Rotation>(static_cast<int>(step->c)), static_cast<int>(step->b));
            int rc = -1;
            if (reg_anim_begin(ANIMATION_REQUEST_RESERVED) == 0) {
                if (kind == 2) {
                    rc = animationRegisterAnimate(gDude, static_cast<AnimationType>(static_cast<int>(step->b)), 0);
                } else if (kind == 1) {
                    rc = animationRegisterRunToTile(gDude, tile, gDude->elevation, gDude->data.critter.combat.ap, 0);
                } else {
                    rc = animationRegisterMoveToTile(gDude, tile, gDude->elevation, gDude->data.critter.combat.ap, 0);
                }
                if (reg_anim_end() != 0) {
                    rc = -1;
                }
            }

            if (rc == 0) {
                gDevAutotestTimedName = step->name;
                gDevAutotestTimedStart = getTicks();
                gDevAutotestTimedTile = gDude->tile;
            } else {
                devAutotestLog("FAIL: %s: animation not started\n", step->name);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_SLOW_FRAMES:
            gDevAutotestSlowFrameMs = static_cast<int>(step->a);
            break;
        case DEV_AUTOTEST_ACTION_STEP_UNDER_ROOF: {
            int target = -1;
            if (step->a != 0) {
                target = gDevAutotestRoofStartTile;
            } else {
                gDevAutotestRoofStartTile = gDude->tile;
                int bestDistance = INT_MAX;
                for (int tile = 0; tile < HEX_GRID_SIZE; tile++) {
                    int distance = tileDistanceBetween(gDude->tile, tile);
                    if (distance > 25 || distance >= bestDistance) {
                        continue;
                    }

                    int roofX = tile % HEX_GRID_WIDTH / 2;
                    int roofY = tile / HEX_GRID_WIDTH / 2;
                    int square = _square[gDude->elevation]->tileFid[roofX + SQUARE_GRID_WIDTH * roofY];
                    if (RoofTileFrmId(square).frameId<TileFrameId>() == TileFrameId::Grid) {
                        continue;
                    }

                    if (_obj_blocking_at(gDude, tile, gDude->elevation) == nullptr) {
                        target = tile;
                        bestDistance = distance;
                    }
                }
            }

            if (target != -1) {
                devAutotestLog("  step from %d to %d (%s a roof)\n", gDude->tile, target, step->a != 0 ? "out from" : "under");
                Rect rect;
                objectSetLocation(gDude, target, gDude->elevation, &rect);
                tileWindowRefreshRect(&rect, gDude->elevation);
            } else {
                devAutotestLog("FAIL: no tile under a roof nearby\n");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_MUI_TAP_THEN_STALL: {
            float x;
            float y;
            if (muiGetWidgetCenter(step->widget, &x, &y)) {
                devAutotestPushFinger(SDL_FINGERDOWN, 8, x, y);
                devAutotestPushFinger(SDL_FINGERUP, 8, x, y);
                SDL_Delay(static_cast<Uint32>(step->a));
            } else {
                devAutotestLog("FAIL: mui %s NOT FOUND\n", step->widget);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_CHECK_MENU_PICTURES: {
            int width;
            int height;
            // The named picture is the high resolution patch's (f2_res.dat):
            // a game without it (plain Fallout 2) has only the numeric one.
            SDL_Texture* named = muiPictureTexture(InterfaceFrmId("HR_MAINMENU.FRM"), &width, &height);
            bool hasNamed = named != nullptr;
            bool ok = !hasNamed || (width > 0 && height > 0);
            int namedWidth = width;
            int namedHeight = height;
            SDL_Texture* legacy = muiPictureTexture(FrmId(InterfaceFrameId::MainMenuBackgroundImage), &width, &height);
            ok = ok && legacy != nullptr && legacy != named && width > 0 && height > 0;
            for (int attempt = 0; attempt < 2; attempt++) {
                SDL_Texture* missing = muiPictureTexture(InterfaceFrmId("__CE_MISSING_PICTURE__.FRM"), &width, &height);
                ok = ok && missing == nullptr && width == 0 && height == 0;
            }
            if (hasNamed) {
                SDL_Texture* cached = muiPictureTexture(InterfaceFrmId("HR_MAINMENU.FRM"), &width, &height);
                ok = ok && cached == named && width == namedWidth && height == namedHeight;
            }
            devAutotestLog("  menu pictures: named %s, numeric, missing path%s: %s\n",
                hasNamed ? (std::to_string(namedWidth) + "x" + std::to_string(namedHeight)).c_str() : "none (no f2_res.dat)",
                hasNamed ? ", cached identity" : "",
                ok ? "PASS" : "FAIL");
            if (!ok) {
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_UPDATE_STATE:
            appUpdateTestState(static_cast<AppUpdateState>(static_cast<int>(step->a)), step->widget, step->b != 0 ? static_cast<AppUpdateError>(static_cast<int>(step->b)) : AppUpdateError::Download);
            devAutotestLog("  update state %d tag %s\n", static_cast<int>(step->a), step->widget != nullptr ? step->widget : "");
            break;
        case DEV_AUTOTEST_ACTION_CHECK_UPDATE_INSTALLS: {
            bool ok = appUpdateTestInstalls() == static_cast<int>(step->a);
            devAutotestLog("  update installs %d expected %d: %s\n", appUpdateTestInstalls(), static_cast<int>(step->a), ok ? "PASS" : "FAIL");
            if (!ok) exit(EXIT_FAILURE);
            break;
        }
        case DEV_AUTOTEST_ACTION_CHECK_WIDGET: {
            float x;
            float y;
            bool present = muiGetWidgetCenter(step->widget, &x, &y);
            bool ok = present == (step->a != 0);
            devAutotestLog("  widget %s %s: %s\n", step->widget, present ? "present" : "absent", ok ? "PASS" : "FAIL");
            if (!ok) {
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_MUI_DRAG_BY: {
            float x;
            float y;
            if (step->widget != nullptr && muiGetWidgetCenter(step->widget, &x, &y)) {
                float width = static_cast<float>(screenGetWidth());
                float height = static_cast<float>(screenGetHeight());
                devAutotestLog("  mui drag %s by (%.2f, %.2f)\n", step->widget, step->a, step->b);
                devAutotestStartTouch(x * width, y * height, (x + step->a) * width, (y + step->b) * height, 0, 25);
            } else {
                devAutotestLog("FAIL: mui %s NOT FOUND\n", step->widget != nullptr ? step->widget : "?");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_PREFERENCES: {
            PreferenceValues values = preferencesGetValues();
            devAutotestLog("  preferences: combat speed %d, player speedup %d, master volume %d, brightness %.3f, text delay %.1f, violence %d\n",
                values.combatSpeed, values.playerSpeedup, values.masterVolume, values.brightness, values.textBaseDelay, values.violenceLevel);
            devAutotestLog("  settings: all animations %d, walk when sneaking %d, hud scale %d, map filter %d\n",
                settings.enhancements.combat_speed_all_animations ? 1 : 0, settings.qol.walk_when_sneaking ? 1 : 0,
                settings.touch.hud_scale, settings.world_view.filter);
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_SAVES: {
            lsgMobileRefreshSlots();
            int count = 0;
            std::string quick;
            for (int slot = 0; slot < 1000; slot++) {
                MobileSaveSlotInfo info;
                if (lsgMobileGetSlotInfo(slot, &info) && info.state == MobileSaveSlotState::Occupied) {
                    count++;
                    if (lsgMobileIsQuickSlot(slot)) {
                        quick += " " + std::to_string(slot + 1);
                    }
                }
            }
            devAutotestLog("  saves: %d, quick saves setting %d (auto_quick_save %d), quick:%s\n",
                count, lsgQuickSaveCount(), settings.ui.auto_quick_save, quick.empty() ? " none" : quick.c_str());
            if (count != static_cast<int>(step->a)) {
                devAutotestLog("FAIL: %d saves, expected %d\n", count, static_cast<int>(step->a));
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_MAP_TRANSITION: {
            char name[COMPAT_MAX_PATH];
            strncpy(name, step->widget, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            MapTransition transition;
            transition.map = wmMapMatchNameToIdx(name);
            transition.elevation = static_cast<int>(step->a);
            transition.tile = static_cast<int>(step->b);
            transition.rotation = ROTATION_NE;
            devAutotestLog("  map %d now, to %s (%d), party %d\n", static_cast<int>(gMapHeader.index), step->widget,
                static_cast<int>(transition.map), static_cast<int>(get_all_party_members_objects(true).size()));
            if (transition.map == MAP_INVALID) {
                devAutotestLog("FAIL: no map %s\n", step->widget);
            } else {
                mapSetTransition(&transition);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_LOG: {
            if (step->a == 0) {
                touchLogStartForTest();
                break;
            }

            if (step->a == 2) {
                touchLogExit();
                break;
            }

            FILE* stream = fopen("touch.log", "r");
            if (stream == nullptr) {
                devAutotestLog("FAIL: no touch.log\n");
                break;
            }

            std::string text;
            char line[512];
            int dumps = 0;
            int fingers = 0;
            while (fgets(line, sizeof(line), stream) != nullptr) {
                text += line;
                if (strncmp(line, "=== ", 4) == 0) {
                    dumps++;
                    devAutotestLog("  touch.log dump:%s", strchr(line + 4, ' ') != nullptr ? strchr(line + 4, ' ') : line);
                } else if (strstr(line, "-> ") != nullptr) {
                    fingers++;
                }
            }
            fclose(stream);

            devAutotestLog("  touch.log: %d dump(s), %d finger events\n", dumps, fingers);
            const char* reasons[] = { step->widget, step->target };
            for (const char* reason : reasons) {
                if (reason != nullptr && text.find(std::string("! ") + reason) == std::string::npos) {
                    devAutotestLog("FAIL: touch.log has no \"%s\"\n", reason);
                }
            }
            if (step->c == 1 && text.find("  game closed\n") == std::string::npos) {
                devAutotestLog("FAIL: touch.log has no \"game closed\" dump\n");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_FINGER:
            if (step->a == 2) {
                devAutotestPushFinger(SDL_FINGERDOWN, 7, step->b, step->c);
                devAutotestPushFinger(SDL_FINGERUP, 7, step->b, step->c);
            } else {
                devAutotestPushFinger(step->a == 0 ? SDL_FINGERDOWN : SDL_FINGERUP, 7, step->b, step->c);
            }
            break;
        case DEV_AUTOTEST_ACTION_PAN_FRAMES:
            gDevAutotestPanX = step->a;
            gDevAutotestPanY = step->b;
            gDevAutotestPanFrames = static_cast<int>(step->c);
            break;
        case DEV_AUTOTEST_ACTION_PERF_LOG: {
            int frames = gDevAutotestFrames - gDevAutotestPerfFrame;
            unsigned int now = getTicks();
            long long uploads = perfMonitorGetTotal(PerfCount::WorldUploads);
            long long pixels = perfMonitorGetTotal(PerfCount::WorldPixels);
            long long drawn = perfMonitorGetTotal(PerfCount::WorldDrawnPixels);
            if (frames > 0 && gDevAutotestPerfTime != 0) {
                devAutotestLog("  perf %s: %d frames, %.1f ms/frame, per frame: drawn %.0f kpx, uploads %.1f, %.0f kpx\n",
                    step->name,
                    frames,
                    static_cast<double>(getTicksBetween(now, gDevAutotestPerfTime)) / frames,
                    static_cast<double>(drawn - gDevAutotestPerfDrawn) / frames / 1000.0,
                    static_cast<double>(uploads - gDevAutotestPerfUploads) / frames,
                    static_cast<double>(pixels - gDevAutotestPerfPixels) / frames / 1000.0);
            }
            gDevAutotestPerfDrawn = drawn;
            gDevAutotestPerfFrame = gDevAutotestFrames;
            gDevAutotestPerfTime = now;
            gDevAutotestPerfUploads = uploads;
            gDevAutotestPerfPixels = pixels;
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_ANIMATION_SOUND: {
            Sound* sound = animationLoadSoundEffect(gDude, step->widget);
            if (sound != nullptr) {
                int bytesPerSecond = sound->bitsPerSample / 8 * sound->channels * sound->rate;
                devAutotestLog("  sound %s: %d ms (tempo %.2f)\n", step->widget, static_cast<int>(static_cast<long long>(sound->fileSize) * 1000 / bytesPerSecond), sound->tempo);
                soundEffectDelete(sound);
            } else {
                devAutotestLog("FAIL: sound %s not loaded\n", step->widget);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_SET_ACTION_POINTS:
            gDude->data.critter.combat.ap = static_cast<int>(step->a);
            interfaceRenderActionPoints(gDude->data.critter.combat.ap, _combat_free_move);
            break;
        case DEV_AUTOTEST_ACTION_ADD_EXPERIENCE:
            pcAddExperience(static_cast<int>(step->a));
            break;
        case DEV_AUTOTEST_ACTION_LOG_CHARACTER: {
            int perks = 0;
            for (int perk = 0; perk < PERK_COUNT; perk++) {
                perks += perkGetRank(gDude, static_cast<Perk>(perk));
            }
            devAutotestLog("  character: level %d, xp %d, skill points %d, perks owed %d, perk ranks %d, small guns %d, big guns %d, unarmed %d\n",
                pcGetStat(PC_STAT_LEVEL),
                pcGetStat(PC_STAT_EXPERIENCE),
                pcGetStat(PC_STAT_UNSPENT_SKILL_POINTS),
                characterEditorGetPerkOwed(),
                perks,
                skillGetValue(gDude, SKILL_SMALL_GUNS),
                skillGetValue(gDude, SKILL_BIG_GUNS),
                skillGetValue(gDude, SKILL_UNARMED));
            break;
        }
        case DEV_AUTOTEST_ACTION_PLAY_MOVIE:
            devAutotestLog("  playing movie %d\n", static_cast<int>(step->a));
            gDevAutotestPendingCapture = step->name;
            gDevAutotestStep++;
            gDevAutotestStepStarted = false;
            gameMoviePlay(static_cast<int>(step->a), GAME_MOVIE_FADE_IN | GAME_MOVIE_FADE_OUT | GAME_MOVIE_PAUSE_MUSIC);
            devAutotestLog("  movie closed\n");
            return;
        case DEV_AUTOTEST_ACTION_CHECK_MOVIE: {
            int width = 0;
            int height = 0;
            bool playing = movieIsMobile() && movieGetFrameTexture(&width, &height) != nullptr;
            devAutotestLog("  movie frame: active %d, %dx%d\n", playing ? 1 : 0, width, height);
            if (!playing) {
                devAutotestLog("autotest failed: expected a decoded movie frame\n");
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_POINTER: {
            int x;
            int y;
            mouseGetPointerPosition(&x, &y);
            int worldX;
            int worldY;
            worldViewScreenToWorld(x, y, &worldX, &worldY);
            int tile = tileFromScreenXY(worldX, worldY);
            Object* object = playerObjectAt(worldX, worldY, OBJ_TYPE_INVALID, true, gElevation);
            Object* critter = devAutotestFindNearestCritter();
            devAutotestLog("  pointer (%d, %d), tile %d, object pid %d%s, buttons %d, click hook calls %d\n",
                x, y, tile,
                object != nullptr ? object->pid : -1,
                object != nullptr && object == critter ? " (the critter)" : "",
                mouseGetPointerButtons(),
                inputGetMouseClickHookCalls());
            break;
        }
        case DEV_AUTOTEST_ACTION_WORLDMAP_AREA: {
            City area = static_cast<City>(static_cast<int>(step->a));
            wmAreaSetVisibleState(area, CITY_STATE_KNOWN, true);
            // An unknown area becomes known first (the game's rule).
            wmAreaMarkVisitedState(area, VisitedState::Visited);
            wmAreaMarkVisitedState(area, VisitedState::Visited);
            wmTeleportToArea(area);
            devAutotestLog("  party at area %d\n", static_cast<int>(area));
            break;
        }
        case DEV_AUTOTEST_ACTION_KILL_DUDE:
            critterKill(gDude, ANIM_INVALID, true);
            devAutotestLog("  dude killed: results 0x%x, hp %d\n", gDude->data.critter.combat.results, gDude->data.critter.hp);
            break;
        case DEV_AUTOTEST_ACTION_REQUEST_ENDGAME:
            scriptsRequestEndgame();
            break;
        case DEV_AUTOTEST_ACTION_OPEN_ELEVATOR: {
            Map map = static_cast<Map>(mapGetCurrentMap());
            int elevation = gElevation;
            int tile = -1;
            devAutotestLog("  opening elevator %d at map %d elevation %d\n", static_cast<int>(step->a), map, elevation);
            gDevAutotestPendingCapture = step->name;
            gDevAutotestStep++;
            gDevAutotestStepStarted = false;
            int rc = elevatorSelectLevel(static_cast<int>(step->a), &map, &elevation, &tile);
            devAutotestLog("  elevator closed: rc %d, map %d elevation %d tile %d\n", rc, map, elevation, tile);
            return;
        }
        case DEV_AUTOTEST_ACTION_OPEN_CHARACTER_CREATION:
            devAutotestLog("  opening character creation\n");
            gDevAutotestPendingCapture = step->name;
            gDevAutotestStep++;
            gDevAutotestStepStarted = false;
            characterEditorShow(true);
            devAutotestLog("  character creation closed\n");
            return;
        case DEV_AUTOTEST_ACTION_TAP_DIALOG_WINDOW: {
            Rect rect;
            int win = gameDialogGetWindow();
            if (win != -1 && windowGetRect(win, &rect) == 0) {
                float x = static_cast<float>(rect.left + step->a);
                float y = static_cast<float>(rect.top + step->b);
                devAutotestLog("  dialog window at (%d, %d), tap (%.0f, %.0f)\n", rect.left, rect.top, x, y);
                devAutotestStartTouch(x, y, x, y, 1, 0);
            } else {
                devAutotestLog("  no dialog window\n");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_PAN_DURING_ENEMY_TURN: {
            Rect visibleRect;
            worldViewGetVisibleRect(&visibleRect);
            devAutotestLog("  before: visible world rect (%d, %d) - (%d, %d)\n", visibleRect.left, visibleRect.top, visibleRect.right, visibleRect.bottom);
            gameCommandPost(GameCommandType::EndTurn);
            float x = static_cast<float>(screenGetWidth() / 2);
            float y = static_cast<float>(screenGetVisibleHeight() / 2);
            devAutotestStartTouch(x, y, x + step->a, y + step->b, 0, 15);
            gDevAutotestTouchDelay = static_cast<int>(step->c);
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_DRAG: {
            float x = static_cast<float>(screenGetWidth() / 2);
            float y = static_cast<float>(screenGetVisibleHeight() / 2);
            devAutotestStartTouch(x, y, x + step->a, y + step->b, 0, 15);
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_TILE:
            if (gDude != nullptr) {
                gDevAutotestTouchStartTile = gDude->tile;
                gDevAutotestWalkTarget = tileGetTileInDirection(gDude->tile, static_cast<Rotation>(static_cast<int>(step->b)), static_cast<int>(step->a));
                float x;
                float y;
                devAutotestGetTileScreenPosition(gDevAutotestWalkTarget, &x, &y);
                devAutotestLog("  tap from %d to %d\n", gDude->tile, gDevAutotestWalkTarget);
                devAutotestStartTouch(x, y, x, y, 1, 0);
            }
            break;
        case DEV_AUTOTEST_ACTION_TOUCH_LONG_PRESS_CRITTER:
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER: {
            // `b` - 1: the nearest enemy in the line of fire.
            Object* critter = step->b == 1 ? devAutotestFindNearestEnemyInSight() : devAutotestFindNearestCritter();
            if (critter != nullptr) {
                float x;
                float y;
                devAutotestGetObjectScreenPosition(critter, &x, &y);
                bool longPress = step->action == DEV_AUTOTEST_ACTION_TOUCH_LONG_PRESS_CRITTER;
                devAutotestLog("  %s critter at tile %d (distance %d)\n", longPress ? "long press" : "tap", critter->tile, objectGetDistanceBetween(gDude, critter));
                devAutotestStartTouch(x, y, x, y, longPress ? 45 : 1, 0);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_CRITTER: {
            Object* critter = devAutotestFindNearestCritter();
            if (critter != nullptr) {
                int tile = tileGetTileInDirection(critter->tile, tileGetRotationTo(critter->tile, gDude->tile), 1);
                float x;
                float y;
                devAutotestGetTileScreenPosition(tile, &x, &y);
                devAutotestLog("  tap tile %d next to critter at %d\n", tile, critter->tile);
                devAutotestStartTouch(x, y, x, y, 1, 0);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_GIVE_ITEM: {
            // `b` - quantity, `c` - ammo: rounds in the clip not full.
            Object* item;
            if (objectCreateWithProtoId(&item, ProtoId(static_cast<int>(step->a))) == 0) {
                int quantity = step->b > 0 ? static_cast<int>(step->b) : 1;
                if (step->c > 0 && itemGetType(item) == ITEM_TYPE_AMMO) {
                    ammoSetQuantity(item, static_cast<int>(step->c));
                }
                // As `add_obj_to_inven`: off the map once carried (or it is
                // freed with the map and the inventory both).
                if (itemAdd(gDude, item, quantity) == 0) {
                    _obj_disconnect(item, nullptr);
                }
                devAutotestLog("  gave item pid %d x%d\n", static_cast<int>(step->a), quantity);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_KEY: {
            SDL_Event event;
            SDL_zero(event);
            event.type = SDL_KEYDOWN;
            event.key.state = SDL_PRESSED;
            event.key.keysym.scancode = static_cast<SDL_Scancode>(static_cast<int>(step->a));
            SDL_PushEvent(&event);
            event.type = SDL_KEYUP;
            event.key.state = SDL_RELEASED;
            SDL_PushEvent(&event);
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_SCREEN:
            devAutotestStartTouch(step->a, step->b, step->a, step->b, 1, 0);
            break;
        case DEV_AUTOTEST_ACTION_LOG_INVENTORY: {
            Object* rightHand = critterGetItem2(gDude);
            Object* leftHand = critterGetItem1(gDude);
            Object* armor = critterGetArmor(gDude);
            devAutotestLog("  inventory length %d, right hand pid %d, left hand pid %d, armor pid %d\n",
                gDude->data.inventory.length,
                rightHand != nullptr ? rightHand->pid : -1,
                leftHand != nullptr ? leftHand->pid : -1,
                armor != nullptr ? armor->pid : -1);

            // Items: pid x quantity, loaded ammo, container contents.
            Inventory* inventory = &(gDude->data.inventory);
            for (int index = 0; index < inventory->length; index++) {
                Object* item = inventory->items[index].item;
                devAutotestLog("    pid %d x%d", item->pid, inventory->items[index].quantity);
                if (itemGetType(item) == ITEM_TYPE_WEAPON && ammoGetCapacity(item) > 0) {
                    devAutotestLog(" ammo %d/%d", ammoGetQuantity(item), ammoGetCapacity(item));
                }
                if (itemGetType(item) == ITEM_TYPE_CONTAINER) {
                    devAutotestLog(" contains %d", item->data.inventory.length);
                }
                devAutotestLog("\n");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER: {
            Object* critter;
            int pid = 0x1000000 | static_cast<int>(step->a);
            if (objectCreateWithProtoId(&critter, ProtoId(pid)) == 0) {
                int distance = step->b > 0 ? static_cast<int>(step->b) : 2;
                Rotation direction = step->b > 0 ? static_cast<Rotation>(static_cast<int>(step->c)) : ROTATION_SE;
                int tile = tileGetTileInDirection(gDude->tile, direction, distance);
                Rect rect;
                objectSetLocation(critter, tile, gDude->elevation, &rect);
                tileWindowRefreshRect(&rect, gDude->elevation);
                devAutotestAttachScript(critter, step->widget);
                int rc = partyMemberAdd(critter);
                gDevAutotestPartyMember = critter;
                devAutotestLog("  party member pid %d at tile %d: %s\n", pid, tile, rc == 0 ? "joined" : "FAIL");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_FILL_PARTY_WEIGHT: {
            Object* member = gDevAutotestPartyMember;
            int pid = static_cast<int>(step->a);
            Object* item;
            if (member != nullptr && objectCreateWithProtoId(&item, ProtoId(pid)) == 0) {
                int unit = std::max(itemGetWeight(item), 1);
                int free = critterGetStat(member, STAT_CARRY_WEIGHT) - objectGetInventoryWeight(member);
                int quantity = (free - static_cast<int>(step->b)) / unit;
                if (quantity > 0 && itemAdd(member, item, quantity) == 0) {
                    _obj_disconnect(item, nullptr);
                } else {
                    objectDestroy(item, nullptr);
                }
                devAutotestLog("  party member filled with pid %d x%d, can carry %d more\n", pid, std::max(quantity, 0),
                    critterGetStat(member, STAT_CARRY_WEIGHT) - objectGetInventoryWeight(member));
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_MOVE_PARTY_MEMBER:
            if (gDevAutotestPartyMember != nullptr) {
                int tile = tileGetTileInDirection(gDude->tile, ROTATION_SE, static_cast<int>(step->a));
                Rect rect;
                objectSetLocation(gDevAutotestPartyMember, tile, gDude->elevation, &rect);
                tileWindowRefreshRect(&rect, gDude->elevation);
                devAutotestLog("  party member at tile %d, %d tiles away\n", tile, tileDistanceBetween(gDude->tile, tile));
            }
            break;
        case DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM: {
            Object* item;
            Object* member = devAutotestPartyMember();
            if (member != nullptr && objectCreateWithProtoId(&item, ProtoId(static_cast<int>(step->a))) == 0) {
                int quantity = step->b > 0 ? static_cast<int>(step->b) : 1;
                if (itemAdd(member, item, quantity) == 0) {
                    _obj_disconnect(item, nullptr);
                }
                devAutotestLog("  gave party member item pid %d x%d\n", static_cast<int>(step->a), quantity);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_WIELD_PARTY_ITEM:
            if (gDevAutotestPartyMember != nullptr) {
                Object* item = objectGetCarriedObjectByProtoId(gDevAutotestPartyMember, ProtoId(static_cast<int>(step->a)));
                int rc = item != nullptr ? inventoryEquip(gDevAutotestPartyMember, item, HAND_RIGHT) : -1;
                devAutotestLog("  party member wields pid %d: %s\n", static_cast<int>(step->a), rc != -1 ? "ok" : "FAIL");
            }
            break;
        case DEV_AUTOTEST_ACTION_SET_NAME:
            if (step->widget != nullptr) {
                dudeSetName(step->widget);
            }
            break;
        case DEV_AUTOTEST_ACTION_LOAD_GAME: {
            int rc = lsgLoadGameFromSlot(static_cast<int>(step->a));
            devAutotestLog("  loaded slot %d: %s\n", static_cast<int>(step->a) + 1, rc != -1 ? "ok" : "FAIL");
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_PARTY:
            for (Object* member : get_all_party_members_objects(false)) {
                Object* weapon = critterGetItem2(member);
                devAutotestLog("  party member pid %d, items %d, script %d, in hand pid %d\n", member->pid, member->data.inventory.length, member->sid, weapon != nullptr ? weapon->pid : -1);
                // What its combat AI goes by: running away below min hp,
                // drugs (combat_ai.cc).
                if (ProtoId(member->pid).objectType() == OBJ_TYPE_CRITTER) {
                    AiPacket* ai = aiGetPacket(member);
                    int healing = devAutotestHealingDrugs(member);
                    devAutotestLog("    hp %d/%d, ai packet %d \"%s\", min hp %d, run away mode %d, chem use %d, disposition %d, hurt too much 0x%x, maneuver 0x%x, healing drugs %d\n",
                        critterGetStat(member, STAT_CURRENT_HIT_POINTS), critterGetStat(member, STAT_MAXIMUM_HIT_POINTS),
                        ai != nullptr ? ai->packet_num : -1, ai != nullptr && ai->name != nullptr ? ai->name : "",
                        ai != nullptr ? ai->min_hp : -1, ai != nullptr ? static_cast<int>(ai->run_away_mode) : -99,
                        ai != nullptr ? static_cast<int>(ai->chem_use) : -99, ai != nullptr ? static_cast<int>(ai->disposition) : -99,
                        ai != nullptr ? static_cast<unsigned>(ai->hurt_too_much) : 0u, static_cast<unsigned>(member->data.critter.combat.maneuver), healing);
                }
            }
            break;
        case DEV_AUTOTEST_ACTION_KEEP_COMBAT: {
            Object* critter = devAutotestFindNearestEnemyInSight();
            if (critter != nullptr && isInCombat()) {
                critterSetWhoHitMe(gDude, critter);
                devAutotestLog("  the dude hit by pid %d, %d hexes away\n", critter->pid, objectGetDistanceBetween(gDude, critter));
            } else {
                devAutotestLog("FAIL: no critter in sight or not in combat\n");
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_START_COMBAT_ATTACKED: {
            Object* critter = devAutotestFindNearestEnemyInSight();
            if (critter == nullptr || isInCombat()) {
                devAutotestLog("FAIL: no critter in sight or already in combat\n");
                exit(EXIT_FAILURE);
            }
            devAutotestLog("  pid %d attacks the dude with %d bonus action points\n", critter->pid, static_cast<int>(step->a));

            static CombatStartData combatStartData;
            combatStartData = {};
            combatStartData.attacker = critter;
            combatStartData.defender = gDude;
            combatStartData.actionPointsBonus = static_cast<int>(step->a);
            combatStartData.maxDamage = 500;

            // The combat loop runs inside, the script continues from there.
            gDevAutotestPendingCapture = step->name;
            gDevAutotestStep++;
            gDevAutotestStepStarted = false;
            _combat(&combatStartData);
            return;
        }
        case DEV_AUTOTEST_ACTION_ADD_DUDE_PERK: {
            Perk perk = static_cast<Perk>(static_cast<int>(step->a));
            int rc = perkAddForce(gDude, perk);
            devAutotestLog("  dude perk %d: %s, rank %d\n", static_cast<int>(step->a), rc == 0 ? "added" : "FAIL", perkGetRank(gDude, perk));
            break;
        }
        case DEV_AUTOTEST_ACTION_SET_FREE_MOVE:
            _combat_free_move = static_cast<int>(step->a);
            interfaceRenderActionPoints(gDude->data.critter.combat.ap, _combat_free_move);
            devAutotestLog("  free move points %d\n", _combat_free_move);
            break;
        case DEV_AUTOTEST_ACTION_CHECK_FREE_MOVE: {
            bool ok = isInCombat() && _combat_free_move == static_cast<int>(step->a);
            devAutotestLog("  free move points %d (expected %d, in combat %d): %s\n", _combat_free_move, static_cast<int>(step->a), isInCombat() ? 1 : 0, ok ? "PASS" : "FAIL");
            if (!ok) {
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_USE_ITEM_ON_CRITTER: {
            Object* critter = devAutotestFindNearestEnemyInSight();
            Object* item;
            if (critter == nullptr || objectCreateWithProtoId(&item, ProtoId(static_cast<int>(step->a))) != 0) {
                devAutotestLog("FAIL: no critter or item\n");
                exit(EXIT_FAILURE);
            }
            if (itemAdd(gDude, item, 1) == 0) {
                _obj_disconnect(item, nullptr);
            }
            int rc = _action_use_an_item_on_object(gDude, critter, item);
            devAutotestLog("  dude uses pid %d on pid %d, %d hexes away: rc %d\n", static_cast<int>(step->a), critter->pid, objectGetDistanceBetween(gDude, critter), rc);
            break;
        }
        case DEV_AUTOTEST_ACTION_CHECK_NEAR_CRITTER: {
            Object* critter = devAutotestFindNearestEnemyInSight();
            int distance = critter != nullptr ? objectGetDistanceBetween(gDude, critter) : -1;
            bool ok = distance >= 0 && distance <= static_cast<int>(step->a);
            devAutotestLog("  dude %d hexes from the critter (at most %d): %s\n", distance, static_cast<int>(step->a), ok ? "PASS" : "FAIL");
            if (!ok) {
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_WALK_LIMITED: {
            Object* critter = devAutotestFindNearestEnemyInSight();
            Rotation away = critter != nullptr ? tileGetRotationTo(critter->tile, gDude->tile) : ROTATION_SE;
            int tile = tileGetTileInDirection(gDude->tile, away, static_cast<int>(step->a));
            gDevAutotestWalkStart = gDude->tile;
            reg_anim_begin(ANIMATION_REQUEST_RESERVED);
            animationRegisterMoveToTile(gDude, tile, gDude->elevation, static_cast<int>(step->b), 0);
            int rc = reg_anim_end();
            devAutotestLog("  walk from %d to %d (%d hexes), distance %d: rc %d\n", gDude->tile, tile, static_cast<int>(step->a), static_cast<int>(step->b), rc);
            break;
        }
        case DEV_AUTOTEST_ACTION_CHECK_WALKED: {
            int walked = tileDistanceBetween(gDevAutotestWalkStart, gDude->tile);
            bool ok = walked == static_cast<int>(step->a);
            devAutotestLog("  walked %d hexes (expected %d): %s\n", walked, static_cast<int>(step->a), ok ? "PASS" : "FAIL");
            if (!ok) {
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_KNOCKOUT: {
            Object* critter = devAutotestFindNearestEnemyInSight();
            if (critter == nullptr) {
                devAutotestLog("FAIL: no critter in sight\n");
                exit(EXIT_FAILURE);
            }
            Dam& results = critter->data.critter.combat.results;
            bool ok = true;
            switch (static_cast<int>(step->a)) {
            case 1:
                results |= DAM_KNOCKED_DOWN;
                _dude_standup(critter);
                ok = !animationIsBusy(critter) && (results & DAM_KNOCKED_DOWN) == DAM_NONE;
                devAutotestLog("  stand up standing: animates %d, knocked down %d: %s\n", animationIsBusy(critter) ? 1 : 0, (results & DAM_KNOCKED_DOWN) != DAM_NONE ? 1 : 0, ok ? "PASS" : "FAIL");
                break;
            case 2:
                results |= DAM_KNOCKED_OUT;
                knockoutEventProcess(critter, nullptr);
                ok = isInCombat() && (results & DAM_KNOCKOUT_WOKEN) != DAM_NONE && (results & DAM_KNOCKED_DOWN) != DAM_NONE;
                devAutotestLog("  woken in combat: results 0x%x: %s\n", static_cast<unsigned>(results), ok ? "PASS" : "FAIL");
                results &= ~(DAM_KNOCKOUT_WOKEN | DAM_KNOCKED_DOWN);
                break;
            case 3: {
                results |= DAM_KNOCKED_DOWN | DAM_LOSE_TURN;
                Rect rect;
                objectSetFrmId(critter, FrmId(critter, ANIM_FALL_BACK, critter->rotation + 1), &rect);
                tileWindowRefreshRect(&rect, critter->elevation);
                devAutotestLog("  knocked down, loses a turn: results 0x%x\n", static_cast<unsigned>(results));
                break;
            }
            case 4:
                critterSetWhoHitMe(gDude, nullptr);
                break;
            case 5:
                ok = !isInCombat() && (results & (DAM_KNOCKED_DOWN | DAM_LOSE_TURN)) == DAM_NONE;
                devAutotestLog("  after combat (in combat %d): results 0x%x, animation %d: %s\n", isInCombat() ? 1 : 0, static_cast<unsigned>(results), static_cast<int>(FrmId(critter).animationType()), ok ? "PASS" : "FAIL");
                break;
            case 6: {
                // queueFindFirstEvent returns the event's data: a marker
                // (knockout events have no data to free).
                static int marker;
                results |= DAM_KNOCKED_OUT;
                queueAddEvent(100, critter, &marker, EVENT_TYPE_KNOCKOUT);
                bool eventAdded = queueFindFirstEvent(critter, EVENT_TYPE_KNOCKOUT) == &marker;
                _queue_leaving_map();
                bool eventLeft = queueFindFirstEvent(critter, EVENT_TYPE_KNOCKOUT) != nullptr;
                ok = eventAdded && (results & DAM_KNOCKED_DOWN) != DAM_NONE && (results & DAM_KNOCKED_OUT) == DAM_NONE && !eventLeft;
                devAutotestLog("  knocked out, map left: results 0x%x, wake up event %d -> %d: %s\n", static_cast<unsigned>(results), eventAdded ? 1 : 0, eventLeft ? 1 : 0, ok ? "PASS" : "FAIL");
                results &= ~(DAM_KNOCKED_OUT | DAM_KNOCKED_DOWN);
                queueRemoveEventsByType(critter, EVENT_TYPE_KNOCKOUT);
                break;
            }
            }
            if (!ok) {
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_RETRIEVE_OBJECT: {
            static int critterId = -1;
            // A critter the NPC armor mod doesn't handle (npcarmor.ini).
            constexpr int RETRIEVE_CRITTER = 67;
            int weaponPid = static_cast<int>(step->c);
            if (step->a == 1) {
                int distance = static_cast<int>(step->b);
                int rotation = -1;
                for (int candidate = 0; candidate < ROTATION_COUNT && rotation == -1; candidate++) {
                    bool free = true;
                    for (int hex = 1; hex <= distance + 1; hex++) {
                        int tile = tileGetTileInDirection(gDude->tile, static_cast<Rotation>(candidate), hex);
                        if (_obj_blocking_at(nullptr, tile, gDude->elevation) != nullptr) {
                            free = false;
                        }
                    }
                    if (free) {
                        rotation = candidate;
                    }
                }
                Object* critter = nullptr;
                Object* weapon = nullptr;
                Object* decoy = nullptr;
                if (rotation == -1 || objectCreateWithProtoId(&critter, ProtoId(0x1000000 | RETRIEVE_CRITTER)) != 0
                    || objectCreateWithProtoId(&weapon, ProtoId(weaponPid)) != 0
                    || objectCreateWithProtoId(&decoy, ProtoId(ItemProtoTypeId::Stimpak)) != 0) {
                    devAutotestLog("FAIL: no free line or no objects\n");
                    exit(EXIT_FAILURE);
                }
                critter->sid = -1;
                critter->data.critter.combat.team = 77;
                // Nothing to fight with in its inventory.
                while (critter->data.inventory.length > 0) {
                    Object* item = critter->data.inventory.items[0].item;
                    itemRemove(critter, item, critter->data.inventory.items[0].quantity);
                }
                objectSetLocation(critter, tileGetTileInDirection(gDude->tile, static_cast<Rotation>(rotation), distance), gDude->elevation, nullptr);
                objectSetLocation(weapon, tileGetTileInDirection(gDude->tile, static_cast<Rotation>(rotation), distance + 1), gDude->elevation, nullptr);
                decoy->id = weapon->id;
                itemAdd(critter, decoy, 1);
                _obj_disconnect(decoy, nullptr);
                critterId = critter->id;
                // The AI looks for a weapon (_ai_switch_weapons) when its target
                // holds one: the dude wields a spear (the starting dude of the
                // unmodded game has no knife animations).
                Object* dudeWeapon = nullptr;
                if (objectCreateWithProtoId(&dudeWeapon, ProtoId(7)) == 0) {
                    itemAdd(gDude, dudeWeapon, 1);
                    _obj_disconnect(dudeWeapon, nullptr);
                    inventoryEquipFunc(gDude, dudeWeapon, interfaceGetCurrentHand(), false);
                }
                devAutotestLog("  unarmed critter at tile %d, weapon pid %d (id %d) on tile %d, Stimpak with that id in its inventory; the dude's weapon animation %d\n", critter->tile, weaponPid, weapon->id, weapon->tile, static_cast<int>(FrmId(gDude).weaponAnimation()));
            } else {
                Object* critter = nullptr;
                for (Object* object = objectFindFirstAtElevation(gDude->elevation); object != nullptr; object = objectFindNextAtElevation()) {
                    if (object->id == critterId && FrmId(object).objectType() == OBJ_TYPE_CRITTER) {
                        critter = object;
                    }
                }
                Object* inHand = critter != nullptr ? critterGetItem2(critter) : nullptr;
                if (critter != nullptr) {
                    for (int index = 0; index < critter->data.inventory.length; index++) {
                        Object* item = critter->data.inventory.items[index].item;
                        devAutotestLog("  inventory %d: pid %d id %d flags 0x%x\n", index, item->pid, item->id, static_cast<unsigned>(item->flags));
                    }
                }
                bool ok = inHand != nullptr && inHand->pid == weaponPid;
                devAutotestLog("  after its turn: in hand pid %d: %s\n", inHand != nullptr ? inHand->pid : -1, ok ? "PASS" : "FAIL");
                if (!ok) {
                    exit(EXIT_FAILURE);
                }
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_DROP_WORN_ARMOR: {
            int armorPid = static_cast<int>(step->a);
            // `b` - 1: the party member, else the dude.
            Object* critter = step->b == 1 ? devAutotestPartyMember() : gDude;
            Object* armor = nullptr;
            if (critter == nullptr || critterGetArmor(critter) != nullptr || objectCreateWithProtoId(&armor, ProtoId(armorPid)) != 0) {
                devAutotestLog("FAIL: no critter, it wears armor already or no armor\n");
                exit(EXIT_FAILURE);
            }
            int baseArmorClass = critterGetStat(critter, STAT_ARMOR_CLASS);
            itemAdd(critter, armor, 1);
            _obj_disconnect(armor, nullptr);
            inventoryEquip(critter, armor, HAND_RIGHT);
            if (critter == gDude) {
                // inven_wield leaves the dude's stats to the inventory screen.
                adjustCritterStatsOnArmorChange(critter, nullptr, armor);
            }
            int wornArmorClass = critterGetStat(critter, STAT_ARMOR_CLASS);
            bool worn = critterGetArmor(critter) == armor;
            // What a script's drop_obj does.
            objectDrop(critter, armor);
            int droppedArmorClass = critterGetStat(critter, STAT_ARMOR_CLASS);
            bool dropped = armor->owner == nullptr && critterGetArmor(critter) == nullptr;
            bool ok = worn && dropped && wornArmorClass > baseArmorClass && droppedArmorClass == baseArmorClass;
            devAutotestLog("  %s, armor pid %d: armor class %d, worn %d (%s), dropped %d (%s): %s\n", critter == gDude ? "the dude" : "the party member", armorPid,
                baseArmorClass, wornArmorClass, worn ? "worn" : "NOT WORN", droppedArmorClass, dropped ? "on the ground" : "NOT DROPPED", ok ? "PASS" : "FAIL");
            if (!ok) {
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_MISS_OBJECT: {
            // The global var the Elder's damage_p_proc sets to 1.
            constexpr int kElderDamageVar = 7;
            Object* object = nullptr;
            if (objectCreateWithProtoId(&object, ProtoId(ItemProtoTypeId::Dynamite)) != 0) {
                devAutotestLog("FAIL: no object\n");
                exit(EXIT_FAILURE);
            }
            object->sid = -1;
            objectSetLocation(object, tileGetTileInDirection(gDude->tile, ROTATION_SE, 1), gDude->elevation, nullptr);
            devAutotestAttachScript(object, "AHElder.int");
            if (object->sid == -1) {
                exit(EXIT_FAILURE);
            }

            int before = gameGetGlobalVar(static_cast<GameGlobalVar>(kElderDamageVar));
            Attack attack;
            attackInit(&attack, gDude, object, HIT_MODE_PUNCH, HIT_LOCATION_TORSO);
            attack.attackerFlags = DAM_NONE;
            _apply_damage(&attack, false);
            int afterMiss = gameGetGlobalVar(static_cast<GameGlobalVar>(kElderDamageVar));

            attackInit(&attack, gDude, object, HIT_MODE_PUNCH, HIT_LOCATION_TORSO);
            attack.attackerFlags = DAM_HIT;
            _apply_damage(&attack, false);
            int afterHit = gameGetGlobalVar(static_cast<GameGlobalVar>(kElderDamageVar));

            bool ok = before == 0 && afterMiss == 0 && afterHit == 1;
            devAutotestLog("  an item with AHElder.int: global var %d %d, after a miss %d, after a hit %d: %s\n", kElderDamageVar, before, afterMiss, afterHit, ok ? "PASS" : "FAIL");
            if (!ok) {
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_STEAL_PARTY_MEMBER: {
            Object* member = devAutotestPartyMember();
            if (member == nullptr) {
                devAutotestLog("FAIL: no party member\n");
                exit(EXIT_FAILURE);
            }
            devAutotestLog("  stealing from the party member: weight %d of %d\n", objectGetInventoryWeight(member), critterGetStat(member, STAT_CARRY_WEIGHT));
            // The screen's loop runs the next steps.
            gDevAutotestPendingCapture = step->name;
            gDevAutotestStep++;
            gDevAutotestStepStarted = false;
            inventoryOpenStealing(gDude, member);
            return;
        }
        case DEV_AUTOTEST_ACTION_CHECK_PARTY_WEIGHT: {
            Object* member = devAutotestPartyMember();
            int weight = member != nullptr ? objectGetInventoryWeight(member) : -1;
            int carryWeight = member != nullptr ? critterGetStat(member, STAT_CARRY_WEIGHT) : -1;
            int sledgehammers = 0;
            if (member != nullptr) {
                for (int index = 0; index < member->data.inventory.length; index++) {
                    if (member->data.inventory.items[index].item->pid == 6) {
                        sledgehammers += member->data.inventory.items[index].quantity;
                    }
                }
            }
            bool ok = member != nullptr && weight <= carryWeight;
            devAutotestLog("  the party member: weight %d of %d, sledgehammers %d: %s\n", weight, carryWeight, sledgehammers, ok ? "PASS" : "FAIL");
            if (!ok) {
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_EXPLODE_ON_LEAVE: {
            // The global var the warrior's destroy_p_proc sets to 1.
            constexpr int kWarriorDestroyVar = 7;
            static int warriorId = -1;
            if (step->a == 1) {
                // A free tile near the dude, away from Klint and the others.
                int tile = -1;
                for (int candidate = 0; candidate < ROTATION_COUNT && tile == -1; candidate++) {
                    int candidateTile = tileGetTileInDirection(gDude->tile, static_cast<Rotation>(candidate), 4);
                    if (_obj_blocking_at(nullptr, candidateTile, gDude->elevation) == nullptr) {
                        tile = candidateTile;
                    }
                }
                Object* warrior = nullptr;
                Object* dynamite = nullptr;
                if (tile == -1 || objectCreateWithProtoId(&warrior, ProtoId(0x1000000 | 67)) != 0
                    || objectCreateWithProtoId(&dynamite, ProtoId(ItemProtoTypeId::Dynamite)) != 0) {
                    devAutotestLog("FAIL: no free tile or no objects\n");
                    exit(EXIT_FAILURE);
                }
                warrior->sid = -1;
                objectSetLocation(warrior, tile, gDude->elevation, nullptr);
                devAutotestAttachScript(warrior, "ACWarior.int");
                warrior->data.critter.hp = 1;
                objectSetLocation(dynamite, warrior->tile, warrior->elevation, nullptr);
                // Lit, going off long after the dude has left.
                queueAddEvent(10000, dynamite, nullptr, EVENT_TYPE_EXPLOSION);
                warriorId = warrior->id;
                devAutotestLog("  a critter with ACWarior.int (sid %d) at tile %d with 1 HP, lit dynamite on its tile; global var %d: %d\n", warrior->sid, warrior->tile,
                    kWarriorDestroyVar, gameGetGlobalVar(static_cast<GameGlobalVar>(kWarriorDestroyVar)));
                if (warrior->sid == -1) {
                    exit(EXIT_FAILURE);
                }
            } else {
                int value = gameGetGlobalVar(static_cast<GameGlobalVar>(kWarriorDestroyVar));
                bool dead = false;
                for (Object* object = objectFindFirstAtElevation(gDude->elevation); object != nullptr; object = objectFindNextAtElevation()) {
                    if (object->id == warriorId && FrmId(object).objectType() == OBJ_TYPE_CRITTER && critterIsDead(object)) {
                        dead = true;
                    }
                }
                bool ok = dead && value == 1;
                devAutotestLog("  back on map %d: the critter %s, global var %d: %d (its destroy_p_proc ran): %s\n", static_cast<int>(gMapHeader.index),
                    dead ? "dead" : "NOT DEAD", kWarriorDestroyVar, value, ok ? "PASS" : "FAIL");
                if (!ok) {
                    exit(EXIT_FAILURE);
                }
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_CHECK_MULTIHEX_PARTY: {
            // Without the villagers: where they stand differs between the
            // game copies (behind the dude, a villager blocks the tiles).
            std::vector<Object*> others;
            for (Object* object = objectFindFirstAtElevation(gDude->elevation); object != nullptr; object = objectFindNextAtElevation()) {
                if (object != gDude && FrmId(object).objectType() == OBJ_TYPE_CRITTER && !objectIsPartyMember(object)) {
                    others.push_back(object);
                }
            }
            for (Object* object : others) {
                objectDestroy(object, nullptr);
            }
            Object* member = nullptr;
            for (Object* object : get_all_party_members_objects(false)) {
                if (object != gDude && (object->flags & OBJECT_MULTIHEX) != 0) {
                    member = object;
                }
            }
            // As on entering an elevation: the party member comes from
            // another one (here it would block the tiles itself, it walks to
            // the dude to attack him).
            if (member != nullptr) {
                objectSetLocation(member, member->tile, (gDude->elevation + 1) % ELEVATION_COUNT, nullptr);
            }
            _partyMemberSyncPosition();
            Object* blocker = nullptr;
            if (member != nullptr) {
                for (int around = 0; around < ROTATION_COUNT && blocker == nullptr; around++) {
                    blocker = _obj_blocking_at(member, tileGetTileInDirection(member->tile, static_cast<Rotation>(around), 1), member->elevation);
                }
            }
            bool ok = member != nullptr && blocker == nullptr && member->elevation == gDude->elevation;
            devAutotestLog("  map %d: the dude at tile %d, the multihex party member at tile %d (%d tiles from him), around it %s: %s\n",
                static_cast<int>(gMapHeader.index), gDude->tile, member != nullptr ? member->tile : -1,
                member != nullptr ? tileDistanceBetween(member->tile, gDude->tile) : -1,
                blocker == nullptr ? "free" : (blocker == gDude ? "the dude" : "another object"), ok ? "PASS" : "FAIL");
            if (!ok) {
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_MULTIHEX_MOVE: {
            static int critterId = -1;
            static int critterPid = -1;
            static int gunmanId = -1;
            int distance = static_cast<int>(step->b);
            bool gunman = step->c != 0;
            // The gunman's place: behind the critter on the line.
            int gunmanDistance = distance + 3;
            if (step->a == 1) {
                // The first multihex critter in the protos.
                int maxId = proto_max_id(OBJ_TYPE_CRITTER);
                for (int index = 1; index <= maxId && critterPid == -1; index++) {
                    Proto* proto;
                    if (protoGetProto(ProtoId(0x1000000 | index), &proto) == 0 && (proto->flags & OBJECT_MULTIHEX) != 0) {
                        critterPid = 0x1000000 | index;
                    }
                }
                // Only the dude and these critters: the villagers would
                // start a fight with a monster themselves.
                std::vector<Object*> others;
                for (Object* object = objectFindFirstAtElevation(gDude->elevation); object != nullptr; object = objectFindNextAtElevation()) {
                    if (object != gDude && FrmId(object).objectType() == OBJ_TYPE_CRITTER && !objectIsPartyMember(object)) {
                        others.push_back(object);
                    }
                }
                for (Object* object : others) {
                    objectDestroy(object, nullptr);
                }
                // A free line, the tiles around it too on the critter's way.
                int rotation = -1;
                for (int candidate = 0; candidate < ROTATION_COUNT && rotation == -1; candidate++) {
                    bool free = true;
                    for (int hex = 1; hex <= (gunman ? gunmanDistance : distance) + 1; hex++) {
                        int tile = tileGetTileInDirection(gDude->tile, static_cast<Rotation>(candidate), hex);
                        // The tiles around only on the critter's way.
                        int aroundCount = hex <= distance + 1 ? ROTATION_COUNT : 0;
                        for (int around = -1; around < aroundCount; around++) {
                            int checkTile = around == -1 ? tile : tileGetTileInDirection(tile, static_cast<Rotation>(around), 1);
                            if (_obj_blocking_at(gDude, checkTile, gDude->elevation) != nullptr) {
                                free = false;
                            }
                        }
                    }
                    if (free) {
                        rotation = candidate;
                    }
                }
                Object* critter = nullptr;
                if (critterPid == -1 || rotation == -1 || objectCreateWithProtoId(&critter, ProtoId(critterPid)) != 0) {
                    devAutotestLog("FAIL: no multihex critter (pid 0x%x) or no free line\n", critterPid);
                    exit(EXIT_FAILURE);
                }
                critter->sid = -1;
                critter->data.critter.combat.team = 77;
                // It fights in melee.
                while (critter->data.inventory.length > 0) {
                    Object* item = critter->data.inventory.items[0].item;
                    itemRemove(critter, item, critter->data.inventory.items[0].quantity);
                }
                objectSetLocation(critter, tileGetTileInDirection(gDude->tile, static_cast<Rotation>(rotation), distance), gDude->elevation, nullptr);
                // Both survive its attacks.
                for (Object* object : { gDude, critter }) {
                    critterSetBonusStat(object, STAT_MAXIMUM_HIT_POINTS, 500);
                    object->data.critter.hp = critterGetStat(object, STAT_MAXIMUM_HIT_POINTS);
                }
                critterId = critter->id;
                if (gunman) {
                    // A team mate with a loaded 10mm SMG (Sulik's look has its
                    // animations; a party member's pid, but the NPC armor mod
                    // only swaps unanimated weapons), rated above the critter
                    // (AC).
                    Object* mate = nullptr;
                    Object* smg = nullptr;
                    Object* ammo = nullptr;
                    if (objectCreateWithProtoId(&mate, ProtoId(0x1000000 | 97)) != 0
                        || objectCreateWithProtoId(&smg, ProtoId(9)) != 0
                        || objectCreateWithProtoId(&ammo, ProtoId(29)) != 0) {
                        devAutotestLog("FAIL: no gunman\n");
                        exit(EXIT_FAILURE);
                    }
                    mate->sid = -1;
                    mate->data.critter.combat.team = critter->data.critter.combat.team;
                    while (mate->data.inventory.length > 0) {
                        Object* item = mate->data.inventory.items[0].item;
                        itemRemove(mate, item, mate->data.inventory.items[0].quantity);
                    }
                    weaponReload(smg, ammo);
                    itemAdd(mate, smg, 1);
                    _obj_disconnect(smg, nullptr);
                    inventoryEquipFunc(mate, smg, HAND_RIGHT, false);
                    objectSetLocation(mate, tileGetTileInDirection(gDude->tile, static_cast<Rotation>(rotation), gunmanDistance), gDude->elevation, nullptr);
                    critterSetBonusStat(mate, STAT_ARMOR_CLASS, 100);
                    critterSetBonusStat(mate, STAT_MAXIMUM_HIT_POINTS, 500);
                    mate->data.critter.hp = critterGetStat(mate, STAT_MAXIMUM_HIT_POINTS);
                    // It shoots from its place.
                    aiGetPacket(mate)->distance = DISTANCE_STAY;
                    gunmanId = mate->id;
                    devAutotestLog("  gunman at tile %d, %d tiles from the dude, ammo %d, in hand pid %d\n", mate->tile, tileDistanceBetween(mate->tile, gDude->tile), ammoGetQuantity(smg),
                        critterGetItem2(mate) != nullptr ? critterGetItem2(mate)->pid : -1);
                }
                devAutotestLog("  multihex critter pid 0x%x (%s) at tile %d, %d tiles from the dude, AP %d\n", critterPid, critterGetName(critter), critter->tile,
                    tileDistanceBetween(critter->tile, gDude->tile), critterGetStat(critter, STAT_MAXIMUM_ACTION_POINTS));
            } else {
                Object* critter = nullptr;
                for (Object* object = objectFindFirstAtElevation(gDude->elevation); object != nullptr; object = objectFindNextAtElevation()) {
                    if (object->id == critterId && object->pid == critterPid) {
                        critter = object;
                    }
                }
                if (step->a == 4) {
                    Object* mate = nullptr;
                    for (Object* object = objectFindFirstAtElevation(gDude->elevation); object != nullptr; object = objectFindNextAtElevation()) {
                        if (object->id == gunmanId && FrmId(object).objectType() == OBJ_TYPE_CRITTER) {
                            mate = object;
                        }
                    }
                    if (mate == nullptr || critter == nullptr || !isInCombat()) {
                        devAutotestLog("FAIL: no gunman or not in combat\n");
                        exit(EXIT_FAILURE);
                    }
                    aiInfoSetLastTarget(mate, gDude);
                    devAutotestLog("  the dude's turn: the critter %d tiles from him, the gunman at tile %d targets him\n", tileDistanceBetween(critter->tile, gDude->tile), mate->tile);
                    break;
                }
                if (step->a == 2 && isInCombat()) {
                    // Hostile, it starts the combat itself on seeing the dude.
                    devAutotestLog("  the combat has started already (they attacked on seeing the dude)\n");
                    break;
                }
                if (step->a == 2) {
                    // Else it attacks him (START_COMBAT_ATTACKED doesn't find
                    // a multihex critter in its line of fire).
                    if (critter == nullptr) {
                        devAutotestLog("FAIL: no critter (id %d pid 0x%x)\n", critterId, critterPid);
                        exit(EXIT_FAILURE);
                    }
                    static CombatStartData combatStartData;
                    combatStartData = {};
                    combatStartData.attacker = critter;
                    combatStartData.defender = gDude;
                    combatStartData.maxDamage = 500;
                    gDevAutotestPendingCapture = step->name;
                    gDevAutotestStep++;
                    gDevAutotestStepStarted = false;
                    _combat(&combatStartData);
                    return;
                }
                int tiles = critter != nullptr ? tileDistanceBetween(critter->tile, gDude->tile) : -1;
                if (critter != nullptr) {
                    devAutotestLog("  round %d, the critter: tile %d, results 0x%x, AP %d, HP %d\n", _combatNumTurns, critter->tile,
                        static_cast<unsigned>(critter->data.critter.combat.results), critter->data.critter.combat.ap, critter->data.critter.hp);
                }
                // Its body takes the tiles around its own: one tile away is over the dude.
                bool ok = tiles >= 2 && tiles < distance;
                devAutotestLog("  after its turn: %d tiles from the dude (object distance %d): %s\n", tiles,
                    critter != nullptr ? objectGetDistanceBetween(critter, gDude) : -1, ok ? "PASS" : "FAIL");
                if (!ok) {
                    exit(EXIT_FAILURE);
                }
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_NPC_PICKUP: {
            static int critterId = -1;
            static int weaponId = -1;
            static bool messageOk = false;
            // A critter the NPC armor mod doesn't handle (npcarmor.ini).
            constexpr int PICKUP_CRITTER = 67;
            int weaponPid = static_cast<int>(step->c);
            if (step->a == 1) {
                int distance = static_cast<int>(step->b);
                int rotation = -1;
                for (int candidate = 0; candidate < ROTATION_COUNT && rotation == -1; candidate++) {
                    bool free = true;
                    for (int hex = 1; hex <= distance + 1; hex++) {
                        int tile = tileGetTileInDirection(gDude->tile, static_cast<Rotation>(candidate), hex);
                        if (_obj_blocking_at(nullptr, tile, gDude->elevation) != nullptr) {
                            free = false;
                        }
                    }
                    if (free) {
                        rotation = candidate;
                    }
                }
                Object* critter = nullptr;
                Object* weapon = nullptr;
                Object* load = nullptr;
                if (rotation == -1 || objectCreateWithProtoId(&critter, ProtoId(0x1000000 | PICKUP_CRITTER)) != 0
                    || objectCreateWithProtoId(&weapon, ProtoId(weaponPid)) != 0
                    || objectCreateWithProtoId(&load, ProtoId(ItemProtoTypeId::PowerArmor)) != 0) {
                    devAutotestLog("FAIL: no free line or no objects\n");
                    exit(EXIT_FAILURE);
                }
                critter->sid = -1;
                critter->data.critter.combat.team = 77;
                // Nothing to fight with in its inventory.
                while (critter->data.inventory.length > 0) {
                    Object* item = critter->data.inventory.items[0].item;
                    itemRemove(critter, item, critter->data.inventory.items[0].quantity);
                }
                objectSetLocation(critter, tileGetTileInDirection(gDude->tile, static_cast<Rotation>(rotation), distance), gDude->elevation, nullptr);
                objectSetLocation(weapon, tileGetTileInDirection(gDude->tile, static_cast<Rotation>(rotation), distance + 1), gDude->elevation, nullptr);
                // Armor past its carry weight.
                int carryWeight = critterGetStat(critter, STAT_CARRY_WEIGHT);
                int loadQuantity = carryWeight / std::max(itemGetWeight(load), 1) + 1;
                itemAdd(critter, load, loadQuantity);
                _obj_disconnect(load, nullptr);
                critterId = critter->id;
                weaponId = weapon->id;

                // Out of combat the pickup fails: whose message.
                objectPickup(critter, weapon);
                char expected[160];
                snprintf(expected, sizeof(expected), muiText(375, "%s cannot pick up the item."), critterGetName(critter));
                const char* message = displayMonitorGetLastMessage();
                messageOk = strcmp(message, expected) == 0 && weapon->owner == nullptr;
                devAutotestLog("  out of combat: weight %d of %d, the knife %s, message \"%s\": %s\n", objectGetInventoryWeight(critter), carryWeight,
                    weapon->owner == nullptr ? "on the ground" : "picked up", message, messageOk ? "PASS" : "FAIL");

                // The AI looks for a weapon (_ai_switch_weapons) when its target
                // holds one: the dude wields a spear (the starting dude of the
                // unmodded game has no knife animations).
                Object* dudeWeapon = nullptr;
                if (objectCreateWithProtoId(&dudeWeapon, ProtoId(7)) == 0) {
                    itemAdd(gDude, dudeWeapon, 1);
                    _obj_disconnect(dudeWeapon, nullptr);
                    inventoryEquipFunc(gDude, dudeWeapon, interfaceGetCurrentHand(), false);
                }
                devAutotestLog("  unarmed critter at tile %d, weapon pid %d on tile %d\n", critter->tile, weaponPid, weapon->tile);
            } else {
                Object* critter = nullptr;
                for (Object* object = objectFindFirstAtElevation(gDude->elevation); object != nullptr; object = objectFindNextAtElevation()) {
                    if (object->id == critterId && FrmId(object).objectType() == OBJ_TYPE_CRITTER) {
                        critter = object;
                    }
                }
                Object* picked = nullptr;
                if (critter != nullptr) {
                    for (int index = 0; index < critter->data.inventory.length; index++) {
                        Object* item = critter->data.inventory.items[index].item;
                        if (item->id == weaponId && item->pid == weaponPid) {
                            picked = item;
                        }
                    }
                }
                bool ok = picked != nullptr;
                devAutotestLog("  after its turn: weight %d of %d, the knife %s: %s\n",
                    critter != nullptr ? objectGetInventoryWeight(critter) : -1, critter != nullptr ? critterGetStat(critter, STAT_CARRY_WEIGHT) : -1,
                    ok ? "in its inventory" : "not in its inventory", ok ? "PASS" : "FAIL");
                if (!ok || !messageOk) {
                    exit(EXIT_FAILURE);
                }
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_MOVE_AWAY: {
            static int grenadierId = -1;
            static int startTile = -1;
            static int startSafeDistance = 0;
            Object* grenadier = nullptr;
            int distance = static_cast<int>(step->b);
            if (step->a == 1) {
                int rotation = -1;
                for (int candidate = 0; candidate < ROTATION_COUNT && rotation == -1; candidate++) {
                    bool free = true;
                    for (int hex = 1; hex <= distance + 3; hex++) {
                        int tile = tileGetTileInDirection(gDude->tile, static_cast<Rotation>(candidate), hex);
                        if (_obj_blocking_at(nullptr, tile, gDude->elevation) != nullptr) {
                            free = false;
                        }
                    }
                    if (free) {
                        rotation = candidate;
                    }
                }
                Object* grenade = nullptr;
                if (rotation == -1 || objectCreateWithProtoId(&grenadier, ProtoId(0x1000000 | 97)) != 0
                    || objectCreateWithProtoId(&grenade, ProtoId(static_cast<int>(step->c))) != 0) {
                    devAutotestLog("FAIL: no free line or no objects\n");
                    exit(EXIT_FAILURE);
                }
                grenadier->sid = -1;
                grenadier->data.critter.combat.team = 77;
                objectSetLocation(grenadier, tileGetTileInDirection(gDude->tile, static_cast<Rotation>(rotation), distance), gDude->elevation, nullptr);
                itemAdd(grenadier, grenade, 3);
                _obj_disconnect(grenade, nullptr);
                inventoryEquipFunc(grenadier, grenade, HAND_RIGHT, false);
                // Its distance preference neither closes in nor backs off
                // (the AI packet in memory, for this run): only the fix moves
                // it out of the blast.
                aiGetPacket(grenadier)->distance = DISTANCE_ON_YOUR_OWN;
                // Both survive the blast.
                for (Object* critter : { gDude, grenadier }) {
                    critterSetBonusStat(critter, STAT_MAXIMUM_HIT_POINTS, 500);
                    critter->data.critter.hp = critterGetStat(critter, STAT_MAXIMUM_HIT_POINTS);
                }
                grenadierId = grenadier->id;
                int safeDistance = 0;
                bool unsafe = _combat_safety_invalidate_weapon(grenadier, critterGetItem2(grenadier), HIT_MODE_RIGHT_WEAPON_PRIMARY, gDude, &safeDistance);
                startTile = grenadier->tile;
                startSafeDistance = safeDistance;
                devAutotestLog("  grenadier at tile %d, %d hexes from the dude, in hand pid %d: unsafe %d, safe distance %d\n", grenadier->tile,
                    objectGetDistanceBetween(grenadier, gDude), critterGetItem2(grenadier) != nullptr ? critterGetItem2(grenadier)->pid : -1, unsafe ? 1 : 0, safeDistance);
            } else {
                for (Object* object = objectFindFirstAtElevation(gDude->elevation); object != nullptr; object = objectFindNextAtElevation()) {
                    if (object->id == grenadierId && FrmId(object).objectType() == OBJ_TYPE_CRITTER) {
                        grenadier = object;
                    }
                }
                // The distance to move away comes negative (see
                // `_ai_move_away`), and it left its tile.
                bool ok = startSafeDistance < 0 && grenadier != nullptr && grenadier->tile != startTile;
                devAutotestLog("  after its turn: safe distance %d, tile %d -> %d, hp %d: %s\n", startSafeDistance, startTile, grenadier != nullptr ? grenadier->tile : -1,
                    grenadier != nullptr ? critterGetStat(grenadier, STAT_CURRENT_HIT_POINTS) : -1, ok ? "PASS" : "FAIL");
                if (!ok) {
                    exit(EXIT_FAILURE);
                }
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_BURST_FRIENDLY: {
            int distance = static_cast<int>(step->b);
            // A direction with free tiles from the dude.
            int rotation = -1;
            for (int candidate = 0; candidate < ROTATION_COUNT && rotation == -1; candidate++) {
                bool free = true;
                for (int hex = 1; hex <= distance; hex++) {
                    int tile = tileGetTileInDirection(gDude->tile, static_cast<Rotation>(candidate), hex);
                    if (_obj_blocking_at(nullptr, tile, gDude->elevation) != nullptr) {
                        free = false;
                    }
                }
                if (free) {
                    rotation = candidate;
                }
            }
            Object* attacker = nullptr;
            Object* friendly = nullptr;
            Object* weapon = nullptr;
            if (rotation == -1
                || objectCreateWithProtoId(&attacker, ProtoId(0x1000000 | 97)) != 0
                || objectCreateWithProtoId(&friendly, ProtoId(0x1000000 | 97)) != 0
                || objectCreateWithProtoId(&weapon, ProtoId(static_cast<int>(step->a))) != 0) {
                devAutotestLog("FAIL: no free line or no objects\n");
                exit(EXIT_FAILURE);
            }
            attacker->sid = -1;
            friendly->sid = -1;
            objectSetLocation(attacker, tileGetTileInDirection(gDude->tile, static_cast<Rotation>(rotation), distance), gDude->elevation, nullptr);
            objectSetLocation(friendly, tileGetTileInDirection(gDude->tile, static_cast<Rotation>(rotation), distance / 2), gDude->elevation, nullptr);
            critterSetBaseStat(attacker, STAT_INTELLIGENCE, 10);
            friendly->data.critter.combat.team = attacker->data.critter.combat.team;
            critterSetBaseStat(friendly, STAT_DAMAGE_RESISTANCE, 0);
            int burst = weaponGetAnimationForHitMode(weapon, HIT_MODE_RIGHT_WEAPON_SECONDARY) == ANIM_FIRE_BURST ? 1 : 0;
            bool unsafe = _combat_safety_invalidate_weapon(attacker, weapon, HIT_MODE_RIGHT_WEAPON_SECONDARY, gDude, nullptr);
            bool ok = burst == 1 && unsafe;
            devAutotestLog("  burst (%d) at the dude with a team mate (team %d, DR %d) in the line, rotation %d: unsafe %d: %s\n", burst,
                friendly->data.critter.combat.team, critterGetStat(friendly, STAT_DAMAGE_RESISTANCE), rotation, unsafe ? 1 : 0, ok ? "PASS" : "FAIL");
            objectDestroy(attacker, nullptr);
            objectDestroy(friendly, nullptr);
            objectDestroy(weapon, nullptr);
            if (!ok) {
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_WIELD_STACK: {
            int pid = static_cast<int>(step->a);
            Object* stack = nullptr;
            for (int index = 0; index < gDude->data.inventory.length; index++) {
                if (gDude->data.inventory.items[index].item->pid == pid) {
                    stack = gDude->data.inventory.items[index].item;
                }
            }
            if (stack == nullptr) {
                devAutotestLog("FAIL: no item pid %d\n", pid);
                exit(EXIT_FAILURE);
            }
            // The dude may have some already (the starting dude's spear).
            int totalBefore = 0;
            for (int index = 0; index < gDude->data.inventory.length; index++) {
                if (gDude->data.inventory.items[index].item->pid == pid) {
                    totalBefore += gDude->data.inventory.items[index].quantity;
                }
            }
            int rc = inventoryEquipFunc(gDude, stack, HAND_RIGHT, false);
            Object* inHand = critterGetItem2(gDude);

            int inHandQuantity = 0;
            int total = 0;
            int entries = 0;
            for (int index = 0; index < gDude->data.inventory.length; index++) {
                InventoryItem* inventoryItem = &(gDude->data.inventory.items[index]);
                if (inventoryItem->item->pid == pid) {
                    entries++;
                    total += inventoryItem->quantity;
                    if (inventoryItem->item == inHand) {
                        inHandQuantity = inventoryItem->quantity;
                    }
                }
            }
            bool ok = rc == 0 && inHand != nullptr && inHand->pid == pid && inHandQuantity == 1 && totalBefore >= static_cast<int>(step->b) && total == totalBefore;
            devAutotestLog("  stack of pid %d wielded (rc %d): in hand x%d, %d (were %d) in %d entries: %s\n", pid, rc, inHandQuantity, total, totalBefore, entries, ok ? "PASS" : "FAIL");
            if (!ok) {
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_DRUG_DOSES: {
            int drugPid = static_cast<int>(step->c);
            auto takeDose = [drugPid]() {
                Object* drug;
                if (objectCreateWithProtoId(&drug, ProtoId(drugPid)) == 0) {
                    drugItemTakeDrug(gDude, drug);
                    objectDestroy(drug, nullptr);
                }
            };
            auto countEffects = [drugPid]() {
                int count = 0;
                for (DrugEffectEvent* event = static_cast<DrugEffectEvent*>(queueFindFirstEvent(gDude, EVENT_TYPE_DRUG)); event != nullptr; event = static_cast<DrugEffectEvent*>(queueFindNextEvent(gDude, EVENT_TYPE_DRUG))) {
                    if (event->drugPid == drugPid) {
                        count++;
                    }
                }
                return count;
            };
            if (step->a == 1) {
                int strength = critterGetStat(gDude, STAT_STRENGTH);
                for (int dose = 0; dose < static_cast<int>(step->b); dose++) {
                    takeDose();
                }
                devAutotestLog("  %d doses of pid %d: Strength %d -> %d, effects %d\n", static_cast<int>(step->b), drugPid, strength, critterGetStat(gDude, STAT_STRENGTH), countEffects());
            } else {
                int effects = countEffects();
                int strength = critterGetStat(gDude, STAT_STRENGTH);
                takeDose();
                int strengthAfter = critterGetStat(gDude, STAT_STRENGTH);
                bool ok = effects == static_cast<int>(step->b) && strengthAfter == strength;
                devAutotestLog("  after loading: effects of pid %d %d; one more dose: Strength %d -> %d: %s\n", drugPid, effects, strength, strengthAfter, ok ? "PASS" : "FAIL");
                if (!ok) {
                    exit(EXIT_FAILURE);
                }
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_BARTER_SALE: {
            auto countPid = [](Object* owner, int pid) {
                int quantity = 0;
                if (owner != nullptr) {
                    for (int index = 0; index < owner->data.inventory.length; index++) {
                        InventoryItem* inventoryItem = &(owner->data.inventory.items[index]);
                        if (inventoryItem->item->pid == pid) {
                            quantity += inventoryItem->quantity;
                        }
                    }
                }
                return quantity;
            };
            int pidB = static_cast<int>(step->b);
            int pidC = static_cast<int>(step->c);
            if (step->a == 1) {
                Object* item;
                if (objectCreateWithProtoId(&item, ProtoId(pidB)) != 0 || itemAdd(gDude, item, 1) != 0) {
                    devAutotestLog("FAIL: no item pid %d\n", pidB);
                    exit(EXIT_FAILURE);
                }
                _obj_disconnect(item, nullptr);
                if (step->c >= 1) {
                    miscItemTurnOn(item);
                }
                if (step->c >= 2) {
                    miscItemTurnOff(item);
                }
                devAutotestLog("  dude's item pid %d (turned %s): now pid %d, queued %d\n", pidB, step->c == 0 ? "never on" : (step->c == 1 ? "on" : "on and off"), item->pid, itemIsQueued(item) ? 1 : 0);
            } else if (step->a == 2) {
                int added = 0;
                while (objectGetInventoryWeight(gDude) <= critterGetStat(gDude, STAT_CARRY_WEIGHT) && added < 100) {
                    Object* item;
                    if (objectCreateWithProtoId(&item, ProtoId(pidB)) != 0 || itemAdd(gDude, item, 1) != 0) {
                        break;
                    }
                    _obj_disconnect(item, nullptr);
                    added++;
                }
                devAutotestLog("  dude overloaded with %d x pid %d: weight %d of %d\n", added, pidB, objectGetInventoryWeight(gDude), critterGetStat(gDude, STAT_CARRY_WEIGHT));
            } else {
                Object* partner = gGameDialogSpeaker;
                int partnerB = countPid(partner, pidB);
                int partnerC = pidC != 0 ? countPid(partner, pidC) : 1;
                int dudeB = countPid(gDude, pidB);
                int dudeC = pidC != 0 ? countPid(gDude, pidC) : 0;
                bool ok = partner != nullptr && partnerB > 0 && partnerC > 0 && dudeB == 0 && dudeC == 0;
                devAutotestLog("  sold: partner pid %d x%d, pid %d x%d; dude x%d, x%d: %s\n", pidB, partnerB, pidC, pidC != 0 ? partnerC : 0, dudeB, dudeC, ok ? "PASS" : "FAIL");
                if (!ok) {
                    exit(EXIT_FAILURE);
                }
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_ORIGINAL_675: {
            static int bodyId = -1;
            static int bodyPid = -1;
            static int bodyTile = -1;
            bool ok = true;
            switch (static_cast<int>(step->a)) {
            case 1: {
                Proto proto;
                memset(&proto, 0, sizeof(proto));
                proto_critter_init(&proto, ProtoId(0x1000000 | 1));
                ok = proto.critter.team == 1;
                devAutotestLog("  new critter proto: team %d: %s\n", proto.critter.team, ok ? "PASS" : "FAIL");
                break;
            }
            case 2: {
                Object* member = devAutotestPartyMember();
                int east = tileGetTileInDirection(gDude->tile, ROTATION_E, 1);
                int northEast = tileGetTileInDirection(gDude->tile, ROTATION_NE, 1);
                if (member == nullptr || !wmEvalTileNumForPlacement(east) || !wmEvalTileNumForPlacement(northEast)) {
                    devAutotestLog("FAIL: no party member or the tiles east/north-east of the dude aren't free\n");
                    exit(EXIT_FAILURE);
                }
                objectAttemptPlacementPartyMember(member, gDude->tile, gElevation);
                ok = member->tile == east;
                devAutotestLog("  party member placed at the dude's tile %d: tile %d (east %d, north-east %d): %s\n", gDude->tile, member->tile, east, northEast, ok ? "PASS" : "FAIL");
                break;
            }
            case 3: {
                Object* critter = nullptr;
                int bestDistance = 0;
                for (Object* object = objectFindFirstAtElevation(gElevation); object != nullptr; object = objectFindNextAtElevation()) {
                    if (object == gDude || FrmId(object).objectType() != OBJ_TYPE_CRITTER || critterIsDead(object) || objectIsPartyMember(object)
                        || critterGetKillType(object) == KILL_TYPE_ROBOT || critterFlagCheck(ProtoId(object), CRITTER_NO_AGE)) {
                        continue;
                    }
                    int distance = objectGetDistanceBetween(gDude, object);
                    if (critter == nullptr || distance < bestDistance) {
                        critter = object;
                        bestDistance = distance;
                    }
                }
                if (critter == nullptr) {
                    devAutotestLog("FAIL: no critter\n");
                    exit(EXIT_FAILURE);
                }
                bodyId = critter->id;
                bodyPid = critter->pid;
                bodyTile = critter->tile;
                critterKill(critter, ANIM_INVALID, true);
                devAutotestLog("  killed critter pid %d (id %d) at tile %d, map %d, dead bodies age %d\n", bodyPid, bodyId, bodyTile, static_cast<int>(gMapHeader.index), wmMapDeadBodiesAge() ? 1 : 0);
                break;
            }
            default: {
                bool body = false;
                bool blood = false;
                for (Object* object = objectFindFirst(); object != nullptr; object = objectFindNext()) {
                    if (object->id == bodyId && object->pid == bodyPid) {
                        body = true;
                    }
                    if (object->tile == bodyTile && FrmId(object).objectType() == OBJ_TYPE_MISC && FrmId(object) == MiscFrameId::BloodPool) {
                        blood = true;
                    }
                }
                ok = step->a == 4 ? !body && blood : !body && !blood;
                devAutotestLog("  back on map %d: body %d, blood stain %d: %s\n", static_cast<int>(gMapHeader.index), body ? 1 : 0, blood ? 1 : 0, ok ? "PASS" : "FAIL");
                break;
            }
            }
            if (!ok) {
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_MAP_AGE: {
            static int critterId = -1;
            static int critterPid = -1;
            if (step->a == 1) {
                Object* critter = nullptr;
                int bestDistance = 0;
                for (Object* object = objectFindFirstAtElevation(gElevation); object != nullptr; object = objectFindNextAtElevation()) {
                    if (object == gDude || FrmId(object).objectType() != OBJ_TYPE_CRITTER || critterIsDead(object) || objectIsPartyMember(object)
                        || critterGetKillType(object) == KILL_TYPE_ROBOT || critterFlagCheck(ProtoId(object), CRITTER_NO_HEAL)) {
                        continue;
                    }
                    int distance = objectGetDistanceBetween(gDude, object);
                    if (critter == nullptr || distance < bestDistance) {
                        critter = object;
                        bestDistance = distance;
                    }
                }
                if (critter == nullptr) {
                    devAutotestLog("FAIL: no critter\n");
                    exit(EXIT_FAILURE);
                }
                // Without its script: critter scripts may heal it themselves
                // (critter_heal).
                if (critter->sid != -1) {
                    scriptRemove(critter->sid);
                    critter->sid = -1;
                }
                critterId = critter->id;
                critterPid = critter->pid;
                int hp = critter->data.critter.hp;
                critter->data.critter.hp = 1;
                devAutotestLog("  critter pid %d (id %d, script removed) from %d to 1/%d hit points, map %d, dead bodies age %d\n", critter->pid, critterId,
                    hp, critterGetStat(critter, STAT_MAXIMUM_HIT_POINTS), static_cast<int>(gMapHeader.index), wmMapDeadBodiesAge() ? 1 : 0);
            } else if (step->a == 2) {
                gameTimeAddTicks(static_cast<int>(step->b) * GAME_TIME_TICKS_PER_HOUR);
                devAutotestLog("  %d hours passed\n", static_cast<int>(step->b));
            } else {
                Object* critter = nullptr;
                for (Object* object = objectFindFirst(); object != nullptr; object = objectFindNext()) {
                    if (object->pid == critterPid) {
                        devAutotestLog("  pid %d: id %d, hp %d, tile %d, script %d\n", object->pid, object->id, object->data.critter.hp, object->tile, object->sid);
                    }
                    if (object->id == critterId && object->pid == critterPid) {
                        critter = object;
                    }
                }
                bool ok = critter != nullptr && critter->data.critter.hp > 1;
                devAutotestLog("  critter id %d back on map %d: %d/%d hit points (pid %d, tile %d): %s\n", critterId, static_cast<int>(gMapHeader.index),
                    critter != nullptr ? critter->data.critter.hp : -1, critter != nullptr ? critterGetStat(critter, STAT_MAXIMUM_HIT_POINTS) : -1,
                    critter != nullptr ? critter->pid : -1, critter != nullptr ? critter->tile : -1, ok ? "PASS" : "FAIL");
                if (!ok) {
                    exit(EXIT_FAILURE);
                }
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_PARTY_CORPSE: {
            static int corpsePid = -1;
            if (step->a == 1) {
                Object* member = devAutotestPartyMember();
                if (member == nullptr) {
                    devAutotestLog("FAIL: no party member\n");
                    exit(EXIT_FAILURE);
                }
                corpsePid = member->pid;
                critterKill(member, ANIM_INVALID, true);
                gDevAutotestPartyMember = nullptr;
                devAutotestLog("  killed party member pid %d: script %d, in the party %d\n", corpsePid, member->sid, objectIsPartyMember(member) ? 1 : 0);
            } else {
                Object* corpse = nullptr;
                for (Object* obj = objectFindFirst(); obj != nullptr; obj = objectFindNext()) {
                    if (obj->pid == corpsePid && critterIsDead(obj)) {
                        corpse = obj;
                        break;
                    }
                }
                bool ok = corpse != nullptr;
                devAutotestLog("  party member's corpse (pid %d) after loading: %s: %s\n", corpsePid, ok ? "on the map" : "gone", ok ? "PASS" : "FAIL");
                if (!ok) {
                    exit(EXIT_FAILURE);
                }
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_CHECK_DUDE_AP: {
            int ap = gDude->data.critter.combat.ap;
            int maximum = critterGetStat(gDude, STAT_MAXIMUM_ACTION_POINTS);
            bool ok = isInCombat() && ap == maximum;
            devAutotestLog("  dude action points %d (maximum %d, in combat %d): %s\n", ap, maximum, isInCombat() ? 1 : 0, ok ? "PASS" : "FAIL");
            if (!ok) {
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_CHECK_CRITICAL: {
            int code = static_cast<int>(step->a);
            KillType killType = static_cast<KillType>(code / 100);
            HitLocation hitLocation = static_cast<HitLocation>(code / 10 % 10);
            CriticalEffect effect = static_cast<CriticalEffect>(code % 10);
            CriticalHitDataMember member = static_cast<CriticalHitDataMember>(static_cast<int>(step->b));
            int value = criticalsGetValue(killType, hitLocation, effect, member);
            bool ok = value == static_cast<int>(step->c);
            devAutotestLog("  critical kill type %d, location %d, effect %d, member %d: %d (expected %d): %s\n",
                killType, hitLocation, effect, member, value, static_cast<int>(step->c), ok ? "PASS" : "FAIL");
            if (!ok) {
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_SET_PARTY_HP: {
            if (step->b != 0) {
                gDevAutotestPartyMember = nullptr;
                for (Object* candidate : get_all_party_members_objects(false)) {
                    if (candidate->pid == (0x1000000 | static_cast<int>(step->b))) {
                        gDevAutotestPartyMember = candidate;
                    }
                }
            }
            Object* member = devAutotestPartyMember();
            if (member == nullptr) {
                devAutotestLog("FAIL: no party member\n");
                exit(EXIT_FAILURE);
            }
            member->data.critter.hp = static_cast<int>(step->a);
            gDevAutotestPartyHealing = devAutotestHealingDrugs(member);
            gDevAutotestPartyHitPoints = critterGetStat(member, STAT_CURRENT_HIT_POINTS);
            devAutotestLog("  party member pid %d hp %d, healing drugs %d\n", member->pid, gDevAutotestPartyHitPoints, gDevAutotestPartyHealing);
            break;
        }
        case DEV_AUTOTEST_ACTION_CHECK_PARTY_HEALED: {
            Object* member = devAutotestPartyMember();
            AiPacket* ai = member != nullptr ? aiGetPacket(member) : nullptr;
            if (ai == nullptr) {
                devAutotestLog("FAIL: no party member\n");
                exit(EXIT_FAILURE);
            }
            int healing = devAutotestHealingDrugs(member);
            int hp = critterGetStat(member, STAT_CURRENT_HIT_POINTS);
            bool fleeing = (member->data.critter.combat.maneuver & CRITTER_MANUEVER_FLEEING) != CRITTER_MANEUVER_NONE;
            bool ok = healing < gDevAutotestPartyHealing && hp > gDevAutotestPartyHitPoints && hp >= ai->min_hp && !fleeing;
            devAutotestLog("  party member healing drugs %d (were %d), hp %d (was %d, runs away below %d), %s: %s\n",
                healing, gDevAutotestPartyHealing, hp, gDevAutotestPartyHitPoints, ai->min_hp, fleeing ? "fleeing" : "not fleeing", ok ? "PASS" : "FAIL");
            if (!ok) {
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_QUICK_SAVE:
            if (lsgSaveGame(LOAD_SAVE_MODE_QUICK) != 1) {
                devAutotestLog("FAIL: quick save\n");
                exit(EXIT_FAILURE);
            }
            break;
        case DEV_AUTOTEST_ACTION_QUICK_LOAD:
            if (lsgLoadGame(LOAD_SAVE_MODE_QUICK) != 1) {
                devAutotestLog("FAIL: quick load\n");
                exit(EXIT_FAILURE);
            }
            break;
        case DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION: {
            int actual = lsgSessionLoadSlot();
            const char* name = critterGetName(gDude);
            bool ok = actual == static_cast<int>(step->a)
                && (step->widget == nullptr || strcmp(name, step->widget) == 0);
            devAutotestLog("  session target=%d expected=%d name=%s: %s\n", actual, static_cast<int>(step->a), name, ok ? "PASS" : "FAIL");
            if (!ok) exit(EXIT_FAILURE);
            break;
        }
        case DEV_AUTOTEST_ACTION_SAVE_GAME: {
            int rc = lsgSaveGameToSlot(static_cast<int>(step->a), step->widget != nullptr ? step->widget : "TEST");
            devAutotestLog("  saved to slot %d: %s\n", static_cast<int>(step->a) + 1, rc != -1 ? "ok" : "FAIL");
            break;
        }
        case DEV_AUTOTEST_ACTION_FLOAT_TEXT: {
            // Centered on dude, so the text is on screen. [a] 1 - the color
            // of scripts' `float_msg` and [widget]'s text.
            tileSetCenter(gDude->tile, TILE_SET_CENTER_REFRESH_WINDOW);
            char text[256] = "Floating text test";
            if (step->widget != nullptr) {
                snprintf(text, sizeof(text), "%s", step->widget);
            }
            Rect rect;
            int rc = textObjectAdd(gDude, text, 101, step->a == 1 ? COLOR_LIGHT_YELLOW : COLOR_GREEN, COLOR_BLACK, &rect);
            if (rc == 0) {
                tileWindowRefreshRect(&rect, gElevation);
            }
            devAutotestLog("  floating text over dude: rc %d, rect (%d, %d) - (%d, %d)\n", rc, rect.left, rect.top, rect.right, rect.bottom);
            break;
        }
        case DEV_AUTOTEST_ACTION_KILL_CRITTER:
            gDevAutotestCorpse = devAutotestFindNearestCritter();
            if (gDevAutotestCorpse != nullptr) {
                critterKill(gDevAutotestCorpse, ANIM_INVALID, true);
                devAutotestLog("  killed critter at tile %d\n", gDevAutotestCorpse->tile);
            }
            break;
        case DEV_AUTOTEST_ACTION_GIVE_CORPSE_ITEM: {
            Object* item;
            if (gDevAutotestCorpse != nullptr && objectCreateWithProtoId(&item, ProtoId(static_cast<int>(step->a))) == 0) {
                itemAdd(gDevAutotestCorpse, item, 1);
                devAutotestLog("  gave corpse item pid %d\n", static_cast<int>(step->a));
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_GIVE_CRITTER_ITEM: {
            Object* critter = devAutotestFindNearestCritter();
            Object* item;
            if (critter != nullptr && objectCreateWithProtoId(&item, ProtoId(static_cast<int>(step->a))) == 0) {
                int quantity = step->b > 0 ? static_cast<int>(step->b) : 1;
                if (step->c > 0 && itemGetType(item) == ITEM_TYPE_AMMO) {
                    ammoSetQuantity(item, static_cast<int>(step->c));
                }
                if (itemAdd(critter, item, quantity) == 0) {
                    _obj_disconnect(item, nullptr);
                }
                devAutotestLog("  gave critter pid %d item pid %d x%d\n", critter->pid, static_cast<int>(step->a), quantity);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_CENTER_CORPSE:
            if (gDevAutotestCorpse != nullptr) {
                tileSetCenter(gDevAutotestCorpse->tile, TILE_SET_CENTER_REFRESH_WINDOW);
            }
            break;
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_CORPSE:
            if (gDevAutotestCorpse != nullptr) {
                float x;
                float y;
                devAutotestGetObjectScreenPosition(gDevAutotestCorpse, &x, &y);
                devAutotestLog("  tap corpse at tile %d\n", gDevAutotestCorpse->tile);
                devAutotestStartTouch(x, y, x, y, 1, 0);
            }
            break;
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_BEHIND_CRITTER: {
            Object* critter = devAutotestFindNearestCritter();
            bool covered = step->b != 0;
            bool tapped = false;
            int footX = 0;
            int footY = 0;
            if (critter != nullptr) {
                tileToScreenXY(critter->tile, &footX, &footY);
                footX += 16;
            }
            for (Rotation rotation : { ROTATION_NE, ROTATION_NW }) {
                if (critter == nullptr || tapped) {
                    break;
                }
                int tile = tileGetTileInDirection(critter->tile, rotation, 1);
                if (_obj_blocking_at(nullptr, tile, gElevation) != nullptr) {
                    continue;
                }

                int tileX;
                int tileY;
                tileToScreenXY(tile, &tileX, &tileY);
                // A point of the tile (world pixels) with or without the
                // critter's sprite over it.
                for (int y = tileY + 2; y <= tileY + 14 && !tapped; y += 2) {
                    for (int x = tileX + 4; x <= tileX + 28 && !tapped; x += 2) {
                        if (tileFromScreenXY(x, y) != tile) {
                            continue;
                        }
                        bool overCritter = playerObjectAt(x, y, OBJ_TYPE_INVALID, true, gElevation) == critter;
                        if (overCritter != covered || (!covered && std::abs(x - footX) <= 14)) {
                            continue;
                        }
                        int screenX;
                        int screenY;
                        worldViewWorldToScreen(x, y, &screenX, &screenY);
                        if (covered) {
                            gDevAutotestTouchStartTile = gDude->tile;
                        }
                        devAutotestLog("  tap tile %d (%s of critter at %d) where %s\n", tile, rotation == ROTATION_NE ? "NE" : "NW", critter->tile,
                            covered ? "the critter covers it" : "it shows");
                        devAutotestStartTouch(static_cast<float>(screenX), static_cast<float>(screenY), static_cast<float>(screenX), static_cast<float>(screenY), 1, 0);
                        tapped = true;
                    }
                }
            }
            if (!tapped) {
                devAutotestLog("FAIL: no free tile behind the critter %s\n", covered ? "under its sprite" : "showing");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_CRITTER_TILE: {
            Object* critter = devAutotestFindNearestCritter();
            if (critter != nullptr) {
                float x;
                float y;
                devAutotestGetTileScreenPosition(critter->tile, &x, &y);
                int hold = step->b > 0 ? static_cast<int>(step->b) : 1;
                devAutotestLog("  %s the tile %d of the critter\n", step->b > 0 ? "long press" : "tap", critter->tile);
                devAutotestStartTouch(x, y, x, y, hold, 0);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_CHECK_TACTICAL: {
            bool shown = tacticalViewIsShown();
            const TacticalViewReach& reach = tacticalViewGetReach();
            // `b` 1 - also on (switched on, whatever shows).
            bool ok = shown == (step->a != 0) && (step->b == 0 || tacticalViewIsOn());
            devAutotestLog("  tactical view: on %d, shown %d, walk %d tiles: %s\n", tacticalViewIsOn() ? 1 : 0, shown ? 1 : 0,
                static_cast<int>(reach.reachable.size()), ok ? "PASS" : "FAIL");
            break;
        }
        case DEV_AUTOTEST_ACTION_CHECK_SELECTION: {
            int action;
            int tile;
            Object* target;
            touchControlsGetSelection(&action, &tile, &target);
            devAutotestLog("  selection: action %d, tile %d, target %s: %s\n", action, tile, target != nullptr ? "yes" : "no",
                action == static_cast<int>(step->a) ? "PASS" : "FAIL");
            break;
        }
        case DEV_AUTOTEST_ACTION_CENTER_CRITTER: {
            Object* critter = devAutotestFindNearestCritter();
            if (critter != nullptr) {
                tileSetCenter(critter->tile, TILE_SET_CENTER_REFRESH_WINDOW);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_AWAY_FROM_CRITTER: {
            Object* critter = devAutotestFindNearestCritter();
            if (critter != nullptr) {
                Rotation rotation = static_cast<Rotation>((tileGetRotationTo(gDude->tile, critter->tile) + 3) % ROTATION_COUNT);
                gDevAutotestWalkTarget = tileGetTileInDirection(gDude->tile, rotation, 1);
                float x;
                float y;
                devAutotestGetTileScreenPosition(gDevAutotestWalkTarget, &x, &y);
                devAutotestLog("  tap tile %d away from critter\n", gDevAutotestWalkTarget);
                devAutotestStartTouch(x, y, x, y, 1, 0);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_OPEN_WORLDMAP:
            // World map loop runs inside, the script continues from there.
            gDevAutotestPendingCapture = step->name;
            gDevAutotestStep++;
            gDevAutotestStepStarted = false;
            wmWorldMap();
            return;
        case DEV_AUTOTEST_ACTION_LOG_WORLDMAP: {
            int offsetX;
            int offsetY;
            int partyX;
            int partyY;
            bool isWalking;
            Rect viewRect;
            wmGetInterfaceState(&offsetX, &offsetY, &partyX, &partyY, &isWalking, &viewRect);
            devAutotestLog("  world map offset (%d, %d), party (%d, %d), walking %d\n", offsetX, offsetY, partyX, partyY, isWalking ? 1 : 0);
            break;
        }
        case DEV_AUTOTEST_ACTION_WORLDMAP_DRAG:
        case DEV_AUTOTEST_ACTION_WORLDMAP_TAP: {
            int offsetX;
            int offsetY;
            int partyX;
            int partyY;
            bool isWalking;
            Rect viewRect;
            wmGetInterfaceState(&offsetX, &offsetY, &partyX, &partyY, &isWalking, &viewRect);
            if (step->action == DEV_AUTOTEST_ACTION_WORLDMAP_DRAG) {
                float x = static_cast<float>((viewRect.left + viewRect.right) / 2);
                float y = static_cast<float>((viewRect.top + viewRect.bottom) / 2);
                devAutotestStartTouch(x, y, x + step->a, y + step->b, 0, 15);
            } else {
                float x = static_cast<float>(viewRect.left + partyX - offsetX) + step->a;
                float y = static_cast<float>(viewRect.top + partyY - offsetY) + step->b;
                devAutotestStartTouch(x, y, x, y, 1, 0);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_EQUIP_ITEM: {
            Object* item;
            if (objectCreateWithProtoId(&item, ProtoId(static_cast<int>(step->a))) == 0) {
                // Off the map once carried, as `DEV_AUTOTEST_ACTION_GIVE_ITEM`.
                if (itemAdd(gDude, item, 1) == 0) {
                    _obj_disconnect(item, nullptr);
                }
                Object* oldArmor = critterGetArmor(gDude);
                int rc = inventoryEquipFunc(gDude, item, static_cast<Hand>(static_cast<int>(step->b)), false);
                // The dude's stats and look follow armor through the
                // inventory screen (and an animation): at once here.
                if (rc == 0 && itemGetType(item) == ITEM_TYPE_ARMOR) {
                    adjustCritterStatsOnArmorChange(gDude, oldArmor, item);
                }
                // The dude's look follows armor through an animation (the
                // inventory screen's), none here: at once.
                if (rc == 0 && itemGetType(item) == ITEM_TYPE_ARMOR) {
                    CritterFrameId look = critterGetStat(gDude, STAT_GENDER) == GENDER_FEMALE ? armorGetFemaleFrameId(item) : armorGetMaleFrameId(item);
                    Rect rect;
                    objectSetFrmId(gDude, CritterFrmId(look, ANIM_STAND, FrmId(gDude).weaponAnimation(), gDude->rotation + 1), &rect);
                    tileWindowRefreshRect(&rect, gDude->elevation);
                }
                interfaceUpdateItems(false, INTERFACE_ITEM_ACTION_DEFAULT, INTERFACE_ITEM_ACTION_DEFAULT);
                devAutotestLog("  equipped pid %d in hand %d: rc %d\n", static_cast<int>(step->a), static_cast<int>(step->b), rc);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_MARKER: {
            int tile = mapHintsGetDestination();
            if (tile == -1) {
                devAutotestLog("  no destination marker\n");
                break;
            }
            float x;
            float y;
            devAutotestGetTileScreenPosition(tile, &x, &y);
            devAutotestLog("  tap near marker tile %d\n", tile);
            devAutotestStartTouch(x + step->a, y + step->b, x + step->a, y + step->b, 1, 0);
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_MOVEMENT:
            devAutotestLog("  dude tile %d, marker tile %d, animation %d (walk %d, run %d)\n",
                gDude->tile,
                mapHintsGetDestination(),
                FrmId(gDude->fid).animationType(),
                ANIM_WALK,
                ANIM_RUNNING);
            break;
        case DEV_AUTOTEST_ACTION_PLACE_ITEM: {
            Object* item;
            if (objectCreateWithProtoId(&item, ProtoId(static_cast<int>(step->a))) == 0) {
                int tile = tileGetTileInDirection(gDude->tile, static_cast<Rotation>(static_cast<int>(step->c)), static_cast<int>(step->b));
                Rect rect;
                objectSetLocation(item, tile, gDude->elevation, &rect);
                tileWindowRefreshRect(&rect, gDude->elevation);
                gDevAutotestPlacedItem = item;
                devAutotestLog("  placed pid %d at tile %d\n", static_cast<int>(step->a), tile);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_ITEM:
            if (gDevAutotestPlacedItem != nullptr) {
                Rect worldRect;
                objectGetRect(gDevAutotestPlacedItem, &worldRect);
                // c = 1 - from the box center, otherwise from bottom right.
                int x;
                int y;
                if (step->c == 1) {
                    worldViewWorldToScreen((worldRect.left + worldRect.right) / 2, (worldRect.top + worldRect.bottom) / 2, &x, &y);
                } else {
                    worldViewWorldToScreen(worldRect.right, worldRect.bottom, &x, &y);
                }
                devAutotestLog("  tap near item at (%d, %d) + (%.0f, %.0f)\n", x, y, step->a, step->b);
                devAutotestStartTouch(x + step->a, y + step->b, x + step->a, y + step->b, 1, 0);
            }
            break;
        case DEV_AUTOTEST_ACTION_MUI_TAP: {
            float x;
            float y;
            if (step->widget != nullptr && muiGetWidgetCenter(step->widget, &x, &y)) {
                x *= screenGetWidth();
                y *= screenGetHeight();
                devAutotestLog("  mui %s at (%.0f, %.0f)\n", step->widget, x, y);
                devAutotestStartTouch(x, y, x, y, step->a == 1 ? -1 : 1, 0);
            } else {
                devAutotestLog("  mui %s: NOT FOUND\n", step->widget != nullptr ? step->widget : "?");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_MUI_LONG_PRESS: {
            float x;
            float y;
            if (step->widget != nullptr && muiGetWidgetCenter(step->widget, &x, &y)) {
                static std::string heldName;
                heldName = std::string(step->name) + "_held";
                int hold = static_cast<int>(step->a);
                x *= screenGetWidth();
                y *= screenGetHeight();
                devAutotestLog("  mui long press %s at (%.0f, %.0f)\n", step->widget, x, y);
                devAutotestStartTouch(x, y, x, y, hold, 0, hold - 2, heldName.c_str());
            } else {
                devAutotestLog("  mui %s: NOT FOUND\n", step->widget != nullptr ? step->widget : "?");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_MUI_DRAG: {
            float x;
            float y;
            float targetX;
            float targetY;
            if (step->widget != nullptr && step->target != nullptr
                && muiGetWidgetCenter(step->widget, &x, &y) && muiGetWidgetCenter(step->target, &targetX, &targetY)) {
                float width = static_cast<float>(screenGetWidth());
                float height = static_cast<float>(screenGetHeight());
                devAutotestLog("  mui drag %s -> %s\n", step->widget, step->target);
                // `c` - capture at this frame of the drag too ("<name>_drag").
                static std::string captureName;
                captureName = std::string(step->name) + "_drag";
                int captureFrame = step->c > 0 ? static_cast<int>(step->c) : -1;
                devAutotestStartTouch(x * width, y * height, targetX * width, targetY * height, 0, 25, captureFrame, captureName.c_str());
            } else {
                devAutotestLog("  mui drag %s -> %s: NOT FOUND\n", step->widget != nullptr ? step->widget : "?", step->target != nullptr ? step->target : "?");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_PLACED_ITEM:
            if (gDevAutotestPlacedItem != nullptr) {
                // Picked item may be merged into a stack and freed, look
                // for it on the map instead of reading it.
                bool onGround = false;
                for (Object* object = objectFindFirstAtElevation(gElevation); object != nullptr; object = objectFindNextAtElevation()) {
                    if (object == gDevAutotestPlacedItem) {
                        onGround = true;
                    }
                }
                devAutotestLog("  placed item %s\n", onGround ? "on the ground" : "PICKED UP");
            }
            break;
        case DEV_AUTOTEST_ACTION_LOG_SLOT: {
            char path[64];
            snprintf(path, sizeof(path), "SAVEGAME\\SLOT%.2d\\SAVE.DAT", static_cast<int>(step->a));
            int size;
            devAutotestLog("  slot %d has save: %d\n", static_cast<int>(step->a), dbGetFileSize(path, &size) == 0 ? 1 : 0);
            break;
        }
        case DEV_AUTOTEST_ACTION_HUD_LONG_PRESS: {
            int x;
            int y;
            if (touchHudGetElementScreenCenter(static_cast<HudElementId>(static_cast<int>(step->a)), &x, &y)) {
                static std::string heldName;
                heldName = std::string(step->name) + "_held";
                int hold = static_cast<int>(step->b);
                devAutotestLog("  hud long press %d at (%d, %d)\n", static_cast<int>(step->a), x, y);
                devAutotestStartTouch(static_cast<float>(x), static_cast<float>(y), static_cast<float>(x), static_cast<float>(y), hold, 0, hold - 2, heldName.c_str());
            } else {
                devAutotestLog("  hud element %d: NOT FOUND\n", static_cast<int>(step->a));
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_HUD_TAP:
        case DEV_AUTOTEST_ACTION_HUD_TAP_MODE:
        case DEV_AUTOTEST_ACTION_HUD_QUICK_TAP_MODE: {
            int x;
            int y;
            bool found = step->action == DEV_AUTOTEST_ACTION_HUD_TAP
                ? touchHudGetElementScreenCenter(static_cast<HudElementId>(static_cast<int>(step->a)), &x, &y)
                : touchHudGetModeScreenCenter(static_cast<int>(step->a), &x, &y);
            if (found && step->action == DEV_AUTOTEST_ACTION_HUD_QUICK_TAP_MODE) {
                // Last chip, right part (where the finger lands at the edge).
                x += static_cast<int>(step->b);
            }
            if (found) {
                devAutotestLog("  hud %s %d at (%d, %d)\n", step->action == DEV_AUTOTEST_ACTION_HUD_TAP ? "element" : "mode", static_cast<int>(step->a), x, y);
                int hold = step->action == DEV_AUTOTEST_ACTION_HUD_QUICK_TAP_MODE ? -1 : 1;
                devAutotestStartTouch(static_cast<float>(x), static_cast<float>(y), static_cast<float>(x), static_cast<float>(y), hold, 0);
            } else {
                devAutotestLog("  hud %d: NOT FOUND\n", static_cast<int>(step->a));
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_HIT_MODE: {
            HitMode hitMode;
            bool aiming;
            interfaceGetCurrentHitMode(&hitMode, &aiming);
            devAutotestLog("  hit mode %d, aiming %d\n", hitMode, aiming ? 1 : 0);
            break;
        }
        case DEV_AUTOTEST_ACTION_CHECK_MODS:
            devAutotestCheckMods();
            break;
        case DEV_AUTOTEST_ACTION_CHECK_LOADED_WORLD: {
            int lit = 0;
            if (gSdlSurface != nullptr && gSdlSurface->format->palette != nullptr) {
                const SDL_Color* colors = gSdlSurface->format->palette->colors;
                for (int index = 1; index < 229; index++) {
                    if (colors[index].r != 0 || colors[index].g != 0 || colors[index].b != 0) lit++;
                }
            }
            devAutotestLog("  non-cycling palette entries with color: %d\n", lit);
            devAutotestLog("  touch state: enabled %d, map input %d, map scroll %d, cursor %d, mode %d, iso disabled %d, ui disabled %d, touchscreen %d, world hit %d\n",
                touchControlsIsEnabled() ? 1 : 0,
                gameMouseIsMapInputEnabled() ? 1 : 0,
                gameMouseIsMapScrollingEnabled() ? 1 : 0,
                gameMouseGetCursor(),
                gameMouseGetMode(),
                isoIsDisabled() ? 1 : 0,
                gameUiIsDisabled() ? 1 : 0,
                touch_get_touchscreen_mode() ? 1 : 0,
                worldViewIsWorldAt(screenGetWidth() / 2, screenGetVisibleHeight() / 2) ? 1 : 0);
            if (!worldViewIsEnabled() || gDude == nullptr || gDude->tile < 0 || lit < 16
                || !gameMouseIsMapInputEnabled()
                || !worldViewIsWorldAt(screenGetWidth() / 2, screenGetVisibleHeight() / 2)) {
                devAutotestLog("autotest failed: loaded world palette or map touch input is unavailable\n");
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_COMBAT:
            devAutotestLog("  in combat %d, dude ap %d, dude tile %d, game mouse mode %d\n", isInCombat() ? 1 : 0, gDude->data.critter.combat.ap, gDude->tile, gameMouseGetMode());
            break;
        case DEV_AUTOTEST_ACTION_START_COMBAT: {
            Object* critter = devAutotestFindNearestCritter();
            if (critter != nullptr) {
                float x;
                float y;
                devAutotestGetObjectScreenPosition(critter, &x, &y);
                devAutotestLog("  starting combat, nearest critter at tile %d\n", critter->tile);

                // Combat loop runs inside, the script continues from there,
                // so this step is completed first.
                gDevAutotestPendingCapture = step->name;
                gDevAutotestStep++;
                gDevAutotestStepStarted = false;

                // Same as "A" key.
                _combat(nullptr);
                return;
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_RADIAL: {
            int x;
            int y;
            if (touchControlsGetRadialMenuItemPosition(static_cast<int>(step->a), &x, &y)) {
                devAutotestLog("  radial item %d at (%d, %d)\n", static_cast<int>(step->a), x, y);
                devAutotestStartTouch(static_cast<float>(x), static_cast<float>(y), static_cast<float>(x), static_cast<float>(y), 1, 0);
            } else {
                devAutotestLog("  radial item %d: MENU NOT OPEN\n", static_cast<int>(step->a));
            }
            break;
        }
        default:
            break;
        }

        worldViewGetVisibleRect(&visibleRect);
        devAutotestLog("%s: zoom %.3f, center tile %d, visible world rect (%d, %d) - (%d, %d)\n",
            step->name,
            worldViewGetZoom(),
            gCenterTile,
            visibleRect.left,
            visibleRect.top,
            visibleRect.right,
            visibleRect.bottom);
        devAutotestCheckDudeRoundTrip();
        return;
    }

    // Touch is advanced by `devAutotestPump`, wait for it.
    if (gDevAutotestTouchFrame != -1) {
        return;
    }

    if (gDevAutotestDelay > 0) {
        gDevAutotestDelay--;
        return;
    }

    gDevAutotestPendingCapture = step->name;
    gDevAutotestStep++;
    gDevAutotestStepStarted = false;
    gDevAutotestPinchFrame = -1;
}

void devAutotestPump()
{
    if (!gDevAutotestEnabled || gDevAutotestTouchFrame == -1) {
        return;
    }

    if (devAutotestTouchTick()) {
        Rect visibleRect;
        worldViewGetVisibleRect(&visibleRect);
        devAutotestLog("  after touch: visible world rect (%d, %d) - (%d, %d)%s\n", visibleRect.left, visibleRect.top, visibleRect.right, visibleRect.bottom,
            isInCombat() && _combat_whose_turn() != gDude ? ", others' turn" : "");
    }
}

void devAutotestCapture(SDL_Renderer* renderer)
{
    if (gDevAutotestPendingCapture == nullptr) {
        return;
    }

    const char* name = gDevAutotestPendingCapture;
    gDevAutotestPendingCapture = nullptr;

    if (gDevAutotestTraceItems) {
        devAutotestLog("%s: state\n", name);
        devAutotestLogItems();
        devAutotestLogInterface();
    }

    int width;
    int height;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0) {
        return;
    }

    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ABGR8888, pixels.data(), width * 4) != 0) {
        devAutotestLog("%s: read pixels failed: %s\n", name, SDL_GetError());
        return;
    }

    std::string path = gDevAutotestOutputPath + "/" + name + ".png";
    unsigned error = lodepng::encode(path, pixels, width, height, LCT_RGBA, 8);
    devAutotestLog("%s: saved %dx%d frame (%s)\n", name, width, height, error == 0 ? "ok" : "error");

    // The game's windows the screen shows besides the map: under the mobile
    // UI screens there should be none of the game's own.
    std::string windows;
    for (int id = 1; id < 50; id++) {
        Window* window = windowGetWindow(id);
        if (window == nullptr || (window->flags & WINDOW_HIDDEN) != 0 || id == gIsoWindow) {
            continue;
        }
        char entry[64];
        snprintf(entry, sizeof(entry), " %d(%dx%d at %d,%d)", id, window->width, window->height, window->rect.left, window->rect.top);
        windows += entry;
    }
    if (!windows.empty()) {
        devAutotestLog("  visible game windows:%s\n", windows.c_str());
    }
}

void devAutotestCaptureNextFrame(const char* name)
{
    if (devAutotestIsEnabled()) {
        gDevAutotestPendingCapture = name;
    }
}

// Checks that dude's tile survives screen -> world -> tile conversion, which
// is what mouse picking relies on.
static void devAutotestCheckDudeRoundTrip()
{
    if (gDude == nullptr) {
        return;
    }

    int worldX;
    int worldY;
    tileToScreenXY(gDude->tile, &worldX, &worldY);

    int screenX;
    int screenY;
    worldViewWorldToScreen(worldX + 16, worldY + 8, &screenX, &screenY);

    int pickedX;
    int pickedY;
    worldViewScreenToWorld(screenX, screenY, &pickedX, &pickedY);

    int pickedTile = tileFromScreenXY(pickedX, pickedY);
    devAutotestLog("  dude tile %d at screen (%d, %d), picked tile %d: %s\n",
        gDude->tile,
        screenX,
        screenY,
        pickedTile,
        pickedTile == gDude->tile ? "ok" : "MISMATCH");
}

static void devAutotestLog(const char* format, ...)
{
    std::string path = gDevAutotestOutputPath + "/log.txt";
    FILE* stream = fopen(path.c_str(), "a");
    if (stream == nullptr) {
        return;
    }

    va_list args;
    va_start(args, format);
    vfprintf(stream, format, args);
    va_end(args);

    fclose(stream);
}

void devAutotestNote(const char* format, ...)
{
    if (!devAutotestIsEnabled()) {
        return;
    }
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    devAutotestLog("%s", text);
}

} // namespace fallout
