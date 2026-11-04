#ifndef FALLOUT_DOOR_TARGETS_H_
#define FALLOUT_DOOR_TARGETS_H_

#include <vector>

#include "door_reaction.h"
#include "obj_types.h"

namespace fallout {

// CE: Doors as targets of the touch controls in combat. The game's crosshair
// takes any door, and mods make targets and obstacles of the type for that.
// Touch controls have no crosshair, a tap on a door uses it; so a door becomes
// a target only when the weapon in hand can do something to it, which its
// script tells (door_reaction.h).

// Scenery of the door type, the game's check for what the crosshair takes.
bool doorTargetIsDoor(Object* object);

// What a hit with [weapon] (nullptr - no weapon) does to [door].
DoorReaction doorTargetReaction(Object* door, Object* weapon);

// Whether a tap on [object] in combat picks it as a target: a closed door the
// dude can hit now, with what he holds, and it may change the door (with
// `[enhancements] door_targets_breakable_only` off, a script's reaction is
// enough).
bool doorTargetCanBeAttacked(Object* object);

// Game loop: finds the targets on the player's turns and outlines them unless
// the target highlight preference is off. An outline lives on the object but
// doesn't get saved: the game drops them on loading.
void doorTargetsUpdate();

// Tiles of the targets found (the tactical view draws them).
const std::vector<int>& doorTargetTiles();

// The target on [tile], nullptr - none.
Object* doorTargetAt(int tile);

} // namespace fallout

#endif /* FALLOUT_DOOR_TARGETS_H_ */
