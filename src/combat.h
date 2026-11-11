#ifndef COMBAT_H
#define COMBAT_H

#include "combat_defs.h"
#include "db.h"
#include "obj_types.h"
#include "proto_types.h"

namespace fallout {

extern int _combatNumTurns;
extern CombatState gCombatState;

extern int _combat_free_move;

int combatInit();
void combatReset();
void combatExit();
int _find_cid(int a1, int a2, Object** a3, int a4);
int combatLoad(File* stream);
int combatSave(File* stream);
bool _combat_safety_invalidate_weapon(Object* attacker, Object* weapon, HitMode hitMode, Object* defender, int* safeDistancePtr);
bool _combatTestIncidentalHit(Object* attacker, Object* defender, Object* attackerFriend, Object* weapon);
Object* _combat_whose_turn();
void _combat_data_init(Object* obj);
Object* aiInfoGetFriendlyDead(Object* obj);
int aiInfoSetFriendlyDead(Object* a1, Object* a2);
Object* aiInfoGetLastTarget(Object* obj);
int aiInfoSetLastTarget(Object* a1, Object* a2);
Object* aiInfoGetLastItem(Object* obj);
int aiInfoSetLastItem(Object* obj, Object* a2);
void _combat_update_critter_outline_for_los(Object* critter, bool enableOutline);
void _combat_over_from_load();
void _combat_give_exps(int exp_points);
void _combat_turn_run();
void _combat(CombatStartData* csd);
void attackInit(Attack* attack, Object* attacker, Object* defender, HitMode hitMode, HitLocation hitLocation);
int _combat_attack(Object* attacker, Object* defender, HitMode hitMode, HitLocation hitLocation);
int _combat_bullet_start(const Object* attacker, const Object* target);
void _compute_explosion_on_extras(Attack* attack, bool isFromAttacker, bool isGrenade, bool noDamage);
int _determine_to_hit(Object* a1, Object* a2, HitLocation hitLocation, HitMode hitMode);
int _determine_to_hit_no_range(Object* attacker, Object* defender, HitLocation hitLocation, HitMode hitMode, unsigned char* a5);
int _determine_to_hit_from_tile(Object* attacker, int tile, Object* defender, HitLocation hitLocation, HitMode hitMode);
void attackComputeDeathFlags(Attack* attack);
void _apply_damage(Attack* attack, bool animated);
void _combat_display(Attack* attack);
void _combat_anim_begin();
void _combat_anim_finished();
CombatBadShot _combat_check_bad_shot(Object* attacker, Object* defender, HitMode hitMode, bool aiming);
bool _combat_to_hit(Object* target, int* accuracy);
void _combat_attack_this(Object* target);

// CE: Screen position of the hit location button (0-7) of the called shot
// window (for automated tests).
// CE: Mobile UI called shot panel (mui_called_shot.cc): the hit locations as
// the game's window lists them (its left column, then the right one), their
// names for [critter] and hit chances with [hitMode].
struct CalledShotTarget {
    HitLocation location;
    const char* name;
    int chance;
};

constexpr int kCalledShotTargetCount = 8;

void calledShotGetTargets(Object* critter, HitMode hitMode, CalledShotTarget* targets);
void _combat_outline_on();
void _combat_outline_off();
// CE: Touch controls have no cursor to keep in targeting: target highlight
// "On" outlines critters the whole player's turn (the game: while the
// cursor is a crosshair), "Targeting only" while an enemy is selected (the
// crosshair mode touch controls set then).
bool combatOutlinesFollowTurn();
// Living critters in the combat (the tactical view outlines them all).
void combatForEachCritter(void (*callback)(Object* critter));
void _combat_highlight_change();
bool _combat_is_shot_blocked(Object* sourceObj, int from, int to, Object* targetObj, int* numCrittersOnLof);
int _combat_player_knocked_out_by();
int _combat_explode_scenery(Object* a1, Object* a2);
void _combat_delete_critter(Object* obj);
void _combatKillCritterOutsideCombat(Object* critter_obj, char* msg);

int combatGetTargetHighlight();
int criticalsGetValue(KillType killType, HitLocation hitLocation, CriticalEffect effect, CriticalHitDataMember dataMember);
void criticalsSetValue(KillType killType, HitLocation hitLocation, CriticalEffect effect, CriticalHitDataMember dataMember, int value);
void criticalsResetValue(KillType killType, HitLocation hitLocation, CriticalEffect effect, CriticalHitDataMember dataMember);
bool criticalsNoTimeLimits();
int unarmedGetDamage(HitMode hitMode, int* minDamagePtr, int* maxDamagePtr);
int unarmedGetBonusCriticalChance(HitMode hitMode);
int unarmedGetActionPointCost(HitMode hitMode);
bool unarmedIsPenetrating(HitMode hitMode);
HitMode unarmedGetPunchHitMode(bool isSecondary);
HitMode unarmedGetKickHitMode(bool isSecondary);
bool unarmedIsPenetrating(HitMode hitMode);
bool damageModGetBonusHthDamageFix();
bool damageModGetDisplayBonusDamage();
int combat_get_hit_location_penalty(HitLocation hitLocation);
void combat_set_hit_location_penalty(HitLocation hitLocation, int penalty);
void combat_reset_hit_location_penalty();
void combatSetFo1HitChance(bool enabled);
void combatResetFo1HitChance();
Attack* combat_get_data();

// CE: What ending combat would do from the player's turn now, for the touch
// HUD's buttons. Nothing in the game changes: End Combat itself
// (`combatAttemptEnd`) asks the critters' AI and runs bystanders' scripts,
// which can't be asked ahead, so only what is certain is told.
enum class CombatEndOutlook {
    // A hostile combatant who sees the player fights on: End Combat would
    // be refused.
    Refused,
    // End Combat may work (the critters left decide when it's pressed);
    // End Turn keeps the combat going.
    Possible,
    // Nobody has a living foe to fight: the combat ends at the end of this
    // round anyway, End Combat only ends it now.
    EndsThisRound,
};

CombatEndOutlook combatEndOutlook();

static inline bool isInCombat()
{
    return (gCombatState & COMBAT_STATE_IN_COMBAT) != 0;
}

static inline bool isUnarmedHitMode(HitMode hitMode)
{
    return hitMode == HIT_MODE_PUNCH
        || hitMode == HIT_MODE_KICK
        || (hitMode >= FIRST_ADVANCED_UNARMED_HIT_MODE && hitMode <= LAST_ADVANCED_UNARMED_HIT_MODE);
}

} // namespace fallout

#endif /* COMBAT_H */
