// Damage calculator for the battle info screen.
//
// Mirrors what happens when a move is used: the power/type adjustments a move's
// battle script makes (Hidden Power, Return, Flail, Weather Ball, Facade, ...),
// then Cmd_damagecalc (CalculateBaseDamage, crit, Charge), Cmd_typecalc (STAB
// and the type chart) and Cmd_adjustnormaldamage (85-100% random roll). The
// roll and crit are not rolled: the result is the full range.
//
// The battle's globals that CalculateBaseDamage and AbilityBattleEffects read
// or write are saved first and restored afterwards, so the battle carries on
// exactly as before. Random() is never called.

#include "global.h"
#include "battle.h"
#include "battle_info_damage.h"
#include "battle_interface.h"
#include "battle_main.h"
#include "battle_util.h"
#include "battle_anim.h"
#include "pokedex.h"
#include "pokemon.h"
#include "constants/abilities.h"
#include "constants/battle_move_effects.h"
#include "constants/moves.h"

// Same as sFlailHpScaleToPowerTable in battle_script_commands.c
static const u8 sFlailPowerTable[] =
{
    1, 200,
    4, 150,
    9, 100,
    16, 80,
    32, 40,
    48, 20
};

// Same as sWeightToDamageTable in battle_script_commands.c
static const u16 sLowKickPowerTable[] =
{
    100, 20,
    250, 40,
    500, 60,
    1000, 80,
    2000, 100,
    0xFFFF, 0xFFFF
};

struct SavedGlobals
{
    u16 currentMove;
    u8 battlerAttacker;
    u8 battlerTarget;
    u16 movePower;
    u8 critMultiplier;
    u8 lastUsedAbility;
    u32 attackerResourceFlags;
};

static void SaveGlobals(struct SavedGlobals *saved, u8 attackerBattler)
{
    saved->currentMove = gCurrentMove;
    saved->battlerAttacker = gBattlerAttacker;
    saved->battlerTarget = gBattlerTarget;
    saved->movePower = gBattleMovePower;
    saved->critMultiplier = gCritMultiplier;
    saved->lastUsedAbility = gLastUsedAbility;
    saved->attackerResourceFlags = gBattleResources->flags->flags[attackerBattler];
}

static void RestoreGlobals(const struct SavedGlobals *saved, u8 attackerBattler)
{
    gCurrentMove = saved->currentMove;
    gBattlerAttacker = saved->battlerAttacker;
    gBattlerTarget = saved->battlerTarget;
    gBattleMovePower = saved->movePower;
    gCritMultiplier = saved->critMultiplier;
    gLastUsedAbility = saved->lastUsedAbility;
    gBattleResources->flags->flags[attackerBattler] = saved->attackerResourceFlags;
}

void DamageCalc_InitMon(struct DamageCalcMon *calcMon, struct Pokemon *party, u8 partyIndex, u8 side, u8 activeBattler)
{
    struct BattlePokemon *bmon = &calcMon->mon;
    struct Pokemon *mon = &party[partyIndex];
    u8 i;

    calcMon->party = party;
    if (activeBattler < MAX_BATTLERS_COUNT)
    {
        *bmon = gBattleMons[activeBattler];
        calcMon->battler = activeBattler;
        calcMon->active = TRUE;
        return;
    }

    // Not in battle: build the battle copy the way a switch-in does
    // (party data, base types and ability, neutral stat stages).
    memset(bmon, 0, sizeof(*bmon));
    bmon->species = GetMonData(mon, MON_DATA_SPECIES);
    bmon->item = GetMonData(mon, MON_DATA_HELD_ITEM);
    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        bmon->moves[i] = GetMonData(mon, MON_DATA_MOVE1 + i);
        bmon->pp[i] = GetMonData(mon, MON_DATA_PP1 + i);
    }
    bmon->ppBonuses = GetMonData(mon, MON_DATA_PP_BONUSES);
    bmon->friendship = GetMonData(mon, MON_DATA_FRIENDSHIP);
    bmon->hpIV = GetMonData(mon, MON_DATA_HP_IV);
    bmon->attackIV = GetMonData(mon, MON_DATA_ATK_IV);
    bmon->defenseIV = GetMonData(mon, MON_DATA_DEF_IV);
    bmon->speedIV = GetMonData(mon, MON_DATA_SPEED_IV);
    bmon->spAttackIV = GetMonData(mon, MON_DATA_SPATK_IV);
    bmon->spDefenseIV = GetMonData(mon, MON_DATA_SPDEF_IV);
    bmon->abilityNum = GetMonData(mon, MON_DATA_ABILITY_NUM);
    bmon->ability = GetAbilityBySpecies(bmon->species, bmon->abilityNum);
    bmon->types[0] = gSpeciesInfo[bmon->species].types[0];
    bmon->types[1] = gSpeciesInfo[bmon->species].types[1];
    bmon->personality = GetMonData(mon, MON_DATA_PERSONALITY);
    bmon->status1 = GetMonData(mon, MON_DATA_STATUS);
    bmon->level = GetMonData(mon, MON_DATA_LEVEL);
    bmon->hp = GetMonData(mon, MON_DATA_HP);
    bmon->maxHP = GetMonData(mon, MON_DATA_MAX_HP);
    bmon->attack = GetMonData(mon, MON_DATA_ATK);
    bmon->defense = GetMonData(mon, MON_DATA_DEF);
    bmon->speed = GetMonData(mon, MON_DATA_SPEED);
    bmon->spAttack = GetMonData(mon, MON_DATA_SPATK);
    bmon->spDefense = GetMonData(mon, MON_DATA_SPDEF);
    for (i = 0; i < NUM_BATTLE_STATS; i++)
        bmon->statStages[i] = DEFAULT_STAT_STAGE;

    // Field lookups only need a battler on the right side.
    calcMon->battler = GetBattlerAtPosition(side == B_SIDE_PLAYER ? B_POSITION_PLAYER_LEFT : B_POSITION_OPPONENT_LEFT);
    calcMon->active = FALSE;
}

// ModulateDmgByType from battle_script_commands.c, on a local damage value.
static s32 ModulateByType(s32 damage, u8 multiplier, u16 move, u8 *flags)
{
    damage = damage * multiplier / 10;
    if (damage == 0 && multiplier != 0)
        damage = 1;

    switch (multiplier)
    {
    case TYPE_MUL_NO_EFFECT:
        *flags |= MOVE_RESULT_DOESNT_AFFECT_FOE;
        *flags &= ~(MOVE_RESULT_NOT_VERY_EFFECTIVE | MOVE_RESULT_SUPER_EFFECTIVE);
        break;
    case TYPE_MUL_NOT_EFFECTIVE:
        if (gBattleMoves[move].power && !(*flags & MOVE_RESULT_NO_EFFECT))
        {
            if (*flags & MOVE_RESULT_SUPER_EFFECTIVE)
                *flags &= ~MOVE_RESULT_SUPER_EFFECTIVE;
            else
                *flags |= MOVE_RESULT_NOT_VERY_EFFECTIVE;
        }
        break;
    case TYPE_MUL_SUPER_EFFECTIVE:
        if (gBattleMoves[move].power && !(*flags & MOVE_RESULT_NO_EFFECT))
        {
            if (*flags & MOVE_RESULT_NOT_VERY_EFFECTIVE)
                *flags &= ~MOVE_RESULT_NOT_VERY_EFFECTIVE;
            else
                *flags |= MOVE_RESULT_SUPER_EFFECTIVE;
        }
        break;
    }
    return damage;
}

// Cmd_typecalc: STAB, Levitate, the type chart (with Foresight) and Wonder Guard.
static s32 ApplyTypeCalc(s32 damage, const struct DamageCalcMon *attacker, const struct DamageCalcMon *defender, u16 move, u8 moveType, u8 *flags)
{
    const struct BattlePokemon *def = &defender->mon;
    s32 i = 0;

    if (move == MOVE_STRUGGLE)
        return damage;

    if (attacker->mon.types[0] == moveType || attacker->mon.types[1] == moveType)
        damage = damage * 15 / 10;

    if (def->ability == ABILITY_LEVITATE && moveType == TYPE_GROUND)
    {
        *flags |= MOVE_RESULT_MISSED | MOVE_RESULT_DOESNT_AFFECT_FOE;
    }
    else
    {
        while (TYPE_EFFECT_ATK_TYPE(i) != TYPE_ENDTABLE)
        {
            if (TYPE_EFFECT_ATK_TYPE(i) == TYPE_FORESIGHT)
            {
                if (def->status2 & STATUS2_FORESIGHT)
                    break;
                i += 3;
                continue;
            }
            else if (TYPE_EFFECT_ATK_TYPE(i) == moveType)
            {
                if (TYPE_EFFECT_DEF_TYPE(i) == def->types[0])
                    damage = ModulateByType(damage, TYPE_EFFECT_MULTIPLIER(i), move, flags);
                if (TYPE_EFFECT_DEF_TYPE(i) == def->types[1] && def->types[0] != def->types[1])
                    damage = ModulateByType(damage, TYPE_EFFECT_MULTIPLIER(i), move, flags);
            }
            i += 3;
        }
    }

    if (def->ability == ABILITY_WONDER_GUARD
     && (!(*flags & MOVE_RESULT_SUPER_EFFECTIVE) || ((*flags & (MOVE_RESULT_SUPER_EFFECTIVE | MOVE_RESULT_NOT_VERY_EFFECTIVE)) == (MOVE_RESULT_SUPER_EFFECTIVE | MOVE_RESULT_NOT_VERY_EFFECTIVE)))
     && gBattleMoves[move].power)
        *flags |= MOVE_RESULT_MISSED;

    return damage;
}

// Cmd_damagecalc for one hit, before type effectiveness and the random roll.
static s32 BaseDamage(const struct DamageCalcMon *attacker, const struct DamageCalcMon *defender, u16 move, u16 power, u8 typeOverride, u8 dmgMultiplier, u8 critMultiplier)
{
    struct BattlePokemon atk = attacker->mon;
    struct BattlePokemon def = defender->mon;
    s32 damage;

    gCritMultiplier = critMultiplier;
    damage = CalculateBaseDamage(&atk, &def, move, gSideStatuses[GetBattlerSide(defender->battler)],
                                 power, typeOverride, attacker->battler, defender->battler);
    damage = damage * critMultiplier * dmgMultiplier;

    if (attacker->active && (gStatuses3[attacker->battler] & STATUS3_CHARGED_UP) && gBattleMoves[move].type == TYPE_ELECTRIC)
        damage *= 2;
    if (attacker->active && gProtectStructs[attacker->battler].helpingHand)
        damage = damage * 15 / 10;
    return damage;
}

// Cmd_adjustnormaldamage's roll, at a fixed percentage.
static s32 ApplyRoll(s32 damage, u8 percent)
{
    if (damage != 0)
    {
        damage = damage * percent / 100;
        if (damage == 0)
            damage = 1;
    }
    return damage;
}

static bool8 IsHealthyForBeatUp(struct Pokemon *mon)
{
    u16 species = GetMonData(mon, MON_DATA_SPECIES_OR_EGG);
    return GetMonData(mon, MON_DATA_HP) != 0
        && species != SPECIES_NONE
        && species != SPECIES_EGG
        && !GetMonData(mon, MON_DATA_STATUS);
}

static void CalcMoveInternal(const struct DamageCalcMon *attacker, const struct DamageCalcMon *defender, u16 move, struct DamageCalcResult *result)
{
    const struct BattlePokemon *atk = &attacker->mon;
    const struct BattlePokemon *def = &defender->mon;
    u8 effect = gBattleMoves[move].effect;
    u8 moveType = gBattleMoves[move].type;
    u8 typeOverride = 0;
    u16 power = 0;      // 0 = the move's own power
    u16 powerMax = 0;   // for moves with a random power: the top of the range
    u8 dmgMultiplier = 1;
    u8 flags = 0;
    s32 fixedDamage = -1;
    s32 i;

    result->kind = DMG_CALC_HIT;
    result->minHits = result->maxHits = 1;

    // --- Moves that don't use the damage formula ---------------------------
    switch (effect)
    {
    case EFFECT_COUNTER:
    case EFFECT_MIRROR_COAT:
    case EFFECT_BIDE:
        result->kind = DMG_CALC_VARIES;
        return;
    }
    if (gBattleMoves[move].power == 0)
    {
        result->kind = DMG_CALC_STATUS;
        return;
    }

    // --- Power and type adjustments made by the move's battle script ---------
    switch (effect)
    {
    case EFFECT_HIDDEN_POWER:
    {
        u8 powerBits = ((atk->hpIV & 2) >> 1)
                     | ((atk->attackIV & 2) << 0)
                     | ((atk->defenseIV & 2) << 1)
                     | ((atk->speedIV & 2) << 2)
                     | ((atk->spAttackIV & 2) << 3)
                     | ((atk->spDefenseIV & 2) << 4);
        u8 typeBits  = ((atk->hpIV & 1) << 0)
                     | ((atk->attackIV & 1) << 1)
                     | ((atk->defenseIV & 1) << 2)
                     | ((atk->speedIV & 1) << 3)
                     | ((atk->spAttackIV & 1) << 4)
                     | ((atk->spDefenseIV & 1) << 5);
        power = (40 * powerBits) / 63 + 30;
        moveType = ((NUMBER_OF_MON_TYPES - 3) * typeBits) / 63 + 1;
        if (moveType >= TYPE_MYSTERY)
            moveType++;
        typeOverride = moveType | F_DYNAMIC_TYPE_IGNORE_PHYSICALITY | F_DYNAMIC_TYPE_SET;
        break;
    }
    case EFFECT_RETURN:
        power = 10 * atk->friendship / 25;
        break;
    case EFFECT_FRUSTRATION:
        power = 10 * (MAX_FRIENDSHIP - atk->friendship) / 25;
        break;
    case EFFECT_FLAIL:
    {
        s32 hpFraction = GetScaledHPFraction(atk->hp, atk->maxHP, 48);
        for (i = 0; i < (s32)sizeof(sFlailPowerTable); i += 2)
        {
            if (hpFraction <= sFlailPowerTable[i])
                break;
        }
        power = sFlailPowerTable[i + 1];
        break;
    }
    case EFFECT_ERUPTION:
        power = atk->hp * gBattleMoves[move].power / atk->maxHP;
        if (power == 0)
            power = 1;
        break;
    case EFFECT_LOW_KICK:
        for (i = 0; sLowKickPowerTable[i] != 0xFFFF; i += 2)
        {
            if (sLowKickPowerTable[i] > GetPokedexHeightWeight(SpeciesToNationalPokedexNum(def->species), 1))
                break;
        }
        power = (sLowKickPowerTable[i] != 0xFFFF) ? sLowKickPowerTable[i + 1] : 120;
        break;
    case EFFECT_MAGNITUDE:
        power = 10;
        powerMax = 150;
        if (defender->active && (gStatuses3[defender->battler] & STATUS3_UNDERGROUND))
            dmgMultiplier = 2;
        break;
    case EFFECT_PRESENT:
        power = 40;
        powerMax = 120;
        break;
    case EFFECT_ROLLOUT:
    {
        // The hit after this one's timer value, as in Cmd_rolloutdamagecalculation
        u8 timer = (attacker->active && (atk->status2 & STATUS2_MULTIPLETURNS)) ? gDisableStructs[attacker->battler].rolloutTimer : 5;
        timer--;
        power = gBattleMoves[move].power;
        for (i = 1; i < (5 - timer); i++)
            power *= 2;
        if (atk->status2 & STATUS2_DEFENSE_CURL)
            power *= 2;
        break;
    }
    case EFFECT_FURY_CUTTER:
    {
        u8 counter = attacker->active ? gDisableStructs[attacker->battler].furyCutterCounter : 0;
        if (counter != 5)
            counter++;
        power = gBattleMoves[move].power;
        for (i = 1; i < counter; i++)
            power *= 2;
        break;
    }
    case EFFECT_WEATHER_BALL:
        if (WEATHER_HAS_EFFECT)
        {
            if (gBattleWeather & B_WEATHER_ANY)
                dmgMultiplier = 2;
            if (gBattleWeather & B_WEATHER_RAIN)
                moveType = TYPE_WATER;
            else if (gBattleWeather & B_WEATHER_SANDSTORM)
                moveType = TYPE_ROCK;
            else if (gBattleWeather & B_WEATHER_SUN)
                moveType = TYPE_FIRE;
            else if (gBattleWeather & B_WEATHER_HAIL)
                moveType = TYPE_ICE;
            else
                moveType = TYPE_NORMAL;
            typeOverride = moveType | F_DYNAMIC_TYPE_SET;
        }
        break;
    case EFFECT_FACADE:
        if (atk->status1 & (STATUS1_POISON | STATUS1_BURN | STATUS1_PARALYSIS | STATUS1_TOXIC_POISON))
            dmgMultiplier = 2;
        break;
    case EFFECT_SMELLINGSALT:
        if (!(def->status2 & STATUS2_SUBSTITUTE) && (def->status1 & STATUS1_PARALYSIS))
            dmgMultiplier = 2;
        break;
    case EFFECT_GUST:
    case EFFECT_TWISTER:
        if (defender->active && (gStatuses3[defender->battler] & STATUS3_ON_AIR))
            dmgMultiplier = 2;
        break;
    case EFFECT_EARTHQUAKE:
        if (defender->active && (gStatuses3[defender->battler] & STATUS3_UNDERGROUND))
            dmgMultiplier = 2;
        break;
    case EFFECT_FLINCH_MINIMIZE_HIT:
        if (defender->active && (gStatuses3[defender->battler] & STATUS3_MINIMIZED))
            dmgMultiplier = 2;
        break;
    case EFFECT_EXPLOSION:
        if (ABILITY_ON_FIELD(ABILITY_DAMP) || def->ability == ABILITY_DAMP)
        {
            result->kind = DMG_CALC_FAILS;
            return;
        }
        break;
    case EFFECT_DREAM_EATER:
        if ((def->status2 & STATUS2_SUBSTITUTE) || !(def->status1 & STATUS1_SLEEP))
        {
            result->kind = DMG_CALC_FAILS;
            return;
        }
        break;
    case EFFECT_SNORE:
        if (!(atk->status1 & STATUS1_SLEEP))
        {
            result->kind = DMG_CALC_FAILS;
            return;
        }
        break;
    case EFFECT_SPIT_UP:
        if (!attacker->active || gDisableStructs[attacker->battler].stockpileCounter == 0)
        {
            result->kind = DMG_CALC_FAILS;
            return;
        }
        break;
    case EFFECT_FALSE_SWIPE:
        result->cantKO = TRUE;
        break;
    }
    if ((move == MOVE_SURF || move == MOVE_WHIRLPOOL) && defender->active && (gStatuses3[defender->battler] & STATUS3_UNDERWATER))
        dmgMultiplier = 2;

    // --- Ability immunities checked before the hit (attackcanceler) ----------
    if ((def->ability == ABILITY_VOLT_ABSORB && moveType == TYPE_ELECTRIC)
     || (def->ability == ABILITY_WATER_ABSORB && moveType == TYPE_WATER)
     || (def->ability == ABILITY_FLASH_FIRE && moveType == TYPE_FIRE && !(def->status1 & STATUS1_FREEZE))
     || (def->ability == ABILITY_SOUNDPROOF && (move == MOVE_SNORE || move == MOVE_UPROAR || move == MOVE_HYPER_VOICE)))
    {
        result->kind = DMG_CALC_IMMUNE;
        return;
    }

    // --- Set-damage moves: typecalc only decides whether they connect ---------
    switch (effect)
    {
    case EFFECT_LEVEL_DAMAGE:
        fixedDamage = atk->level;
        break;
    case EFFECT_DRAGON_RAGE:
        fixedDamage = 40;
        break;
    case EFFECT_SONICBOOM:
        fixedDamage = 20;
        break;
    case EFFECT_SUPER_FANG:
        fixedDamage = def->hp / 2;
        if (fixedDamage == 0)
            fixedDamage = 1;
        break;
    case EFFECT_ENDEAVOR:
        if (def->hp <= atk->hp)
        {
            result->kind = DMG_CALC_FAILS;
            return;
        }
        fixedDamage = def->hp - atk->hp;
        break;
    case EFFECT_PSYWAVE:
        fixedDamage = atk->level * 50 / 100;
        break;
    case EFFECT_OHKO:
        fixedDamage = 0;
        break;
    }
    if (fixedDamage >= 0)
    {
        ApplyTypeCalc(1, attacker, defender, move, moveType, &flags);
        if (flags & MOVE_RESULT_NO_EFFECT)
        {
            result->kind = DMG_CALC_IMMUNE;
            return;
        }
        if (effect == EFFECT_OHKO)
        {
            // Cmd_tryKO: Sturdy blocks it, and it can't hit a higher-level target
            if (def->ability == ABILITY_STURDY || atk->level < def->level)
                result->kind = DMG_CALC_FAILS;
            else
                result->kind = DMG_CALC_OHKO;
            return;
        }
        result->min = fixedDamage;
        result->max = (effect == EFFECT_PSYWAVE) ? atk->level * 150 / 100 : fixedDamage;
        return;
    }

    // --- Beat Up: one typeless hit per healthy party member (Cmd_trydobeatup) --
    if (effect == EFFECT_BEAT_UP)
    {
        u8 hits = 0;

        if (def->hp == 0)
        {
            result->kind = DMG_CALC_FAILS;
            return;
        }
        for (i = 0; i < PARTY_SIZE; i++)
        {
            struct Pokemon *mon = &attacker->party[i];
            s32 damage;

            if (!IsHealthyForBeatUp(mon))
                continue;
            damage = gSpeciesInfo[GetMonData(mon, MON_DATA_SPECIES)].baseAttack;
            damage *= gBattleMoves[move].power;
            damage *= (GetMonData(mon, MON_DATA_LEVEL) * 2 / 5 + 2);
            damage /= gSpeciesInfo[def->species].baseDefense;
            damage = (damage / 50) + 2;
            result->min += ApplyRoll(damage, 85);
            result->max += damage;
            hits++;
        }
        if (hits == 0)
        {
            result->kind = DMG_CALC_FAILS;
            return;
        }
        result->minHits = result->maxHits = hits;
        return;
    }

    // --- Future Sight / Doom Desire: base damage now, typeless roll on impact --
    if (effect == EFFECT_FUTURE_SIGHT)
    {
        s32 damage = BaseDamage(attacker, defender, move, 0, 0, 1, 1);
        result->min = ApplyRoll(damage, 85);
        result->max = damage;
        return;
    }

    // --- Spit Up: base damage x stockpile, then typecalc, no roll or crit -----
    if (effect == EFFECT_SPIT_UP)
    {
        s32 damage = BaseDamage(attacker, defender, move, 0, 0, 1, 1) * gDisableStructs[attacker->battler].stockpileCounter;
        damage = ApplyTypeCalc(damage, attacker, defender, move, moveType, &flags);
        if (flags & MOVE_RESULT_NO_EFFECT)
        {
            result->kind = DMG_CALC_IMMUNE;
            return;
        }
        result->effectiveness = flags & (MOVE_RESULT_SUPER_EFFECTIVE | MOVE_RESULT_NOT_VERY_EFFECTIVE);
        result->min = result->max = damage;
        return;
    }

    // --- Normal hits ------------------------------------------------------------
    {
        u8 numPowers = 1;
        u16 powers[3];
        bool8 canCrit = def->ability != ABILITY_BATTLE_ARMOR && def->ability != ABILITY_SHELL_ARMOR
                     && !(attacker->active && (gStatuses3[attacker->battler] & STATUS3_CANT_SCORE_A_CRIT));
        s32 lo, hi, critLo, critHi;

        if (effect == EFFECT_TRIPLE_KICK)
        {
            // Each kick hits harder: 10, 20, 30
            powers[0] = 10;
            powers[1] = 20;
            powers[2] = 30;
            numPowers = 3;
        }
        else
        {
            powers[0] = power;
        }

        lo = hi = critLo = critHi = 0;
        for (i = 0; i < numPowers; i++)
        {
            s32 normal, crit;
            u8 critFlags = 0;

            // Type effectiveness doesn't depend on power or crits, so the flags
            // from the first (normal) hit are the ones reported.
            flags = 0;
            normal = ApplyTypeCalc(BaseDamage(attacker, defender, move, powers[i], typeOverride, dmgMultiplier, 1), attacker, defender, move, moveType, &flags);
            crit = ApplyTypeCalc(BaseDamage(attacker, defender, move, powers[i], typeOverride, dmgMultiplier, 2), attacker, defender, move, moveType, &critFlags);
            if (flags & MOVE_RESULT_NO_EFFECT)
            {
                result->kind = DMG_CALC_IMMUNE;
                return;
            }
            lo += ApplyRoll(normal, 85);
            critLo += ApplyRoll(crit, 85);

            if (powerMax != 0)
            {
                // Random power (Magnitude, Present): top of the range uses the highest power
                critFlags = 0;
                normal = ApplyTypeCalc(BaseDamage(attacker, defender, move, powerMax, typeOverride, dmgMultiplier, 1), attacker, defender, move, moveType, &critFlags);
                critFlags = 0;
                crit = ApplyTypeCalc(BaseDamage(attacker, defender, move, powerMax, typeOverride, dmgMultiplier, 2), attacker, defender, move, moveType, &critFlags);
            }
            hi += normal;
            critHi += crit;
        }
        result->effectiveness = flags & (MOVE_RESULT_SUPER_EFFECTIVE | MOVE_RESULT_NOT_VERY_EFFECTIVE);

        switch (effect)
        {
        case EFFECT_MULTI_HIT:
            result->minHits = 2;
            result->maxHits = 5;
            break;
        case EFFECT_DOUBLE_HIT:
        case EFFECT_TWINEEDLE:
            result->minHits = result->maxHits = 2;
            break;
        case EFFECT_TRIPLE_KICK:
            result->minHits = result->maxHits = 3;
            break;
        }

        if (result->maxHits > 1)
        {
            // Totals over all hits; per-hit crits aren't shown
            if (effect != EFFECT_TRIPLE_KICK)
            {
                lo *= result->minHits;
                hi *= result->maxHits;
            }
            result->hasCrit = FALSE;
        }
        else
        {
            result->hasCrit = canCrit;
        }
        result->min = lo;
        result->max = hi;
        result->critMin = critLo;
        result->critMax = critHi;
    }
}

void DamageCalc_Move(const struct DamageCalcMon *attacker, const struct DamageCalcMon *defender, u16 move, struct DamageCalcResult *result)
{
    struct SavedGlobals saved;

    memset(result, 0, sizeof(*result));
    if (move == MOVE_NONE)
    {
        result->kind = DMG_CALC_STATUS;
        return;
    }

    SaveGlobals(&saved, attacker->battler);
    gCurrentMove = move;
    gBattlerAttacker = attacker->battler;
    gBattlerTarget = defender->battler;
    if (!attacker->active)
    {
        // A Pokemon switching in doesn't inherit the active one's Flash Fire boost
        gBattleResources->flags->flags[attacker->battler] &= ~RESOURCE_FLAG_FLASH_FIRE;
    }

    CalcMoveInternal(attacker, defender, move, result);

    RestoreGlobals(&saved, attacker->battler);
}

u16 DamageCalc_GetActiveWeather(void)
{
    u8 lastUsedAbility = gLastUsedAbility;
    u8 battlerAttacker = gBattlerAttacker;
    u8 battlerTarget = gBattlerTarget;
    u16 weather = WEATHER_HAS_EFFECT ? gBattleWeather : 0;

    gLastUsedAbility = lastUsedAbility;
    gBattlerAttacker = battlerAttacker;
    gBattlerTarget = battlerTarget;
    return weather;
}
