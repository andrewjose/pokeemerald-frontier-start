// Damage preview on the HP bars while the player picks a move (v1.36).
//
// Each bar the highlighted move can hit alternates between the forecast after
// the hit and its current HP:
//
//   [ remaining HP | lowest-to-highest roll (pale) | lost on any roll (empty) ]
//
// The bar's tiles are copied before the first change and copied back by
// DamagePreview_Hide, so nothing the battle draws is recomputed or changed.
// The damage comes from DamageCalc_Move (battle_info_damage.c), which reads
// the battle state without changing it.

#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_damage_preview.h"
#include "battle_info_damage.h"
#include "battle_interface.h"
#include "pokemon.h"
#include "sprite.h"
#include "util.h"
#include "constants/moves.h"

#define B_HEALTHBAR_PIXELS 48 // as in battle_interface.c
#define BAR_TILES       (B_HEALTHBAR_PIXELS / 8)
#define BAR_BYTES       (BAR_TILES * TILE_SIZE_4BPP)
#define BAR_FIRST_TILE  2   // the healthbar sprite's tiles: "H", "P", then the bar
#define hMain_HealthBarSpriteId data[5] // as in battle_interface.c

// The bar is two pixel rows of each tile (graphics/battle_interface/hpbar.png),
// colored from the healthbar palette.
#define BAR_ROW_TOP         3
#define BAR_ROW_BOTTOM      4
#define COLOR_EMPTY_TOP     5
#define COLOR_EMPTY_BOTTOM  6
#define COLOR_ROLL_TOP      3   // beige
#define COLOR_ROLL_BOTTOM   2   // white

// One cycle: the forecast, then the bar as it is now.
#define FORECAST_FRAMES 40
#define CYCLE_FRAMES    60

struct PreviewBar
{
    bool8 active;
    u8 healthbarSpriteId;
    u8 now;         // filled pixels now
    u8 afterMin;    // filled pixels left after the lowest roll
    u8 afterMax;    // filled pixels left after the highest roll
    u32 saved[BAR_BYTES / 4];   // word-aligned for CpuCopy32
};

static EWRAM_DATA struct PreviewBar sBars[MAX_BATTLERS_COUNT] = {0};
static EWRAM_DATA bool8 sShown = FALSE;
static EWRAM_DATA bool8 sForecastDrawn = FALSE;
static EWRAM_DATA u8 sAttacker = 0;
static EWRAM_DATA u8 sOnlyTarget = 0;
static EWRAM_DATA u8 sTimer = 0;
static EWRAM_DATA u16 sMove = MOVE_NONE;

static u8 *GetBarVram(u8 healthbarSpriteId)
{
    return (u8 *)(OBJ_VRAM0 + (gSprites[healthbarSpriteId].oam.tileNum + BAR_FIRST_TILE) * TILE_SIZE_4BPP);
}

static struct Pokemon *GetBattlerParty(u8 battler)
{
    return GetBattlerSide(battler) == B_SIDE_PLAYER ? gPlayerParty : gEnemyParty;
}

// Same rounding as the game's bar (CalcBarFilledPixels): at least one pixel
// while the Pokemon has HP left.
static u8 HpToPixels(s32 hp, s32 maxHp)
{
    s32 pixels;

    if (hp <= 0 || maxHp <= 0)
        return 0;
    pixels = hp * B_HEALTHBAR_PIXELS / maxHp;
    if (pixels == 0)
        pixels = 1;
    if (pixels > B_HEALTHBAR_PIXELS)
        pixels = B_HEALTHBAR_PIXELS;
    return pixels;
}

static u8 GetPixel(const u8 *tiles, u8 x, u8 row)
{
    u8 byte = tiles[(x / 8) * TILE_SIZE_4BPP + row * 4 + (x % 8) / 2];

    return (x & 1) ? (byte >> 4) : (byte & 0xF);
}

static void SetPixel(u8 *tiles, u8 x, u8 row, u8 color)
{
    u8 *byte = &tiles[(x / 8) * TILE_SIZE_4BPP + row * 4 + (x % 8) / 2];

    if (x & 1)
        *byte = (*byte & 0x0F) | (color << 4);
    else
        *byte = (*byte & 0xF0) | color;
}

// The filled length of the bar as drawn.
static u8 CountFilledPixels(const u8 *tiles)
{
    u8 x;

    for (x = 0; x < B_HEALTHBAR_PIXELS; x++)
    {
        if (GetPixel(tiles, x, BAR_ROW_TOP) == COLOR_EMPTY_TOP)
            break;
    }
    return x;
}

static bool8 CanPreviewOn(u8 attacker, u16 move, u8 target, u8 onlyTarget)
{
    u8 moveTarget = gBattleMoves[move].target;
    u8 healthboxSpriteId;

    if (target >= gBattlersCount || target == attacker)
        return FALSE;
    if ((gAbsentBattlerFlags & gBitTable[target]) || gBattleMons[target].hp == 0)
        return FALSE;
    // In doubles, START swaps your healthboxes' bars for HP numbers
    if (gBattleSpritesDataPtr->battlerData[target].hpNumbersNoBars)
        return FALSE;
    healthboxSpriteId = gHealthboxSpriteIds[target];
    if (healthboxSpriteId >= MAX_SPRITES || !gSprites[healthboxSpriteId].inUse
     || !gSprites[gSprites[healthboxSpriteId].hMain_HealthBarSpriteId].inUse)
        return FALSE;

    if (onlyTarget < MAX_BATTLERS_COUNT)
        return target == onlyTarget;
    if (GetBattlerSide(target) == GetBattlerSide(attacker))
        return (moveTarget & MOVE_TARGET_FOES_AND_ALLY) != 0; // Earthquake, Surf, ...
    return !(moveTarget & (MOVE_TARGET_USER | MOVE_TARGET_DEPENDS | MOVE_TARGET_OPPONENTS_FIELD));
}

static void SetUpBar(const struct DamageCalcMon *attacker, u8 target, u16 move)
{
    struct PreviewBar *bar = &sBars[target];
    struct DamageCalcMon defender;
    struct DamageCalcResult result;
    s32 hp, maxHp, lo, hi;
    u8 afterMin, afterMax;

    DamageCalc_InitMon(&defender, GetBattlerParty(target), gBattlerPartyIndexes[target], GetBattlerSide(target), target);
    DamageCalc_Move(attacker, &defender, move, &result);

    hp = defender.mon.hp;
    maxHp = defender.mon.maxHP;
    if (result.kind == DMG_CALC_OHKO)
    {
        lo = hp;
        hi = hp;
    }
    else if (result.kind == DMG_CALC_HIT)
    {
        lo = result.min;
        hi = result.max;
    }
    else
    {
        return; // status move, immune, fails, or depends on this turn (Counter)
    }

    if (result.cantKO) // False Swipe
    {
        if (lo > hp - 1)
            lo = hp - 1;
        if (hi > hp - 1)
            hi = hp - 1;
    }
    if (hi > hp)
        hi = hp;
    if (lo > hi)
        lo = hi;
    if (lo < 0)
        lo = 0;
    if (hi <= 0)
        return;

    bar->healthbarSpriteId = gSprites[gHealthboxSpriteIds[target]].hMain_HealthBarSpriteId;
    CpuCopy32(GetBarVram(bar->healthbarSpriteId), bar->saved, BAR_BYTES);
    bar->now = CountFilledPixels((const u8 *)bar->saved);

    afterMin = HpToPixels(hp - lo, maxHp);
    afterMax = HpToPixels(hp - hi, maxHp);
    if (afterMin > bar->now)
        afterMin = bar->now;
    if (afterMax > afterMin)
        afterMax = afterMin;
    // Less than a pixel of damage still shows one pale pixel
    if (afterMax == bar->now && bar->now > 1)
        afterMax = bar->now - 1;
    if (afterMax == bar->now)
        return;

    bar->afterMin = afterMin;
    bar->afterMax = afterMax;
    bar->active = TRUE;
}

static void SetUp(u8 attacker, u16 move, u8 onlyTarget)
{
    struct DamageCalcMon attackerMon;
    u8 target;

    sShown = TRUE;
    sForecastDrawn = FALSE;
    sTimer = 0;
    sAttacker = attacker;
    sMove = move;
    sOnlyTarget = onlyTarget;
    for (target = 0; target < MAX_BATTLERS_COUNT; target++)
        sBars[target].active = FALSE;

    if (move == MOVE_NONE || move >= MOVES_COUNT || gBattleMoves[move].power == 0)
        return;

    DamageCalc_InitMon(&attackerMon, GetBattlerParty(attacker), gBattlerPartyIndexes[attacker], GetBattlerSide(attacker), attacker);
    for (target = 0; target < gBattlersCount; target++)
    {
        if (CanPreviewOn(attacker, move, target, onlyTarget))
            SetUpBar(&attackerMon, target, move);
    }
}

static void DrawBar(const struct PreviewBar *bar, bool8 forecast)
{
    u32 tiles[BAR_BYTES / 4];   // word-aligned for CpuCopy32
    u8 x;

    CpuCopy32(bar->saved, tiles, BAR_BYTES);
    if (forecast)
    {
        for (x = bar->afterMax; x < bar->afterMin; x++)
        {
            SetPixel((u8 *)tiles, x, BAR_ROW_TOP, COLOR_ROLL_TOP);
            SetPixel((u8 *)tiles, x, BAR_ROW_BOTTOM, COLOR_ROLL_BOTTOM);
        }
        for (x = bar->afterMin; x < bar->now; x++)
        {
            SetPixel((u8 *)tiles, x, BAR_ROW_TOP, COLOR_EMPTY_TOP);
            SetPixel((u8 *)tiles, x, BAR_ROW_BOTTOM, COLOR_EMPTY_BOTTOM);
        }
    }
    CpuCopy32(tiles, GetBarVram(bar->healthbarSpriteId), BAR_BYTES);
}

void DamagePreview_Update(u8 attacker, u16 move, u8 onlyTarget)
{
    bool8 forecast;
    u8 i;

    if (!sShown || attacker != sAttacker || move != sMove || onlyTarget != sOnlyTarget)
    {
        DamagePreview_Hide();
        SetUp(attacker, move, onlyTarget);
    }

    forecast = (sTimer < FORECAST_FRAMES);
    if (forecast != sForecastDrawn)
    {
        for (i = 0; i < MAX_BATTLERS_COUNT; i++)
        {
            if (sBars[i].active)
                DrawBar(&sBars[i], forecast);
        }
        sForecastDrawn = forecast;
    }
    if (++sTimer >= CYCLE_FRAMES)
        sTimer = 0;
}

void DamagePreview_Hide(void)
{
    u8 i;

    if (!sShown)
        return;
    for (i = 0; i < MAX_BATTLERS_COUNT; i++)
    {
        if (sBars[i].active)
        {
            if (sForecastDrawn && gSprites[sBars[i].healthbarSpriteId].inUse)
                CpuCopy32(sBars[i].saved, GetBarVram(sBars[i].healthbarSpriteId), BAR_BYTES);
            sBars[i].active = FALSE;
        }
    }
    sShown = FALSE;
    sForecastDrawn = FALSE;
}

void DamagePreview_Reset(void)
{
    u8 i;

    for (i = 0; i < MAX_BATTLERS_COUNT; i++)
        sBars[i].active = FALSE;
    sShown = FALSE;
    sForecastDrawn = FALSE;
}
