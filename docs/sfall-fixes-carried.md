# sfall fixes carried by the port

Fallout 2 CE builds sfall into the engine (sfall itself patches the original
`fallout2.exe` and can't be used with CE). Some of sfall's engine fixes
aren't in CE yet. The port adds them the way CE does, and offers each to CE
as a pull request:

- One fix (or one group of sfall's own) is a branch from upstream `main` and
  a pull request to [fallout2-ce](https://github.com/fallout2-ce/fallout2-ce).
- The same commit is cherry-picked into the port's `main` (`git cherry-pick
  -x`, the original commit is recorded in the message).
- The code follows sfall's behavior and values (flags, thresholds), marked
  `SFALL:` with sfall's name of the fix. No `[enhancements]` setting: the
  fixes sfall always applies are always on.
- An autotest (`src/dev_autotest.cc`, port-only) checks the behavior; it
  fails without the fix.
- Merging upstream: when CE has taken the fix, upstream's version wins in a
  conflict and the row below is marked merged. If CE changed it, the
  autotest still checks the behavior.

Which sfall fixes CE has and which it doesn't:
[sfall-fixes-audit.md](sfall-fixes-audit.md).

| sfall fix | Port commit | Pull request | Autotest | State |
|---|---|---|---|---|
| Fix to allow fleeing NPC to use drugs; Fix for AI not checking minimum HP properly for using stimpaks; Fix for NPC stuck in fleeing mode when the hit chance of a target was too low; Restore combat flags after fleeing when NPC cannot move closer to target (`Modules/AI.cpp`, `Game/combatAI.cpp`) | fd32f057, f44c480d (review: ReTarget only when it flees, not when it disengages) | [fallout2-ce#839](https://github.com/fallout2-ce/fallout2-ce/pull/839) | `fleeheal`, `retargetleave` | merged |
| use_obj_on_obj: an object using an item on itself uses it at once (`Modules/BugFixes.cpp`, `op_use_obj_on_obj_hack`) | e16a5be2 | [fallout2-ce#840](https://github.com/fallout2-ce/fallout2-ce/pull/840) | `talkheal` | merged |
| Critical hit table: sfall's corrections after CE's port of its table fixes - right arm messages of men, women, geckos, aliens and giant ants (cfcc66ad, 2023-12), the super mutants' fix on the right arm (c0f11b7a, 2024-02) (`Modules/Criticals.cpp`) | 42953e08 | [fallout2-ce#841](https://github.com/fallout2-ce/fallout2-ce/pull/841) | `crittable` | merged |
| Fix for incorrect initialization of action points at the beginning of each turn (`Modules/BugFixes.cpp`, `combat_hack`) | 550b490e | [fallout2-ce#850](https://github.com/fallout2-ce/fallout2-ce/pull/850) | `apturn` | merged |
| Fix for Bonus Move APs being replenished when you save and load the game in combat (`Modules/BugFixes.cpp`, `combat_load_hook`, `combat_turn_hack`) | 570fd40c, 4957d458 | [fallout2-ce#851](https://github.com/fallout2-ce/fallout2-ce/pull/851) | `bonusmoveload` | merged |
| Fix for the player's movement in combat being interrupted when trying to use objects with Bonus Move APs available; Fix for anim_move_to_tile_ engine function ignoring the distance argument for the player (`Modules/BugFixes.cpp`) | 0a7e92dd | [fallout2-ce#852](https://github.com/fallout2-ce/fallout2-ce/pull/852) | `bonusmovewalk` | merged |
| Fix for critter_state function when a critter in combat recovers from the knockout state outside its turn; Fixes for knockout/down issues; Fix for "NPC turns into a container" bug; Fix for knocked down critters not playing stand up animation when the combat ends (`Modules/BugFixes.cpp`) | 7f8dd5ff | [fallout2-ce#846](https://github.com/fallout2-ce/fallout2-ce/pull/846) | `knockouts` | merged |
| Fix for the removal of party member's corpse when loading the map (`Modules/BugFixes.cpp`, `partyFixMultipleMembers_hack`) | f96c6285 | [fallout2-ce#847](https://github.com/fallout2-ce/fallout2-ce/pull/847) | `partycorpse` | merged |
| Fix for critters not being healed over time when entering a map with 'dead_bodies_age=No' set in maps.txt (`Modules/BugFixes.cpp`, `map_age_dead_critters_hack`) | 84c3439f | [fallout2-ce#848](https://github.com/fallout2-ce/fallout2-ce/pull/848) | `mapageheal` | merged |
| Fix for being unable to sell used geiger counters or stealth boys (`Modules/BugFixes.cpp`, `CanSellUsedGeiger`, as `game.cfg` `[items] can_sell_used_geiger`) | 12dda5a2, d7a2a0fe | [fallout2-ce#853](https://github.com/fallout2-ce/fallout2-ce/pull/853) | `barterfixes`, `barterfixesoff` | open |
| Fix for being unable to sell/give items in the barter screen when the player/party member is overloaded (`Modules/BugFixes.cpp`) | b974b00b | [fallout2-ce#854](https://github.com/fallout2-ce/fallout2-ce/pull/854) | `barterfixes` | merged |
| Fix for gaining stats from more than two doses of a specific chem after save-load (`Modules/BugFixes.cpp`, drug pids in `sfallgv.sav`, little-endian) | e887915a, 82cb5613 | [fallout2-ce#855](https://github.com/fallout2-ce/fallout2-ce/pull/855) | `drugsave`, `drugpidsfile` | open |
| Fix for "Unlimited Ammo" exploit (`Modules/BugFixes.cpp`, `invenWieldFunc_item_get_type_hook`) | 2b5e62bd | [fallout2-ce#856](https://github.com/fallout2-ce/fallout2-ce/pull/856) | `wieldstack` | merged |
| Fix to reduce friendly fire in burst attacks (`Modules/AI.cpp`, `Game/AI/AIHelpers.cpp`) | c5d865eb | [fallout2-ce#857](https://github.com/fallout2-ce/fallout2-ce/pull/857) | `burstfriendly` | merged |
| Fix for ai_move_away_ engine function not working correctly in cases when needing to move a distance away from the target (`Modules/AI.cpp`) | 7908dc0a | [fallout2-ce#858](https://github.com/fallout2-ce/fallout2-ce/pull/858) | `moveaway` | merged |
| Fix for ai_retrieve_object_ engine function not returning the requested object when there are different objects with the same ID (`Modules/BugFixes.cpp`) | 8c8be205 | [fallout2-ce#859](https://github.com/fallout2-ce/fallout2-ce/pull/859) | `retrieveobj` | merged |
| Fix for NPC stuck in a loop of picking up items in combat and the incorrect message being displayed when the NPC cannot pick up an item due to not enough space in the inventory (`Modules/BugFixes.cpp`; message 1000 of `game\ce.msg`, sfall's translations) | ba666c92, 9c425cd9 | [fallout2-ce#860](https://github.com/fallout2-ce/fallout2-ce/pull/860) | `npcpickup` | merged |
| Fix for multihex critters moving too close and overlapping their targets in combat (`Modules/BugFixes.cpp`) | 28d0b87e | [fallout2-ce#861](https://github.com/fallout2-ce/fallout2-ce/pull/861) | `multihexmove`, `multihexretarget` | merged |
| Fix the placement of multihex critters in the player's party when entering a map or elevation (`Modules/BugFixes.cpp`) | e67d3b60 | [fallout2-ce#862](https://github.com/fallout2-ce/fallout2-ce/pull/862) | `multihexparty` | merged |
| Fix for destroy_p_proc not being called if the critter is killed by explosives when you leave the map (`Modules/BugFixes.cpp`) | a9939729 | [fallout2-ce#863](https://github.com/fallout2-ce/fallout2-ce/pull/863) | `explodeleave` | merged |
| Fix for not counting in the weight/size of equipped items on NPC when stealing or bartering (`Modules/BugFixes.cpp`; CE counted them only in the loot screen's weight label) | 2533b1ac | [fallout2-ce#864](https://github.com/fallout2-ce/fallout2-ce/pull/864) | `partyweight`, `partyweightsteal` | open |
| Fix for damage_p_proc being called for misses if the target is not a critter (`Modules/BugFixes.cpp`) | f5952641 | [fallout2-ce#865](https://github.com/fallout2-ce/fallout2-ce/pull/865) | `missobject` | merged |
| Fix for drop_obj function (`Modules/BugFixes.cpp`; the worn armor's stats after HOOK_INVENWIELD, as in sfall) | c4aaffc9, e1bec921 | [fallout2-ce#866](https://github.com/fallout2-ce/fallout2-ce/pull/866) | `droparmor` | merged |
| Skip party members' IDs for new objects, fix ID increment; don't put an item with a unique ID into a stack (`Modules/Objects.cpp`, `new_obj_id_hook`, `item_identical_hack`): items stacked again after about 9000 new objects in a session | 41736608 | [fallout2-ce#891](https://github.com/fallout2-ce/fallout2-ce/pull/891) | `objectids` | merged |

## Other fixes offered to CE

Where CE differs from the original game in a way sfall doesn't change
(checked against the original `fallout2.exe`), the same scheme: a branch
from upstream `main`, a pull request, a cherry-pick here, an autotest.

| Fix | Port commit | Pull request | Autotest | State |
|---|---|---|---|---|
| Corpse aging after 6/14 days, party member placement's first direction, a new critter proto's team: three changes of fallout2-ce#675 that differ from the original (the automap dots, a bug of the original, stay as CE has them) | 379f3359, 11682f53 | [fallout2-ce#849](https://github.com/fallout2-ce/fallout2-ce/pull/849) | `orig675` | merged |
| Saved maps' script lists: the extents written with the lengths of the saved scripts (party members' scripts over an extent's edge broke them), older saves' longer extents recovered when only not saved scripts are past the count. Since fallout2-ce#786 checks the lengths, such a map loaded as new and a transition's destination was lost (issue #16: Sonora's destroyer stairs led to the pier). aizerin's fix, carried as is (our #895 was a duplicate) | bdfcf15c | [fallout2-ce#893](https://github.com/fallout2-ce/fallout2-ce/pull/893) | `scriptsave` (`stairs` for a save's stairs) | merged |
| mods_order.txt made from the mods folder lists its folders named *.dat too, sorted, as sfall's LoadOrder.cpp (files only before: the Nevada repack's InventoryFilter, KeysHelp and Glasses folders were never loaded) | 97741231 | [fallout2-ce#897](https://github.com/fallout2-ce/fallout2-ce/pull/897) | - (Nevada copy: the made file) | merged |
| A script whose file isn't there no longer takes the game down: `programFatalError` isn't `[[noreturn]]`, it returns when no script runs (a program being loaded), as in the original, and `programCreateByPath` gives nullptr. Since `__builtin_unreachable` (1f91ab4d) control went past its end. Every scripts.lst has such entries (Fallout 2's own Test2.int); Fallout 1.5: Resurrection's New Hope village has an object with xnefunk7.int, so the game crashed on entering it. Fixes issue fallout2-ce#396 | 1ddcb790 | [fallout2-ce#898](https://github.com/fallout2-ce/fallout2-ce/pull/898) | `missingscript` | merged |
| An area's entrance without a tile enters at the map's own start, as in the original: since fallout2-ce#707 Golgotha and the whale encounter (elevation 0, tile -1 in city.txt) failed to load from the world map, an empty map with no exit (our #1, fallout2-ce#894; not fixed by #893) | ad4343b8 | [fallout2-ce#907](https://github.com/fallout2-ce/fallout2-ce/pull/907) | `golgotha` | open |
| Perk carryover: an owed level that offers no perk is chosen at the next level that offers one. No perk is available below level 3, so with a perk every level (sfall's `set_perk_freq`) the level 2 perk, and every one after it, stayed owed for good (our #19) | 502a964b, ec6e08b4 | [fallout2-ce#908](https://github.com/fallout2-ce/fallout2-ce/pull/908) | `perkowed`, `perkeverylevel` | open |

## Kept for rebuilds, not offered to CE

The port follows CE and mainline sfall. A few small changes are kept only
because third-party rebuilds of mods (Foxx's, made for sfall's 5.x line)
rely on that line's conventions and break without them. Each one changes
nothing for the official releases of the mods or for Fallout 2, isn't
something CE needs, and is marked `REBUILDS:` in the code (search for it).
If CE takes such a change after all, its version wins in a conflict; if CE
changes that code, the port's change goes with CE's way unless the rebuild
still breaks.

| Change | Port commit | Why not CE | Autotest | Rebuilds it serves |
|---|---|---|---|---|
| Movie timers: a negative `[movies] artimer1-4` means that Arroyo dream never comes (sfall 5.x: "-1 to disable"). As days, -1 came at once and, the movie missing, at every game event check: GVAR_TOWN_REP_ARROYO -15 each time, the Arroyo areas changed (Resurrection FE: its start town New Hope gone from the world map, its reputation falling) | d6c28db2 | official releases use 5000-9999 or the defaults; mainline sfall clamps to 0 and up ([fallout2-ce#896](https://github.com/fallout2-ce/fallout2-ce/pull/896) closed) | `movietimersoff` (on a rebuild's copy) | Fallout of Nevada EV (ddraw.ini), Fallout 1.5: Resurrection FE (config\game#patch.cfg) |
