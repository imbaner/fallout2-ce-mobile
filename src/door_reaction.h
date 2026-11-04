#ifndef FALLOUT_DOOR_REACTION_H_
#define FALLOUT_DOOR_REACTION_H_

#include <cstddef>
#include <set>

namespace fallout {

// CE: What a hit does to a door, told by reading its compiled script, which is
// never run: what the game's crosshair takes can't be asked in advance, and
// `damage_p_proc` breaks the door the moment it's run. Works on the bytes of
// the script file only (no game state), for any mod: a command it doesn't know
// counts as a change of the door.
//
// Every path through the procedure is followed. The only thing known is the
// weapon's damage type: a condition on anything else (variables, random, the
// damage) goes both ways. So the answer holds whatever the state of the game.

enum class DoorReaction {
    // Messages at most: "doesn't even put a scratch on the door".
    None,
    // The script acts (variables, guards, sounds), the door stays as it is.
    Script,
    // The door may break, open or go away.
    Door,
};

// Damage types of `DamageType` and these two.
constexpr int kDoorReactionUnarmed = -1;
constexpr int kDoorReactionUnknownDamageType = -2;

// The reaction of the script in [data] to a hit by the dude.
DoorReaction doorReactionRead(const unsigned char* data, size_t size, int damageType);

// Adds the weapons whose damage type the script in [data] changes while the
// game runs (their type can't be told in advance) to [weapons].
void doorReactionFindDamageTypeChanges(const unsigned char* data, size_t size, std::set<int>* weapons);

} // namespace fallout

#endif /* FALLOUT_DOOR_REACTION_H_ */
