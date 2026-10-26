#include "global.h"
#include "pokemon.h"
#include "preset_parties.h"
#include "pokemon_storage_system.h"
#include "event_data.h"
#include "constants/species.h"
#include "constants/moves.h"
#include "constants/items.h"
#include "constants/pokemon.h"
#include "constants/characters.h"

#define ARCHETYPE_OFFENSE      0
#define ARCHETYPE_STALL        1
#define ARCHETYPE_BALANCED     2
#define ARCHETYPE_SPEED        3
#define ARCHETYPE_TRICKROOM    4
#define ARCHETYPE_RAIN         5
#define ARCHETYPE_SUN          6
#define ARCHETYPE_SAND         7
#define NUM_ARCHETYPES         8
#define PRESET_PARTY_SIZE      6

static const u8 sBoxName_Offense[]   = {CHAR_O, CHAR_F, CHAR_F, CHAR_E, CHAR_N, CHAR_S, CHAR_E, EOS};
static const u8 sBoxName_Stall[]     = {CHAR_S, CHAR_T, CHAR_A, CHAR_L, CHAR_L, EOS};
static const u8 sBoxName_Balanced[]  = {CHAR_B, CHAR_A, CHAR_L, CHAR_A, CHAR_N, CHAR_C, CHAR_E, CHAR_D, EOS};
static const u8 sBoxName_Speed[]     = {CHAR_S, CHAR_P, CHAR_E, CHAR_E, CHAR_D, EOS};
static const u8 sBoxName_TrickRoom[] = {CHAR_T, CHAR_R, CHAR_I, CHAR_C, CHAR_K, CHAR_R, CHAR_M, EOS};
static const u8 sBoxName_Rain[]      = {CHAR_R, CHAR_A, CHAR_I, CHAR_N, EOS};
static const u8 sBoxName_Sun[]       = {CHAR_S, CHAR_U, CHAR_N, EOS};
static const u8 sBoxName_Sand[]      = {CHAR_S, CHAR_A, CHAR_N, CHAR_D, EOS};

static const u8 *const sBoxNames[NUM_ARCHETYPES] = {
    sBoxName_Offense, sBoxName_Stall,  sBoxName_Balanced, sBoxName_Speed,
    sBoxName_TrickRoom, sBoxName_Rain, sBoxName_Sun,      sBoxName_Sand,
};

static void DoAddMon(struct Pokemon *mon,
                     u16 species, u8 level, u8 nature, u16 item,
                     u16 move1, u16 move2, u16 move3, u16 move4,
                     u8 hpEV, u8 atkEV, u8 defEV,
                     u8 speEV, u8 spAtkEV, u8 spDefEV)
{
    CreateMon(mon, species, level, 31, TRUE, (u32)nature, OT_ID_PLAYER_ID, 0);
    SetMonData(mon, MON_DATA_HELD_ITEM, &item);
    SetMonData(mon, MON_DATA_MOVE1,     &move1);
    SetMonData(mon, MON_DATA_MOVE2,     &move2);
    SetMonData(mon, MON_DATA_MOVE3,     &move3);
    SetMonData(mon, MON_DATA_MOVE4,     &move4);
    // PP_BONUSES is 2 bits per move (4 moves) — 0xFF = all 4 moves at max PP bonus
    {
        u8 ppMax = 0xFF;
        SetMonData(mon, MON_DATA_PP_BONUSES, &ppMax);
    }
    MonRestorePP(mon); // recalculates actual PP values using the bonuses above
    SetMonData(mon, MON_DATA_HP_EV,     &hpEV);
    SetMonData(mon, MON_DATA_ATK_EV,    &atkEV);
    SetMonData(mon, MON_DATA_DEF_EV,    &defEV);
    SetMonData(mon, MON_DATA_SPEED_EV,  &speEV);
    SetMonData(mon, MON_DATA_SPATK_EV,  &spAtkEV);
    SetMonData(mon, MON_DATA_SPDEF_EV,  &spDefEV);
    CalculateMonStats(mon);
}

static void AddPresetMon(u8 slot,
                         u16 species, u8 level, u8 nature, u16 item,
                         u16 move1, u16 move2, u16 move3, u16 move4,
                         u8 hpEV, u8 atkEV, u8 defEV,
                         u8 speEV, u8 spAtkEV, u8 spDefEV)
{
    DoAddMon(&gPlayerParty[slot], species, level, nature, item,
             move1, move2, move3, move4,
             hpEV, atkEV, defEV, speEV, spAtkEV, spDefEV);
}

// Single reusable buffer for building box mons — avoids large repeated stack allocations
// that agbcc can mishandle across a loop of 48 calls.
static struct Pokemon sBoxMonBuf;

static void AddBoxMon(u8 boxId, u8 boxSlot,
                      u16 species, u8 level, u8 nature, u16 item,
                      u16 move1, u16 move2, u16 move3, u16 move4,
                      u8 hpEV, u8 atkEV, u8 defEV,
                      u8 speEV, u8 spAtkEV, u8 spDefEV)
{
    DoAddMon(&sBoxMonBuf, species, level, nature, item,
             move1, move2, move3, move4,
             hpEV, atkEV, defEV, speEV, spAtkEV, spDefEV);
    SetBoxMonAt(boxId, boxSlot, &sBoxMonBuf.box);
}

static void SetPresetBoxName(u8 boxId, const u8 *name)
{
    u8 i;
    u8 *dest = GetBoxNamePtr(boxId);
    for (i = 0; i <= BOX_NAME_LENGTH; i++)
    {
        dest[i] = name[i];
        if (name[i] == EOS)
            break;
    }
    dest[BOX_NAME_LENGTH] = EOS;
}

/* Macro to reduce the 16-line party/box branching to one call site */
#define MON(slot_or_box, slotOrBoxSlot, ...) \
    do { \
        if (toParty) AddPresetMon((slot_or_box), __VA_ARGS__); \
        else         AddBoxMon((boxId), (slotOrBoxSlot), __VA_ARGS__); \
    } while (0)

/* -------------------------------------------------------------------------
   OFFENSE (Hyper Offense)
   Lv50: Choice Band Sala/Hera/Sneasel, Lum Gengar/Meta, Petaya Jolt
   Lv100: Lum Sala, Kings Rock Gengar, Salac Hera, Sitrus Star, Shell Bell Meta, Scope Lens Jolt
   ------------------------------------------------------------------------- */
static void FillOffense(u8 level, u8 boxId, u8 slotBase)
{
    u8 toParty = (boxId == 0xFF);
    u8 s = toParty ? 0 : slotBase;

    if (level == 50)
    {
        MON(s+0,s+0, SPECIES_SALAMENCE,50,NATURE_ADAMANT,ITEM_CHOICE_BAND,
            MOVE_ROCK_SLIDE,MOVE_EARTHQUAKE,MOVE_FIRE_BLAST,MOVE_BRICK_BREAK,
            4,252,0,252,0,0);
        MON(s+1,s+1, SPECIES_GENGAR,50,NATURE_TIMID,ITEM_LUM_BERRY,
            MOVE_THUNDERBOLT,MOVE_ICE_PUNCH,MOVE_FIRE_PUNCH,MOVE_EXPLOSION,
            4,0,0,252,252,0);
        MON(s+2,s+2, SPECIES_HERACROSS,50,NATURE_ADAMANT,ITEM_SALAC_BERRY,
            MOVE_SUBSTITUTE,MOVE_MEGAHORN,MOVE_BRICK_BREAK,MOVE_ROCK_SLIDE,
            4,252,0,252,0,0);
        MON(s+3,s+3, SPECIES_STARMIE,50,NATURE_TIMID,ITEM_SITRUS_BERRY,
            MOVE_SURF,MOVE_THUNDERBOLT,MOVE_ICE_BEAM,MOVE_PSYCHIC,
            4,0,0,252,252,0);
        MON(s+4,s+4, SPECIES_METAGROSS,50,NATURE_ADAMANT,ITEM_SHELL_BELL,
            MOVE_METEOR_MASH,MOVE_EARTHQUAKE,MOVE_ROCK_SLIDE,MOVE_EXPLOSION,
            176,252,0,80,0,0);
        MON(s+5,s+5, SPECIES_JOLTEON,50,NATURE_TIMID,ITEM_PETAYA_BERRY,
            MOVE_SUBSTITUTE,MOVE_THUNDERBOLT,MOVE_HIDDEN_POWER,MOVE_BATON_PASS,
            4,0,0,252,252,0);
    }
    else
    {
        MON(s+0,s+0, SPECIES_SALAMENCE,100,NATURE_ADAMANT,ITEM_LUM_BERRY,
            MOVE_ROCK_SLIDE,MOVE_EARTHQUAKE,MOVE_FIRE_BLAST,MOVE_BRICK_BREAK,
            4,252,0,252,0,0);
        MON(s+1,s+1, SPECIES_GENGAR,100,NATURE_TIMID,ITEM_PETAYA_BERRY,
            MOVE_THUNDERBOLT,MOVE_ICE_PUNCH,MOVE_FIRE_PUNCH,MOVE_EXPLOSION,
            4,0,0,252,252,0);
        MON(s+2,s+2, SPECIES_HERACROSS,100,NATURE_ADAMANT,ITEM_SALAC_BERRY,
            MOVE_SUBSTITUTE,MOVE_MEGAHORN,MOVE_BRICK_BREAK,MOVE_ROCK_SLIDE,
            4,252,0,252,0,0);
        MON(s+3,s+3, SPECIES_STARMIE,100,NATURE_TIMID,ITEM_SITRUS_BERRY,
            MOVE_SURF,MOVE_THUNDERBOLT,MOVE_ICE_BEAM,MOVE_PSYCHIC,
            4,0,0,252,252,0);
        MON(s+4,s+4, SPECIES_METAGROSS,100,NATURE_ADAMANT,ITEM_CHOICE_BAND,
            MOVE_METEOR_MASH,MOVE_EARTHQUAKE,MOVE_ROCK_SLIDE,MOVE_EXPLOSION,
            176,252,0,80,0,0);
        MON(s+5,s+5, SPECIES_JOLTEON,100,NATURE_TIMID,ITEM_LEFTOVERS,
            MOVE_THUNDERBOLT,MOVE_HIDDEN_POWER,MOVE_SHADOW_BALL,MOVE_BATON_PASS,
            4,0,0,252,252,0);
    }
    if (toParty) gPlayerPartyCount = PRESET_PARTY_SIZE;
}

/* -------------------------------------------------------------------------
   STALL
   Lv50: All Leftovers (6 unique since each mon is different)
   Lv100: Liechi Skar, Sitrus Bliss, Lum Umbr, Persim Quag, Wiki Dusc, Aguav Clay
   ------------------------------------------------------------------------- */
static void FillStall(u8 level, u8 boxId, u8 slotBase)
{
    u8 toParty = (boxId == 0xFF);
    u8 s = toParty ? 0 : slotBase;

    if (level == 50)
    {
        MON(s+0,s+0, SPECIES_SKARMORY,50,NATURE_IMPISH,ITEM_LEFTOVERS,
            MOVE_SPIKES,MOVE_WHIRLWIND,MOVE_AERIAL_ACE,MOVE_TOXIC,
            252,0,252,0,0,4);
        MON(s+1,s+1, SPECIES_BLISSEY,50,NATURE_CALM,ITEM_SITRUS_BERRY,
            MOVE_SOFT_BOILED,MOVE_TOXIC,MOVE_SEISMIC_TOSS,MOVE_ICE_BEAM,
            252,0,4,0,0,252);
        MON(s+2,s+2, SPECIES_UMBREON,50,NATURE_CALM,ITEM_LUM_BERRY,
            MOVE_MOONLIGHT,MOVE_TOXIC,MOVE_MEAN_LOOK,MOVE_BATON_PASS,
            252,0,4,0,0,252);
        MON(s+3,s+3, SPECIES_QUAGSIRE,50,NATURE_RELAXED,ITEM_SHELL_BELL,
            MOVE_EARTHQUAKE,MOVE_YAWN,MOVE_RECOVER,MOVE_ICE_BEAM,
            252,0,252,0,0,4);
        MON(s+4,s+4, SPECIES_DUSCLOPS,50,NATURE_CAREFUL,ITEM_BRIGHT_POWDER,
            MOVE_WILL_O_WISP,MOVE_SHADOW_BALL,MOVE_PAIN_SPLIT,MOVE_PROTECT,
            252,0,4,0,0,252);
        MON(s+5,s+5, SPECIES_CLAYDOL,50,NATURE_BOLD,ITEM_CHESTO_BERRY,
            MOVE_RAPID_SPIN,MOVE_EARTHQUAKE,MOVE_ICE_BEAM,MOVE_COSMIC_POWER,
            252,0,252,0,0,4);
    }
    else
    {
        MON(s+0,s+0, SPECIES_SKARMORY,100,NATURE_IMPISH,ITEM_LEFTOVERS,
            MOVE_SPIKES,MOVE_WHIRLWIND,MOVE_AERIAL_ACE,MOVE_TOXIC,
            252,0,252,0,0,4);
        MON(s+1,s+1, SPECIES_BLISSEY,100,NATURE_CALM,ITEM_SITRUS_BERRY,
            MOVE_SOFT_BOILED,MOVE_TOXIC,MOVE_SEISMIC_TOSS,MOVE_ICE_BEAM,
            252,0,4,0,0,252);
        MON(s+2,s+2, SPECIES_UMBREON,100,NATURE_CALM,ITEM_LUM_BERRY,
            MOVE_MOONLIGHT,MOVE_TOXIC,MOVE_MEAN_LOOK,MOVE_BATON_PASS,
            252,0,4,0,0,252);
        MON(s+3,s+3, SPECIES_QUAGSIRE,100,NATURE_RELAXED,ITEM_SHELL_BELL,
            MOVE_EARTHQUAKE,MOVE_YAWN,MOVE_RECOVER,MOVE_ICE_BEAM,
            252,0,252,0,0,4);
        MON(s+4,s+4, SPECIES_DUSCLOPS,100,NATURE_CAREFUL,ITEM_BRIGHT_POWDER,
            MOVE_WILL_O_WISP,MOVE_SHADOW_BALL,MOVE_PAIN_SPLIT,MOVE_PROTECT,
            252,0,4,0,0,252);
        MON(s+5,s+5, SPECIES_CLAYDOL,100,NATURE_BOLD,ITEM_CHESTO_BERRY,
            MOVE_RAPID_SPIN,MOVE_EARTHQUAKE,MOVE_ICE_BEAM,MOVE_COSMIC_POWER,
            252,0,252,0,0,4);
    }
    if (toParty) gPlayerPartyCount = PRESET_PARTY_SIZE;
}

/* -------------------------------------------------------------------------
   BALANCED
   Lv50: CB Flyg, Leftovers Milo/Snor, Lum Meta/Gen/Brel
   Lv100: Lum Flyg, Sitrus Milo, Shell Bell Meta, Wiki Snor, Kings Rock Gen, Salac Brel
   ------------------------------------------------------------------------- */
static void FillBalanced(u8 level, u8 boxId, u8 slotBase)
{
    u8 toParty = (boxId == 0xFF);
    u8 s = toParty ? 0 : slotBase;

    if (level == 50)
    {
        MON(s+0,s+0, SPECIES_FLYGON,50,NATURE_ADAMANT,ITEM_CHOICE_BAND,
            MOVE_EARTHQUAKE,MOVE_ROCK_SLIDE,MOVE_DRAGON_CLAW,MOVE_FIRE_BLAST,
            4,252,0,252,0,0);
        MON(s+1,s+1, SPECIES_MILOTIC,50,NATURE_BOLD,ITEM_LEFTOVERS,
            MOVE_SURF,MOVE_ICE_BEAM,MOVE_RECOVER,MOVE_TOXIC,
            252,0,252,0,4,0);
        MON(s+2,s+2, SPECIES_METAGROSS,50,NATURE_ADAMANT,ITEM_LUM_BERRY,
            MOVE_METEOR_MASH,MOVE_EARTHQUAKE,MOVE_ROCK_SLIDE,MOVE_EXPLOSION,
            176,252,0,80,0,0);
        MON(s+3,s+3, SPECIES_SNORLAX,50,NATURE_CAREFUL,ITEM_SITRUS_BERRY,
            MOVE_BODY_SLAM,MOVE_EARTHQUAKE,MOVE_REST,MOVE_SLEEP_TALK,
            252,4,0,0,0,252);
        MON(s+4,s+4, SPECIES_GENGAR,50,NATURE_TIMID,ITEM_PETAYA_BERRY,
            MOVE_THUNDERBOLT,MOVE_ICE_PUNCH,MOVE_FIRE_PUNCH,MOVE_EXPLOSION,
            4,0,0,252,252,0);
        MON(s+5,s+5, SPECIES_BRELOOM,50,NATURE_ADAMANT,ITEM_SALAC_BERRY,
            MOVE_SUBSTITUTE,MOVE_SPORE,MOVE_FOCUS_PUNCH,MOVE_ROCK_SLIDE,
            4,252,0,252,0,0);
    }
    else
    {
        MON(s+0,s+0, SPECIES_FLYGON,100,NATURE_ADAMANT,ITEM_LUM_BERRY,
            MOVE_EARTHQUAKE,MOVE_ROCK_SLIDE,MOVE_DRAGON_CLAW,MOVE_FIRE_BLAST,
            4,252,0,252,0,0);
        MON(s+1,s+1, SPECIES_MILOTIC,100,NATURE_BOLD,ITEM_SITRUS_BERRY,
            MOVE_SURF,MOVE_ICE_BEAM,MOVE_RECOVER,MOVE_TOXIC,
            252,0,252,0,4,0);
        MON(s+2,s+2, SPECIES_METAGROSS,100,NATURE_ADAMANT,ITEM_SHELL_BELL,
            MOVE_METEOR_MASH,MOVE_EARTHQUAKE,MOVE_ROCK_SLIDE,MOVE_EXPLOSION,
            176,252,0,80,0,0);
        MON(s+3,s+3, SPECIES_SNORLAX,100,NATURE_CAREFUL,ITEM_LEFTOVERS,
            MOVE_BODY_SLAM,MOVE_EARTHQUAKE,MOVE_REST,MOVE_SLEEP_TALK,
            252,4,0,0,0,252);
        MON(s+4,s+4, SPECIES_GENGAR,100,NATURE_TIMID,ITEM_PETAYA_BERRY,
            MOVE_THUNDERBOLT,MOVE_ICE_PUNCH,MOVE_FIRE_PUNCH,MOVE_EXPLOSION,
            4,0,0,252,252,0);
        MON(s+5,s+5, SPECIES_BRELOOM,100,NATURE_ADAMANT,ITEM_SALAC_BERRY,
            MOVE_SUBSTITUTE,MOVE_SPORE,MOVE_FOCUS_PUNCH,MOVE_ROCK_SLIDE,
            4,252,0,252,0,0);
    }
    if (toParty) gPlayerPartyCount = PRESET_PARTY_SIZE;
}

/* -------------------------------------------------------------------------
   SPEED CONTROL
   Lv50: CB Aero/Dug/Sneas, Lum Crob/Ninj, Salac Elec
   Lv100: Scope Aero, Lum Dug, Kings Rock Crob, Petaya Elec, White Herb Ninj, Quick Claw Sneas
   ------------------------------------------------------------------------- */
static void FillSpeed(u8 level, u8 boxId, u8 slotBase)
{
    u8 toParty = (boxId == 0xFF);
    u8 s = toParty ? 0 : slotBase;

    if (level == 50)
    {
        MON(s+0,s+0, SPECIES_AERODACTYL,50,NATURE_JOLLY,ITEM_CHOICE_BAND,
            MOVE_ROCK_SLIDE,MOVE_EARTHQUAKE,MOVE_AERIAL_ACE,MOVE_DOUBLE_EDGE,
            4,252,0,252,0,0);
        MON(s+1,s+1, SPECIES_DUGTRIO,50,NATURE_JOLLY,ITEM_LUM_BERRY,
            MOVE_EARTHQUAKE,MOVE_ROCK_SLIDE,MOVE_HIDDEN_POWER,MOVE_AERIAL_ACE,
            4,252,0,252,0,0);
        MON(s+2,s+2, SPECIES_CROBAT,50,NATURE_JOLLY,ITEM_BRIGHT_POWDER,
            MOVE_SLUDGE_BOMB,MOVE_SHADOW_BALL,MOVE_AERIAL_ACE,MOVE_TAUNT,
            4,252,0,252,0,0);
        MON(s+3,s+3, SPECIES_ELECTRODE,50,NATURE_TIMID,ITEM_SALAC_BERRY,
            MOVE_SUBSTITUTE,MOVE_THUNDERBOLT,MOVE_HIDDEN_POWER,MOVE_EXPLOSION,
            4,0,0,252,252,0);
        MON(s+4,s+4, SPECIES_NINJASK,50,NATURE_JOLLY,ITEM_SITRUS_BERRY,
            MOVE_SUBSTITUTE,MOVE_SWORDS_DANCE,MOVE_BATON_PASS,MOVE_AERIAL_ACE,
            4,252,0,252,0,0);
        MON(s+5,s+5, SPECIES_SNEASEL,50,NATURE_JOLLY,ITEM_SCOPE_LENS,
            MOVE_SHADOW_BALL,MOVE_BRICK_BREAK,MOVE_ROCK_SLIDE,MOVE_ICE_PUNCH,
            4,252,0,252,0,0);
    }
    else
    {
        MON(s+0,s+0, SPECIES_AERODACTYL,100,NATURE_JOLLY,ITEM_SCOPE_LENS,
            MOVE_ROCK_SLIDE,MOVE_EARTHQUAKE,MOVE_AERIAL_ACE,MOVE_DOUBLE_EDGE,
            4,252,0,252,0,0);
        MON(s+1,s+1, SPECIES_DUGTRIO,100,NATURE_JOLLY,ITEM_LUM_BERRY,
            MOVE_EARTHQUAKE,MOVE_ROCK_SLIDE,MOVE_HIDDEN_POWER,MOVE_AERIAL_ACE,
            4,252,0,252,0,0);
        MON(s+2,s+2, SPECIES_CROBAT,100,NATURE_JOLLY,ITEM_KINGS_ROCK,
            MOVE_SLUDGE_BOMB,MOVE_SHADOW_BALL,MOVE_AERIAL_ACE,MOVE_TAUNT,
            4,252,0,252,0,0);
        MON(s+3,s+3, SPECIES_ELECTRODE,100,NATURE_TIMID,ITEM_PETAYA_BERRY,
            MOVE_SUBSTITUTE,MOVE_THUNDERBOLT,MOVE_HIDDEN_POWER,MOVE_EXPLOSION,
            4,0,0,252,252,0);
        MON(s+4,s+4, SPECIES_NINJASK,100,NATURE_JOLLY,ITEM_SITRUS_BERRY,
            MOVE_SUBSTITUTE,MOVE_SWORDS_DANCE,MOVE_BATON_PASS,MOVE_AERIAL_ACE,
            4,252,0,252,0,0);
        MON(s+5,s+5, SPECIES_SNEASEL,100,NATURE_JOLLY,ITEM_CHOICE_BAND,
            MOVE_SHADOW_BALL,MOVE_BRICK_BREAK,MOVE_ROCK_SLIDE,MOVE_ICE_PUNCH,
            4,252,0,252,0,0);
    }
    if (toParty) gPlayerPartyCount = PRESET_PARTY_SIZE;
}

/* -------------------------------------------------------------------------
   TRICK ROOM
   Lv50: Lum Dusc, Leftovers Pory2/Tork, CB Hari, Thick Club Maro, Lum Mawi
   Lv100: Sitrus Dusc, Shell Bell Pory2, Scope Hari, Thick Club Maro, Wiki Tork, Aguav Mawi
   Note: Marowak always gets Thick Club — it's mandatory.
   ------------------------------------------------------------------------- */
static void FillTrickRoom(u8 level, u8 boxId, u8 slotBase)
{
    u8 toParty = (boxId == 0xFF);
    u8 s = toParty ? 0 : slotBase;

    if (level == 50)
    {
        MON(s+0,s+0, SPECIES_DUSCLOPS,50,NATURE_QUIET,ITEM_LUM_BERRY,
            MOVE_WILL_O_WISP,MOVE_SHADOW_BALL,MOVE_PAIN_SPLIT,MOVE_FIRE_PUNCH,
            252,0,4,0,252,0);
        MON(s+1,s+1, SPECIES_PORYGON2,50,NATURE_QUIET,ITEM_LEFTOVERS,
            MOVE_THUNDERBOLT,MOVE_ICE_BEAM,MOVE_RECOVER,MOVE_THUNDER_WAVE,
            252,0,4,0,252,0);
        MON(s+2,s+2, SPECIES_HARIYAMA,50,NATURE_BRAVE,ITEM_CHOICE_BAND,
            MOVE_BRICK_BREAK,MOVE_ROCK_SLIDE,MOVE_SHADOW_BALL,MOVE_FAKE_OUT,
            252,252,4,0,0,0);
        MON(s+3,s+3, SPECIES_MAROWAK,50,NATURE_BRAVE,ITEM_THICK_CLUB,
            MOVE_EARTHQUAKE,MOVE_ROCK_SLIDE,MOVE_FIRE_PUNCH,MOVE_SWORDS_DANCE,
            252,252,4,0,0,0);
        MON(s+4,s+4, SPECIES_TORKOAL,50,NATURE_QUIET,ITEM_SITRUS_BERRY,
            MOVE_FLAMETHROWER,MOVE_OVERHEAT,MOVE_AMNESIA,MOVE_PROTECT,
            252,0,4,0,252,0);
        MON(s+5,s+5, SPECIES_MAWILE,50,NATURE_BRAVE,ITEM_SHELL_BELL,
            MOVE_SWORDS_DANCE,MOVE_RETURN,MOVE_SHADOW_BALL,MOVE_BRICK_BREAK,
            252,252,4,0,0,0);
    }
    else
    {
        MON(s+0,s+0, SPECIES_DUSCLOPS,100,NATURE_QUIET,ITEM_SITRUS_BERRY,
            MOVE_WILL_O_WISP,MOVE_SHADOW_BALL,MOVE_PAIN_SPLIT,MOVE_FIRE_PUNCH,
            252,0,4,0,252,0);
        MON(s+1,s+1, SPECIES_PORYGON2,100,NATURE_QUIET,ITEM_SHELL_BELL,
            MOVE_THUNDERBOLT,MOVE_ICE_BEAM,MOVE_RECOVER,MOVE_THUNDER_WAVE,
            252,0,4,0,252,0);
        MON(s+2,s+2, SPECIES_HARIYAMA,100,NATURE_BRAVE,ITEM_SCOPE_LENS,
            MOVE_BRICK_BREAK,MOVE_ROCK_SLIDE,MOVE_SHADOW_BALL,MOVE_FAKE_OUT,
            252,252,4,0,0,0);
        MON(s+3,s+3, SPECIES_MAROWAK,100,NATURE_BRAVE,ITEM_THICK_CLUB,
            MOVE_EARTHQUAKE,MOVE_ROCK_SLIDE,MOVE_FIRE_PUNCH,MOVE_SWORDS_DANCE,
            252,252,4,0,0,0);
        MON(s+4,s+4, SPECIES_TORKOAL,100,NATURE_QUIET,ITEM_LEFTOVERS,
            MOVE_FLAMETHROWER,MOVE_OVERHEAT,MOVE_AMNESIA,MOVE_PROTECT,
            252,0,4,0,252,0);
        MON(s+5,s+5, SPECIES_MAWILE,100,NATURE_BRAVE,ITEM_LUM_BERRY,
            MOVE_SWORDS_DANCE,MOVE_RETURN,MOVE_SHADOW_BALL,MOVE_BRICK_BREAK,
            252,252,4,0,0,0);
    }
    if (toParty) gPlayerPartyCount = PRESET_PARTY_SIZE;
}

/* -------------------------------------------------------------------------
   RAIN DANCE
   Lv50: Lum Ludi/King/Kabu, Leftovers Lant/Peli/Tent
   Lv100: Sitrus Ludi, Scope King, Salac Kabu, Shell Bell Lant, Wiki Peli, Aguav Tent
   ------------------------------------------------------------------------- */
static void FillRain(u8 level, u8 boxId, u8 slotBase)
{
    u8 toParty = (boxId == 0xFF);
    u8 s = toParty ? 0 : slotBase;

    if (level == 50)
    {
        MON(s+0,s+0, SPECIES_LUDICOLO,50,NATURE_MODEST,ITEM_LUM_BERRY,
            MOVE_SURF,MOVE_ICE_BEAM,MOVE_RAIN_DANCE,MOVE_LEECH_SEED,
            4,0,0,252,252,0);
        MON(s+1,s+1, SPECIES_KINGDRA,50,NATURE_MODEST,ITEM_SHELL_BELL,
            MOVE_DRAGON_DANCE,MOVE_SURF,MOVE_ICE_BEAM,MOVE_SUBSTITUTE,
            4,0,0,252,252,0);
        MON(s+2,s+2, SPECIES_KABUTOPS,50,NATURE_ADAMANT,ITEM_SALAC_BERRY,
            MOVE_WATERFALL,MOVE_ROCK_SLIDE,MOVE_BRICK_BREAK,MOVE_SWORDS_DANCE,
            4,252,0,252,0,0);
        MON(s+3,s+3, SPECIES_LANTURN,50,NATURE_CALM,ITEM_LEFTOVERS,
            MOVE_SURF,MOVE_THUNDER,MOVE_RAIN_DANCE,MOVE_ICE_BEAM,
            252,0,0,0,4,252);
        MON(s+4,s+4, SPECIES_PELIPPER,50,NATURE_BOLD,ITEM_SITRUS_BERRY,
            MOVE_SURF,MOVE_ICE_BEAM,MOVE_RAIN_DANCE,MOVE_PROTECT,
            252,0,252,0,4,0);
        MON(s+5,s+5, SPECIES_TENTACRUEL,50,NATURE_CALM,ITEM_BRIGHT_POWDER,
            MOVE_SURF,MOVE_ICE_BEAM,MOVE_SLUDGE_BOMB,MOVE_RAPID_SPIN,
            252,0,0,0,4,252);
    }
    else
    {
        MON(s+0,s+0, SPECIES_LUDICOLO,100,NATURE_MODEST,ITEM_SITRUS_BERRY,
            MOVE_SURF,MOVE_ICE_BEAM,MOVE_RAIN_DANCE,MOVE_LEECH_SEED,
            4,0,0,252,252,0);
        MON(s+1,s+1, SPECIES_KINGDRA,100,NATURE_MODEST,ITEM_LUM_BERRY,
            MOVE_DRAGON_DANCE,MOVE_SURF,MOVE_ICE_BEAM,MOVE_SUBSTITUTE,
            4,0,0,252,252,0);
        MON(s+2,s+2, SPECIES_KABUTOPS,100,NATURE_ADAMANT,ITEM_SALAC_BERRY,
            MOVE_WATERFALL,MOVE_ROCK_SLIDE,MOVE_BRICK_BREAK,MOVE_SWORDS_DANCE,
            4,252,0,252,0,0);
        MON(s+3,s+3, SPECIES_LANTURN,100,NATURE_CALM,ITEM_SHELL_BELL,
            MOVE_SURF,MOVE_THUNDER,MOVE_RAIN_DANCE,MOVE_ICE_BEAM,
            252,0,0,0,4,252);
        MON(s+4,s+4, SPECIES_PELIPPER,100,NATURE_BOLD,ITEM_LEFTOVERS,
            MOVE_SURF,MOVE_ICE_BEAM,MOVE_RAIN_DANCE,MOVE_PROTECT,
            252,0,252,0,4,0);
        MON(s+5,s+5, SPECIES_TENTACRUEL,100,NATURE_CALM,ITEM_BRIGHT_POWDER,
            MOVE_SURF,MOVE_ICE_BEAM,MOVE_SLUDGE_BOMB,MOVE_RAPID_SPIN,
            252,0,0,0,4,252);
    }
    if (toParty) gPlayerPartyCount = PRESET_PARTY_SIZE;
}

/* -------------------------------------------------------------------------
   SUN
   Lv50: Lum Nine/Venu/Flyg/Arma, Petaya Char, Lum Vict (all unique by species)
   Lv100: White Herb Nine, Sitrus Venu, Scope Char, Kings Rock Vict, Shell Bell Flyg, Salac Arma
   ------------------------------------------------------------------------- */
static void FillSun(u8 level, u8 boxId, u8 slotBase)
{
    u8 toParty = (boxId == 0xFF);
    u8 s = toParty ? 0 : slotBase;

    if (level == 50)
    {
        MON(s+0,s+0, SPECIES_NINETALES,50,NATURE_TIMID,ITEM_LUM_BERRY,
            MOVE_FIRE_BLAST,MOVE_SUNNY_DAY,MOVE_SUBSTITUTE,MOVE_WILL_O_WISP,
            4,0,0,252,252,0);
        MON(s+1,s+1, SPECIES_VENUSAUR,50,NATURE_MODEST,ITEM_SHELL_BELL,
            MOVE_SOLAR_BEAM,MOVE_SLUDGE_BOMB,MOVE_SUNNY_DAY,MOVE_SLEEP_POWDER,
            4,0,0,252,252,0);
        MON(s+2,s+2, SPECIES_CHARIZARD,50,NATURE_TIMID,ITEM_SCOPE_LENS,
            MOVE_FIRE_BLAST,MOVE_SOLAR_BEAM,MOVE_EARTHQUAKE,MOVE_BELLY_DRUM,
            4,0,0,252,252,0);
        MON(s+3,s+3, SPECIES_VICTREEBEL,50,NATURE_MODEST,ITEM_SITRUS_BERRY,
            MOVE_SOLAR_BEAM,MOVE_SLUDGE_BOMB,MOVE_SYNTHESIS,MOVE_SLEEP_POWDER,
            4,0,0,252,252,0);
        MON(s+4,s+4, SPECIES_FLYGON,50,NATURE_ADAMANT,ITEM_CHOICE_BAND,
            MOVE_EARTHQUAKE,MOVE_ROCK_SLIDE,MOVE_DRAGON_CLAW,MOVE_FIRE_BLAST,
            4,252,0,252,0,0);
        MON(s+5,s+5, SPECIES_ARMALDO,50,NATURE_ADAMANT,ITEM_SALAC_BERRY,
            MOVE_ROCK_BLAST,MOVE_BRICK_BREAK,MOVE_SWORDS_DANCE,MOVE_AERIAL_ACE,
            176,252,0,80,0,0);
    }
    else
    {
        MON(s+0,s+0, SPECIES_NINETALES,100,NATURE_TIMID,ITEM_LUM_BERRY,
            MOVE_FIRE_BLAST,MOVE_SUNNY_DAY,MOVE_SUBSTITUTE,MOVE_WILL_O_WISP,
            4,0,0,252,252,0);
        MON(s+1,s+1, SPECIES_VENUSAUR,100,NATURE_MODEST,ITEM_SITRUS_BERRY,
            MOVE_SOLAR_BEAM,MOVE_SLUDGE_BOMB,MOVE_SUNNY_DAY,MOVE_SLEEP_POWDER,
            4,0,0,252,252,0);
        MON(s+2,s+2, SPECIES_CHARIZARD,100,NATURE_TIMID,ITEM_SCOPE_LENS,
            MOVE_FIRE_BLAST,MOVE_SOLAR_BEAM,MOVE_EARTHQUAKE,MOVE_BELLY_DRUM,
            4,0,0,252,252,0);
        MON(s+3,s+3, SPECIES_VICTREEBEL,100,NATURE_MODEST,ITEM_BRIGHT_POWDER,
            MOVE_SOLAR_BEAM,MOVE_SLUDGE_BOMB,MOVE_SYNTHESIS,MOVE_SLEEP_POWDER,
            4,0,0,252,252,0);
        MON(s+4,s+4, SPECIES_FLYGON,100,NATURE_ADAMANT,ITEM_SHELL_BELL,
            MOVE_EARTHQUAKE,MOVE_ROCK_SLIDE,MOVE_DRAGON_CLAW,MOVE_FIRE_BLAST,
            4,252,0,252,0,0);
        MON(s+5,s+5, SPECIES_ARMALDO,100,NATURE_ADAMANT,ITEM_SALAC_BERRY,
            MOVE_ROCK_BLAST,MOVE_BRICK_BREAK,MOVE_SWORDS_DANCE,MOVE_AERIAL_ACE,
            176,252,0,80,0,0);
    }
    if (toParty) gPlayerPartyCount = PRESET_PARTY_SIZE;
}

/* -------------------------------------------------------------------------
   SAND
   Lv50: Lum Ttar/Cact/Arma, CB Flyg, Leftovers Clay/Steel
   Lv100: Scope Ttar, Shell Bell Flyg, Sitrus Clay, Wiki Steel, Salac Cact, Kings Rock Arma
   ------------------------------------------------------------------------- */
static void FillSand(u8 level, u8 boxId, u8 slotBase)
{
    u8 toParty = (boxId == 0xFF);
    u8 s = toParty ? 0 : slotBase;

    if (level == 50)
    {
        MON(s+0,s+0, SPECIES_TYRANITAR,50,NATURE_ADAMANT,ITEM_LUM_BERRY,
            MOVE_ROCK_SLIDE,MOVE_CRUNCH,MOVE_EARTHQUAKE,MOVE_DRAGON_DANCE,
            4,252,0,252,0,0);
        MON(s+1,s+1, SPECIES_FLYGON,50,NATURE_ADAMANT,ITEM_CHOICE_BAND,
            MOVE_EARTHQUAKE,MOVE_ROCK_SLIDE,MOVE_DRAGON_CLAW,MOVE_FIRE_BLAST,
            4,252,0,252,0,0);
        MON(s+2,s+2, SPECIES_CLAYDOL,50,NATURE_BOLD,ITEM_SHELL_BELL,
            MOVE_RAPID_SPIN,MOVE_EARTHQUAKE,MOVE_ICE_BEAM,MOVE_EXPLOSION,
            252,0,252,0,0,4);
        MON(s+3,s+3, SPECIES_STEELIX,50,NATURE_IMPISH,ITEM_LEFTOVERS,
            MOVE_EARTHQUAKE,MOVE_ROCK_SLIDE,MOVE_IRON_TAIL,MOVE_ROAR,
            252,4,252,0,0,0);
        MON(s+4,s+4, SPECIES_CACTURNE,50,NATURE_ADAMANT,ITEM_FOCUS_BAND,
            MOVE_SPIKES,MOVE_DESTINY_BOND,MOVE_FAINT_ATTACK,MOVE_NEEDLE_ARM,
            4,252,0,252,0,0);
        MON(s+5,s+5, SPECIES_ARMALDO,50,NATURE_ADAMANT,ITEM_KINGS_ROCK,
            MOVE_ROCK_BLAST,MOVE_BRICK_BREAK,MOVE_SWORDS_DANCE,MOVE_AERIAL_ACE,
            176,252,0,80,0,0);
    }
    else
    {
        MON(s+0,s+0, SPECIES_TYRANITAR,100,NATURE_ADAMANT,ITEM_SCOPE_LENS,
            MOVE_ROCK_SLIDE,MOVE_CRUNCH,MOVE_EARTHQUAKE,MOVE_DRAGON_DANCE,
            4,252,0,252,0,0);
        MON(s+1,s+1, SPECIES_FLYGON,100,NATURE_ADAMANT,ITEM_SHELL_BELL,
            MOVE_EARTHQUAKE,MOVE_ROCK_SLIDE,MOVE_DRAGON_CLAW,MOVE_FIRE_BLAST,
            4,252,0,252,0,0);
        MON(s+2,s+2, SPECIES_CLAYDOL,100,NATURE_BOLD,ITEM_SITRUS_BERRY,
            MOVE_RAPID_SPIN,MOVE_EARTHQUAKE,MOVE_ICE_BEAM,MOVE_EXPLOSION,
            252,0,252,0,0,4);
        MON(s+3,s+3, SPECIES_STEELIX,100,NATURE_IMPISH,ITEM_LEFTOVERS,
            MOVE_EARTHQUAKE,MOVE_ROCK_SLIDE,MOVE_IRON_TAIL,MOVE_ROAR,
            252,4,252,0,0,0);
        MON(s+4,s+4, SPECIES_CACTURNE,100,NATURE_ADAMANT,ITEM_FOCUS_BAND,
            MOVE_SPIKES,MOVE_DESTINY_BOND,MOVE_FAINT_ATTACK,MOVE_NEEDLE_ARM,
            4,252,0,252,0,0);
        MON(s+5,s+5, SPECIES_ARMALDO,100,NATURE_ADAMANT,ITEM_KINGS_ROCK,
            MOVE_ROCK_BLAST,MOVE_BRICK_BREAK,MOVE_SWORDS_DANCE,MOVE_AERIAL_ACE,
            176,252,0,80,0,0);
    }
    if (toParty) gPlayerPartyCount = PRESET_PARTY_SIZE;
}

/* -------------------------------------------------------------------------
   Dispatch table
   ------------------------------------------------------------------------- */
typedef void (*ArchetypeFiller)(u8 level, u8 boxId, u8 slotBase);

static const ArchetypeFiller sFillers[NUM_ARCHETYPES] = {
    FillOffense, FillStall,    FillBalanced, FillSpeed,
    FillTrickRoom, FillRain,   FillSun,      FillSand,
};

/* Forward declaration for FillLegendaryBox which is defined after GivePresetPartyAndFillBoxes */
static void FillLegendaryBox(u8 level, u8 boxId);

/* -------------------------------------------------------------------------
   Public entry point — called from script via `special GivePresetPartyAndFillBoxes`
   Script sets VAR_0x8004 = archetype index, VAR_0x8005 = level (50 or 100).

   Party: chosen archetype at chosen level.
   Boxes 0-7: each archetype's ALTERNATE level (the one not chosen for party).
   So if you pick Offense lv50, Box 0 "OFFENSE" has the lv100 Offense team, and
   all other boxes also have their lv100 teams.
   ------------------------------------------------------------------------- */
void GivePresetPartyAndFillBoxes(void)
{
    u8 archetype = (u8)gSpecialVar_0x8004;
    u8 level     = (gSpecialVar_0x8005 == 100) ? 100 : 50;
    u8 i;

    if (archetype >= NUM_ARCHETYPES)
        archetype = 0;

    /* Fill party with chosen archetype + level */
    sFillers[archetype](level, 0xFF, 0);

    /* Fill all 8 boxes: lv50 in slots 0-5, lv100 in slots 6-11 */
    for (i = 0; i < NUM_ARCHETYPES; i++)
    {
        SetPresetBoxName(i, sBoxNames[i]);
        sFillers[i](50,  i, 0);  /* slots 0-5  */
        sFillers[i](100, i, 6);  /* slots 6-11 */
    }

    /* Box 8: all legendaries/pseudos at lv50 (slots 0-22) */
    FillLegendaryBox(50, 8);
    /* Box 9: all legendaries/pseudos at lv100 (slots 0-22) */
    FillLegendaryBox(100, 9);
}

/* =========================================================================
   LEGENDARY & PSEUDO-LEGENDARY BOX SETS
   Box 8 = lv50, Box 9 = lv100. 23 mons per box, slots 0-22.
   Pseudos included: Dragonite (not in any preset team).
   Tyranitar, Salamence, Metagross already appear in preset teams but are
   included here too for completeness.
   ========================================================================= */

// "LEGND50" and "LEGND100" — 8 char max, so "LGD LV50" / "LGD L100"
static const u8 sBoxName_Legd50[]  = {CHAR_L,CHAR_G,CHAR_D,CHAR_SPACE,CHAR_L,CHAR_V,CHAR_5,CHAR_0,EOS};
static const u8 sBoxName_Legd100[] = {CHAR_L,CHAR_G,CHAR_D,CHAR_SPACE,CHAR_L,CHAR_1,CHAR_0,CHAR_0,EOS};

static void FillLegendaryBox(u8 level, u8 boxId)
{
    SetPresetBoxName(boxId, (level == 50) ? sBoxName_Legd50 : sBoxName_Legd100);

    /* --- Pseudo-legendaries --- */

    /* 0: Dragonite | Adamant | Dragon Dance sweeper
       Lv50: Choice Band  Lv100: Lum Berry */
    AddBoxMon(boxId, 0, SPECIES_DRAGONITE, level, NATURE_ADAMANT,
        (level==50) ? ITEM_CHOICE_BAND : ITEM_LUM_BERRY,
        MOVE_DRAGON_CLAW, MOVE_EARTHQUAKE, MOVE_AERIAL_ACE, MOVE_DRAGON_DANCE,
        4, 252, 0, 252, 0, 0);

    /* --- Gen 1 legendaries --- */

    /* 1: Articuno | Bold | Calm Mind stall
       Lv50: Leftovers  Lv100: Sitrus Berry */
    AddBoxMon(boxId, 1, SPECIES_ARTICUNO, level, NATURE_BOLD,
        (level==50) ? ITEM_LEFTOVERS : ITEM_SITRUS_BERRY,
        MOVE_ICE_BEAM, MOVE_CALM_MIND, MOVE_SUBSTITUTE, MOVE_RECOVER,
        252, 0, 252, 0, 4, 0);

    /* 2: Zapdos | Timid | Offensive pivot
       Lv50: Lum Berry  Lv100: Petaya Berry */
    AddBoxMon(boxId, 2, SPECIES_ZAPDOS, level, NATURE_TIMID,
        (level==50) ? ITEM_LUM_BERRY : ITEM_PETAYA_BERRY,
        MOVE_THUNDERBOLT, MOVE_DRILL_PECK, MOVE_TOXIC, MOVE_THUNDER_WAVE,
        4, 0, 0, 252, 252, 0);

    /* 3: Moltres | Modest | Sun sweeper
       Lv50: Lum Berry  Lv100: Petaya Berry */
    AddBoxMon(boxId, 3, SPECIES_MOLTRES, level, NATURE_MODEST,
        (level==50) ? ITEM_LUM_BERRY : ITEM_PETAYA_BERRY,
        MOVE_FIRE_BLAST, MOVE_AERIAL_ACE, MOVE_FLAMETHROWER, MOVE_SUNNY_DAY,
        4, 0, 0, 252, 252, 0);

    /* 4: Mewtwo | Timid | Calm Mind sweeper
       Lv50: Lum Berry  Lv100: Petaya Berry */
    AddBoxMon(boxId, 4, SPECIES_MEWTWO, level, NATURE_TIMID,
        (level==50) ? ITEM_LUM_BERRY : ITEM_PETAYA_BERRY,
        MOVE_PSYCHIC, MOVE_CALM_MIND, MOVE_ICE_BEAM, MOVE_THUNDERBOLT,
        4, 0, 0, 252, 252, 0);

    /* 5: Mew | Jolly | All-around utility
       Lv50: Lum Berry  Lv100: Salac Berry */
    AddBoxMon(boxId, 5, SPECIES_MEW, level, NATURE_JOLLY,
        (level==50) ? ITEM_LUM_BERRY : ITEM_SALAC_BERRY,
        MOVE_SOFT_BOILED, MOVE_ANCIENT_POWER, MOVE_EARTHQUAKE, MOVE_PSYCHIC,
        4, 252, 0, 252, 0, 0);

    /* --- Gen 2 legendaries --- */

    /* 6: Raikou | Timid | Calm Mind special sweeper
       Lv50: Lum Berry  Lv100: Petaya Berry */
    AddBoxMon(boxId, 6, SPECIES_RAIKOU, level, NATURE_TIMID,
        (level==50) ? ITEM_LUM_BERRY : ITEM_PETAYA_BERRY,
        MOVE_THUNDERBOLT, MOVE_CALM_MIND, MOVE_CRUNCH, MOVE_SUBSTITUTE,
        4, 0, 0, 252, 252, 0);

    /* 7: Entei | Adamant | Physical fire attacker
       Lv50: Choice Band  Lv100: Lum Berry */
    AddBoxMon(boxId, 7, SPECIES_ENTEI, level, NATURE_ADAMANT,
        (level==50) ? ITEM_CHOICE_BAND : ITEM_LUM_BERRY,
        MOVE_SACRED_FIRE, MOVE_EXTREME_SPEED, MOVE_IRON_TAIL, MOVE_SHADOW_BALL,
        4, 252, 0, 252, 0, 0);

    /* 8: Suicune | Bold | Calm Mind stall
       Lv50: Leftovers  Lv100: Sitrus Berry */
    AddBoxMon(boxId, 8, SPECIES_SUICUNE, level, NATURE_BOLD,
        (level==50) ? ITEM_LEFTOVERS : ITEM_SITRUS_BERRY,
        MOVE_SURF, MOVE_ICE_BEAM, MOVE_CALM_MIND, MOVE_SUBSTITUTE,
        252, 0, 252, 0, 4, 0);

    /* 9: Lugia | Bold | Defensive pivot
       Lv50: Leftovers  Lv100: Sitrus Berry */
    AddBoxMon(boxId, 9, SPECIES_LUGIA, level, NATURE_BOLD,
        (level==50) ? ITEM_LEFTOVERS : ITEM_SITRUS_BERRY,
        MOVE_AEROBLAST, MOVE_CALM_MIND, MOVE_RECOVER, MOVE_ICE_BEAM,
        252, 0, 252, 0, 4, 0);

    /* 10: Ho-Oh | Adamant | Physical attacker
       Lv50: Choice Band  Lv100: Lum Berry */
    AddBoxMon(boxId, 10, SPECIES_HO_OH, level, NATURE_ADAMANT,
        (level==50) ? ITEM_CHOICE_BAND : ITEM_LUM_BERRY,
        MOVE_SACRED_FIRE, MOVE_EARTHQUAKE, MOVE_SHADOW_BALL, MOVE_ANCIENT_POWER,
        4, 252, 0, 252, 0, 0);

    /* 11: Celebi | Calm | Calm Mind support
       Lv50: Leftovers  Lv100: Sitrus Berry */
    AddBoxMon(boxId, 11, SPECIES_CELEBI, level, NATURE_CALM,
        (level==50) ? ITEM_LEFTOVERS : ITEM_SITRUS_BERRY,
        MOVE_PSYCHIC, MOVE_CALM_MIND, MOVE_RECOVER, MOVE_LEECH_SEED,
        252, 0, 0, 0, 4, 252);

    /* --- Gen 3 legendaries --- */

    /* 12: Regirock | Impish | Physical wall
       Lv50: Leftovers  Lv100: Wiki Berry */
    AddBoxMon(boxId, 12, SPECIES_REGIROCK, level, NATURE_IMPISH,
        (level==50) ? ITEM_LEFTOVERS : ITEM_WIKI_BERRY,
        MOVE_ROCK_SLIDE, MOVE_EARTHQUAKE, MOVE_THUNDER_WAVE, MOVE_PROTECT,
        252, 4, 252, 0, 0, 0);

    /* 13: Regice | Calm | Special wall / Calm Mind
       Lv50: Leftovers  Lv100: Sitrus Berry */
    AddBoxMon(boxId, 13, SPECIES_REGICE, level, NATURE_CALM,
        (level==50) ? ITEM_LEFTOVERS : ITEM_SITRUS_BERRY,
        MOVE_ICE_BEAM, MOVE_THUNDERBOLT, MOVE_CALM_MIND, MOVE_AMNESIA,
        252, 0, 0, 0, 4, 252);

    /* 14: Registeel | Impish | Utility wall
       Lv50: Leftovers  Lv100: Wiki Berry */
    AddBoxMon(boxId, 14, SPECIES_REGISTEEL, level, NATURE_IMPISH,
        (level==50) ? ITEM_LEFTOVERS : ITEM_WIKI_BERRY,
        MOVE_SEISMIC_TOSS, MOVE_EARTHQUAKE, MOVE_THUNDER_WAVE, MOVE_PROTECT,
        252, 4, 252, 0, 0, 0);

    /* 15: Latias | Timid | Calm Mind sweeper
       Lv50: Lum Berry  Lv100: Petaya Berry */
    AddBoxMon(boxId, 15, SPECIES_LATIAS, level, NATURE_TIMID,
        (level==50) ? ITEM_LUM_BERRY : ITEM_PETAYA_BERRY,
        MOVE_MIST_BALL, MOVE_CALM_MIND, MOVE_ICE_BEAM, MOVE_RECOVER,
        4, 0, 0, 252, 252, 0);

    /* 16: Latios | Timid | Offensive Calm Mind
       Lv50: Lum Berry  Lv100: Scope Lens */
    AddBoxMon(boxId, 16, SPECIES_LATIOS, level, NATURE_TIMID,
        (level==50) ? ITEM_LUM_BERRY : ITEM_SCOPE_LENS,
        MOVE_LUSTER_PURGE, MOVE_CALM_MIND, MOVE_THUNDERBOLT, MOVE_ICE_BEAM,
        4, 0, 0, 252, 252, 0);

    /* 17: Kyogre | Modest | Rain Dance Water Spout
       Lv50: Lum Berry  Lv100: Petaya Berry */
    AddBoxMon(boxId, 17, SPECIES_KYOGRE, level, NATURE_MODEST,
        (level==50) ? ITEM_LUM_BERRY : ITEM_PETAYA_BERRY,
        MOVE_WATER_SPOUT, MOVE_ICE_BEAM, MOVE_THUNDER, MOVE_CALM_MIND,
        4, 0, 0, 252, 252, 0);

    /* 18: Groudon | Adamant | Sunny physical attacker
       Lv50: Choice Band  Lv100: Lum Berry */
    AddBoxMon(boxId, 18, SPECIES_GROUDON, level, NATURE_ADAMANT,
        (level==50) ? ITEM_CHOICE_BAND : ITEM_LUM_BERRY,
        MOVE_EARTHQUAKE, MOVE_ROCK_SLIDE, MOVE_FIRE_BLAST, MOVE_SWORDS_DANCE,
        4, 252, 0, 252, 0, 0);

    /* 19: Rayquaza | Adamant | Dragon Dance sweeper
       Lv50: Lum Berry  Lv100: Salac Berry */
    AddBoxMon(boxId, 19, SPECIES_RAYQUAZA, level, NATURE_ADAMANT,
        (level==50) ? ITEM_LUM_BERRY : ITEM_SALAC_BERRY,
        MOVE_DRAGON_CLAW, MOVE_EARTHQUAKE, MOVE_ROCK_SLIDE, MOVE_DRAGON_DANCE,
        4, 252, 0, 252, 0, 0);

    /* 20: Jirachi | Jolly | Wish support + physical attacker
       Lv50: Lum Berry  Lv100: Shell Bell */
    AddBoxMon(boxId, 20, SPECIES_JIRACHI, level, NATURE_JOLLY,
        (level==50) ? ITEM_LUM_BERRY : ITEM_SHELL_BELL,
        MOVE_DOOM_DESIRE, MOVE_WISH, MOVE_BODY_SLAM, MOVE_PROTECT,
        4, 252, 0, 252, 0, 0);

    /* 21: Deoxys (Attack Forme in Emerald) | Timid | Glass cannon
       Lv50: Lum Berry  Lv100: Petaya Berry */
    AddBoxMon(boxId, 21, SPECIES_DEOXYS, level, NATURE_TIMID,
        (level==50) ? ITEM_LUM_BERRY : ITEM_PETAYA_BERRY,
        MOVE_PSYCHO_BOOST, MOVE_THUNDERBOLT, MOVE_ICE_BEAM, MOVE_SHADOW_BALL,
        4, 0, 0, 252, 252, 0);

    /* 22: Tyranitar | Adamant | Sand Stream Dragon Dance
       Lv50: Lum Berry  Lv100: Scope Lens */
    AddBoxMon(boxId, 22, SPECIES_TYRANITAR, level, NATURE_ADAMANT,
        (level==50) ? ITEM_LUM_BERRY : ITEM_SCOPE_LENS,
        MOVE_ROCK_SLIDE, MOVE_CRUNCH, MOVE_EARTHQUAKE, MOVE_DRAGON_DANCE,
        4, 252, 0, 252, 0, 0);
}


/* -------------------------------------------------------------------------
   Legacy single-archetype specials kept for compatibility with specials.inc.
   They now call into the unified filler at lv50.
   ------------------------------------------------------------------------- */
void GivePresetParty_Offense(void)     { FillOffense(50, 0xFF, 0); }
void GivePresetParty_Stall(void)       { FillStall(50, 0xFF, 0); }
void GivePresetParty_Balanced(void)    { FillBalanced(50, 0xFF, 0); }
void GivePresetParty_SpeedControl(void){ FillSpeed(50, 0xFF, 0); }
void GivePresetParty_TrickRoom(void)   { FillTrickRoom(50, 0xFF, 0); }
void GivePresetParty_Rain(void)        { FillRain(50, 0xFF, 0); }
void GivePresetParty_Sun(void)         { FillSun(50, 0xFF, 0); }
void GivePresetParty_Sand(void)        { FillSand(50, 0xFF, 0); }
