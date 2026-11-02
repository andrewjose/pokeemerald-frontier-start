#ifndef GUARD_BATTLE_INFO_DAMAGE_H
#define GUARD_BATTLE_INFO_DAMAGE_H

// Damage calculator for the battle info screen.
//
// Predicts the damage range of a move using the game's own formula
// (CalculateBaseDamage) and the same steps the battle scripts apply after it:
// critical hit, STAB, type effectiveness and the 85-100% random roll. It
// reads battle state but never changes it, and never touches the RNG.

#include "battle.h"

enum {
    DMG_CALC_HIT,       // damaging move: min/max (and critMin/critMax if hasCrit)
    DMG_CALC_STATUS,    // status move, no damage
    DMG_CALC_IMMUNE,    // type or ability immunity
    DMG_CALC_FAILS,     // the move would fail here (no Stockpile, Damp, Dream Eater on an awake target, ...)
    DMG_CALC_VARIES,    // depends on damage taken this turn (Counter, Mirror Coat, Bide)
    DMG_CALC_OHKO,      // one-hit KO move that can work (accuracy permitting)
};

// A Pokemon as it is (or would be) in battle.
struct DamageCalcMon
{
    struct BattlePokemon mon;
    u8 battler;     // battler used for side/field lookups (a placeholder on that side when not in battle)
    bool8 active;   // in battle now; otherwise treated as if just switched in
    struct Pokemon *party; // the side's party (Beat Up)
};

struct DamageCalcResult
{
    u8 kind;
    u8 effectiveness;   // MOVE_RESULT_SUPER_EFFECTIVE / MOVE_RESULT_NOT_VERY_EFFECTIVE
    bool8 hasCrit;      // critMin/critMax are valid
    bool8 cantKO;       // False Swipe
    u8 minHits;         // number of hits (multi-hit moves); 1 otherwise
    u8 maxHits;
    s32 min;            // total damage over all hits
    s32 max;
    s32 critMin;
    s32 critMax;
};

// Fills calcMon from the live battle copy (activeBattler < MAX_BATTLERS_COUNT)
// or from the party data with stat stages reset, as on switch-in.
void DamageCalc_InitMon(struct DamageCalcMon *calcMon, struct Pokemon *party, u8 partyIndex, u8 side, u8 activeBattler);
void DamageCalc_Move(const struct DamageCalcMon *attacker, const struct DamageCalcMon *defender, u16 move, struct DamageCalcResult *result);
// gBattleWeather, or 0 if Cloud Nine / Air Lock cancels it.
u16 DamageCalc_GetActiveWeather(void);

#endif // GUARD_BATTLE_INFO_DAMAGE_H
