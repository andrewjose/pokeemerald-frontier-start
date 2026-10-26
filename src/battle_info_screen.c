// Battle Info Screen
//
// Press L+R on the battle action menu (FIGHT/BAG/POKEMON/RUN) or the move menu
// to open a full-screen view of every Pokemon on both sides of the battle:
// IVs, EVs, nature, stats, in-battle stat stages, ability, held item, HP,
// status, Hidden Power and moves with PP. The screen opens on the opponent's
// side, on the opponent currently in battle.
//
// Controls: D-pad = previous/next Pokemon, L/R/SELECT = switch side,
//           B/START = back to the battle.
//
// The battle is suspended exactly like it is for the in-battle Bag and party
// menu: the battle graphics are torn down, and ReshowBattleScreenAfterMenu
// rebuilds them on exit. Nothing in the battle state is modified.

#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_info_screen.h"
#include "battle_main.h"
#include "bg.h"
#include "data.h"
#include "gpu_regs.h"
#include "item.h"
#include "main.h"
#include "malloc.h"
#include "menu.h"
#include "menu_helpers.h"
#include "palette.h"
#include "pokemon.h"
#include "pokemon_icon.h"
#include "pokemon_summary_screen.h"
#include "scanline_effect.h"
#include "sound.h"
#include "sprite.h"
#include "string_util.h"
#include "task.h"
#include "text.h"
#include "util.h"
#include "window.h"
#include "constants/battle.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/rgb.h"
#include "constants/songs.h"

enum {
    INFO_SIDE_FOE,
    INFO_SIDE_PLAYER,
    INFO_SIDE_COUNT
};

enum {
    WIN_HEADER,
    WIN_LEFT,
    WIN_RIGHT,
};

#define PANEL_WIDTH      (15 * 8)
#define LINE_HEIGHT      12
#define ICON_X_START     76
#define ICON_X_SPACING   28
#define ICON_Y           17

// Palette 15 color indices (copy of the standard menu palette, with a few
// extra colors filled in below).
#define COLOR_TRANSPARENT  0
#define COLOR_WHITE        1
#define COLOR_DARK_GRAY    2
#define COLOR_LIGHT_GRAY   3
#define COLOR_RED          4
#define COLOR_LIGHT_RED    5
#define COLOR_GREEN        6
#define COLOR_LIGHT_GREEN  7
#define COLOR_BLUE         8
#define COLOR_LIGHT_BLUE   9
#define COLOR_ORANGE       10
#define COLOR_LIGHT_ORANGE 11
#define COLOR_MID_GRAY     12
#define COLOR_SEPARATOR    13

#define BG_COLOR_FOE     RGB(19, 6, 7)
#define BG_COLOR_PLAYER  RGB(5, 9, 19)

struct BattleInfoScreen
{
    MainCallback exitCallback;
    u16 tilemap[BG_SCREEN_SIZE / 2];
    u8 side;
    u8 count[INFO_SIDE_COUNT];
    u8 slots[INFO_SIDE_COUNT][PARTY_SIZE];
    u8 cursor[INFO_SIDE_COUNT];
    u8 iconSpriteIds[PARTY_SIZE];
    u8 iconCount;
    bool8 failed;
    u16 savedDispcnt;
    u16 savedBldcnt;
    u16 savedBldalpha;
    u16 savedBldy;
    u16 savedWinin;
    u16 savedWinout;
    u16 savedWin0h;
    u16 savedWin0v;
    u16 savedWin1h;
    u16 savedWin1v;
};

// Everything shown on one page, gathered from the party struct and, for
// Pokemon currently in battle, the live gBattleMons copy.
struct InfoPageData
{
    u16 species;
    u8 level;
    u8 gender;
    bool8 shiny;
    u8 nature;
    u8 ability;
    u16 item;
    u16 hp;
    u16 maxHp;
    u32 status;
    u16 stats[NUM_STATS];
    u8 ivs[NUM_STATS];
    u8 evs[NUM_STATS];
    u16 moves[MAX_MON_MOVES];
    u8 pp[MAX_MON_MOVES];
    u8 maxPp[MAX_MON_MOVES];
    s8 stages[NUM_BATTLE_STATS];
    bool8 active;
    bool8 transformed;
    u8 hiddenPowerType;
    u8 hiddenPowerPower;
};

static EWRAM_DATA struct BattleInfoScreen *sInfo = NULL;

static void CB2_BattleInfoSetup(void);
static void CB2_BattleInfo(void);
static void VBlankCB_BattleInfo(void);
static void Task_BattleInfoInput(u8 taskId);
static void Task_BattleInfoExit(u8 taskId);
static void BuildSideLists(void);
static void CreateSideIcons(void);
static void DestroySideIcons(void);
static void UpdateIconSelection(void);
static void DrawHeader(void);
static void DrawPage(void);
static void GatherPageData(struct InfoPageData *data, u8 side, u8 partyIndex);

static const struct BgTemplate sBgTemplates[] =
{
    {
        .bg = 0,
        .charBaseIndex = 0,
        .mapBaseIndex = 31,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 1,
        .baseTile = 0,
    },
};

static const struct WindowTemplate sWindowTemplates[] =
{
    [WIN_HEADER] = {
        .bg = 0,
        .tilemapLeft = 0,
        .tilemapTop = 0,
        .width = 8,
        .height = 4,
        .paletteNum = 15,
        .baseBlock = 1,
    },
    [WIN_LEFT] = {
        .bg = 0,
        .tilemapLeft = 0,
        .tilemapTop = 4,
        .width = 15,
        .height = 16,
        .paletteNum = 15,
        .baseBlock = 1 + 8 * 4,
    },
    [WIN_RIGHT] = {
        .bg = 0,
        .tilemapLeft = 15,
        .tilemapTop = 4,
        .width = 15,
        .height = 16,
        .paletteNum = 15,
        .baseBlock = 1 + 8 * 4 + 15 * 16,
    },
    DUMMY_WIN_TEMPLATE
};

// Display order of the stats table (party/battle structs use HP, Atk, Def, Spe, SpA, SpD)
static const u8 sStatDisplayOrder[NUM_STATS] = {STAT_HP, STAT_ATK, STAT_DEF, STAT_SPATK, STAT_SPDEF, STAT_SPEED};

static const u8 sText_Foe[] = _("FOE");
static const u8 sText_Player[] = _("YOURS");
static const u8 sText_Of[] = _(" of ");
static const u8 sText_Lv[] = _("Lv");
static const u8 sText_Male[] = _("♂");
static const u8 sText_Female[] = _("♀");
static const u8 sText_Nature[] = _("Nature");
static const u8 sText_Ability[] = _("Ability");
static const u8 sText_Item[] = _("Item");
static const u8 sText_HP[] = _("HP");
static const u8 sText_HiddenPower[] = _("Hid.Pow");
static const u8 sText_None[] = _("-");
static const u8 sText_Slash[] = _("/");
static const u8 sText_Space[] = _(" ");
static const u8 sText_Plus[] = _("+");
static const u8 sText_Zero[] = _("0");
static const u8 sText_ColStat[] = _("STAT");
static const u8 sText_ColIV[] = _("IV");
static const u8 sText_ColEV[] = _("EV");
static const u8 sText_ColStage[] = _("STG");
static const u8 sText_Acc[] = _("ACC");
static const u8 sText_Eva[] = _("EVA");
static const u8 sText_InBattle[] = _("IN BATTLE");
static const u8 sText_InParty[] = _("IN PARTY");
static const u8 sText_Fainted[] = _("FAINTED");
static const u8 sText_Shiny[] = _("SHINY");
static const u8 sText_Transformed[] = _("TRANSFORMED");
static const u8 sText_EVTotal[] = _("EVs ");
static const u8 sText_Controls[] = _("{L_BUTTON}{R_BUTTON} Side   {B_BUTTON} Back");

static const u8 sText_StatHP[] = _("HP");
static const u8 sText_StatAtk[] = _("ATK");
static const u8 sText_StatDef[] = _("DEF");
static const u8 sText_StatSpe[] = _("SPE");
static const u8 sText_StatSpA[] = _("SPA");
static const u8 sText_StatSpD[] = _("SPD");

static const u8 *const sStatNames[NUM_STATS] =
{
    [STAT_HP]    = sText_StatHP,
    [STAT_ATK]   = sText_StatAtk,
    [STAT_DEF]   = sText_StatDef,
    [STAT_SPEED] = sText_StatSpe,
    [STAT_SPATK] = sText_StatSpA,
    [STAT_SPDEF] = sText_StatSpD,
};

static const u8 sText_StatusSlp[] = _("SLP");
static const u8 sText_StatusPsn[] = _("PSN");
static const u8 sText_StatusTox[] = _("TOX");
static const u8 sText_StatusBrn[] = _("BRN");
static const u8 sText_StatusFrz[] = _("FRZ");
static const u8 sText_StatusPar[] = _("PAR");
static const u8 sText_StatusFnt[] = _("FNT");

// {bg, fg, shadow}
static const u8 sColor_Normal[]     = {COLOR_WHITE, COLOR_DARK_GRAY, COLOR_LIGHT_GRAY};
static const u8 sColor_Label[]      = {COLOR_WHITE, COLOR_MID_GRAY, COLOR_LIGHT_GRAY};
static const u8 sColor_Red[]        = {COLOR_WHITE, COLOR_RED, COLOR_LIGHT_RED};
static const u8 sColor_Blue[]       = {COLOR_WHITE, COLOR_BLUE, COLOR_LIGHT_BLUE};
static const u8 sColor_Green[]      = {COLOR_WHITE, COLOR_GREEN, COLOR_LIGHT_GREEN};
static const u8 sColor_Orange[]     = {COLOR_WHITE, COLOR_ORANGE, COLOR_LIGHT_ORANGE};
static const u8 sColor_Header[]     = {COLOR_TRANSPARENT, COLOR_WHITE, COLOR_DARK_GRAY};

// With the L=A button option (the default on new saves in the Frontier Start
// mod), a lone L press counts as A. Hold that A back for a few frames so that
// L landing slightly before R opens this screen instead of choosing FIGHT or a
// move. A real A press, or letting go of L, delivers it immediately.
#define L_EQUALS_A_GRACE_FRAMES 5

static EWRAM_DATA u8 sLEqualsAGraceFrames = 0;

u8 BattleInfoScreen_CheckCombo(void)
{
    if (gBattleTypeFlags & (BATTLE_TYPE_LINK | BATTLE_TYPE_RECORDED))
    {
        sLEqualsAGraceFrames = 0;
        return BATTLE_INFO_COMBO_NONE;
    }

    if ((gMain.heldKeys & (L_BUTTON | R_BUTTON)) == (L_BUTTON | R_BUTTON)
     && (gMain.newKeys & (L_BUTTON | R_BUTTON)))
    {
        sLEqualsAGraceFrames = 0;
        return BATTLE_INFO_COMBO_OPEN;
    }

    if (gSaveBlock2Ptr->optionsButtonMode != OPTIONS_BUTTON_MODE_L_EQUALS_A)
    {
        sLEqualsAGraceFrames = 0;
        return BATTLE_INFO_COMBO_NONE;
    }

    if (sLEqualsAGraceFrames != 0)
    {
        if ((gMain.newKeysRaw & A_BUTTON) || !(gMain.heldKeysRaw & L_BUTTON) || --sLEqualsAGraceFrames == 0)
        {
            // R never came: deliver the held-back L-as-A press now.
            sLEqualsAGraceFrames = 0;
            gMain.newKeys |= A_BUTTON;
            return BATTLE_INFO_COMBO_NONE;
        }
        return BATTLE_INFO_COMBO_WAIT;
    }

    if ((gMain.newKeysRaw & L_BUTTON) && !(gMain.newKeysRaw & A_BUTTON))
    {
        sLEqualsAGraceFrames = L_EQUALS_A_GRACE_FRAMES;
        gMain.newKeys &= ~A_BUTTON;
        return BATTLE_INFO_COMBO_WAIT;
    }

    return BATTLE_INFO_COMBO_NONE;
}

void ShowBattleInfoScreen(MainCallback exitCallback)
{
    sInfo = AllocZeroed(sizeof(*sInfo));
    if (sInfo == NULL)
    {
        // Not enough memory: go straight back to the battle.
        SetMainCallback2(exitCallback);
        return;
    }

    sInfo->exitCallback = exitCallback;
    sInfo->savedDispcnt = GetGpuReg(REG_OFFSET_DISPCNT);
    sInfo->savedBldcnt = GetGpuReg(REG_OFFSET_BLDCNT);
    sInfo->savedBldalpha = GetGpuReg(REG_OFFSET_BLDALPHA);
    sInfo->savedBldy = GetGpuReg(REG_OFFSET_BLDY);
    sInfo->savedWinin = GetGpuReg(REG_OFFSET_WININ);
    sInfo->savedWinout = GetGpuReg(REG_OFFSET_WINOUT);
    sInfo->savedWin0h = GetGpuReg(REG_OFFSET_WIN0H);
    sInfo->savedWin0v = GetGpuReg(REG_OFFSET_WIN0V);
    sInfo->savedWin1h = GetGpuReg(REG_OFFSET_WIN1H);
    sInfo->savedWin1v = GetGpuReg(REG_OFFSET_WIN1V);

    BuildSideLists();

    gMain.state = 0;
    SetMainCallback2(CB2_BattleInfoSetup);
}

static void CB2_BattleInfoSetup(void)
{
    switch (gMain.state)
    {
    case 0:
        SetVBlankHBlankCallbacksToNull();
        ClearScheduledBgCopiesToVram();
        ScanlineEffect_Stop();
        gMain.state++;
        break;
    case 1:
        ResetPaletteFade();
        FreeAllSpritePalettes();
        ResetSpriteData();
        ResetTasks();
        gMain.state++;
        break;
    case 2:
        DmaClearLarge16(3, (void *)VRAM, VRAM_SIZE, 0x1000);
        DmaClear32(3, (void *)OAM, OAM_SIZE);
        DmaClear16(3, (void *)PLTT, PLTT_SIZE);
        gMain.state++;
        break;
    case 3:
        ResetBgsAndClearDma3BusyFlags(0);
        InitBgsFromTemplates(0, sBgTemplates, ARRAY_COUNT(sBgTemplates));
        SetBgTilemapBuffer(0, sInfo->tilemap);
        ResetAllBgsCoordinates();
        if (!InitWindows(sWindowTemplates))
        {
            sInfo->failed = TRUE;
            CreateTask(Task_BattleInfoExit, 0);
            SetMainCallback2(CB2_BattleInfo);
            return;
        }
        DeactivateAllTextPrinters();
        gMain.state++;
        break;
    case 4:
    {
        u16 pal[16];
        CpuCopy16(gStandardMenuPalette, pal, sizeof(pal));
        pal[COLOR_ORANGE]       = RGB(28, 14, 2);
        pal[COLOR_LIGHT_ORANGE] = RGB(31, 25, 15);
        pal[COLOR_MID_GRAY]     = RGB(15, 15, 15);
        pal[COLOR_SEPARATOR]    = RGB(24, 24, 23);
        LoadPalette(pal, BG_PLTT_ID(15), PLTT_SIZE_4BPP);
        LoadMonIconPalettes();
        gMain.state++;
        break;
    }
    case 5:
        DrawHeader();
        CreateSideIcons();
        DrawPage();
        gMain.state++;
        break;
    default:
        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP);
        SetGpuReg(REG_OFFSET_WININ, 0);
        SetGpuReg(REG_OFFSET_WINOUT, 0);
        // Unselected party icons are drawn semi-transparent over the backdrop
        SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_TGT2_BG0 | BLDCNT_TGT2_BD);
        SetGpuReg(REG_OFFSET_BLDALPHA, BLDALPHA_BLEND(7, 9));
        SetGpuReg(REG_OFFSET_BLDY, 0);
        ShowBg(0);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        SetVBlankCallback(VBlankCB_BattleInfo);
        CreateTask(Task_BattleInfoInput, 0);
        SetMainCallback2(CB2_BattleInfo);
        break;
    }
}

static void CB2_BattleInfo(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    DoScheduledBgTilemapCopiesToVram();
    UpdatePaletteFade();
}

static void VBlankCB_BattleInfo(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

static void Task_BattleInfoInput(u8 taskId)
{
    u8 side;

    if (gPaletteFade.active)
        return;

    side = sInfo->side;

    if (JOY_NEW(B_BUTTON | START_BUTTON))
    {
        PlaySE(SE_SELECT);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
        gTasks[taskId].func = Task_BattleInfoExit;
    }
    else if (JOY_NEW(L_BUTTON | R_BUTTON | SELECT_BUTTON))
    {
        PlaySE(SE_SELECT);
        sInfo->side ^= 1;
        DestroySideIcons();
        DrawHeader();
        CreateSideIcons();
        DrawPage();
    }
    else if (JOY_REPEAT(DPAD_RIGHT | DPAD_DOWN))
    {
        if (sInfo->count[side] > 1)
        {
            PlaySE(SE_SELECT);
            if (++sInfo->cursor[side] >= sInfo->count[side])
                sInfo->cursor[side] = 0;
            DrawHeader();
            UpdateIconSelection();
            DrawPage();
        }
    }
    else if (JOY_REPEAT(DPAD_LEFT | DPAD_UP))
    {
        if (sInfo->count[side] > 1)
        {
            PlaySE(SE_SELECT);
            if (sInfo->cursor[side] == 0)
                sInfo->cursor[side] = sInfo->count[side] - 1;
            else
                sInfo->cursor[side]--;
            DrawHeader();
            UpdateIconSelection();
            DrawPage();
        }
    }
}

static void Task_BattleInfoExit(u8 taskId)
{
    MainCallback exitCallback;

    if (gPaletteFade.active)
        return;

    exitCallback = sInfo->exitCallback;
    if (!sInfo->failed)
    {
        DestroySideIcons();
        FreeMonIconPalettes();
    }
    FreeAllWindowBuffers();

    SetGpuReg(REG_OFFSET_DISPCNT, sInfo->savedDispcnt & ~(DISPCNT_BG_ALL_ON));
    SetGpuReg(REG_OFFSET_BLDCNT, sInfo->savedBldcnt);
    SetGpuReg(REG_OFFSET_BLDALPHA, sInfo->savedBldalpha);
    SetGpuReg(REG_OFFSET_BLDY, sInfo->savedBldy);
    SetGpuReg(REG_OFFSET_WININ, sInfo->savedWinin);
    SetGpuReg(REG_OFFSET_WINOUT, sInfo->savedWinout);
    SetGpuReg(REG_OFFSET_WIN0H, sInfo->savedWin0h);
    SetGpuReg(REG_OFFSET_WIN0V, sInfo->savedWin0v);
    SetGpuReg(REG_OFFSET_WIN1H, sInfo->savedWin1h);
    SetGpuReg(REG_OFFSET_WIN1V, sInfo->savedWin1v);

    DestroyTask(taskId);
    FREE_AND_SET_NULL(sInfo);
    SetMainCallback2(exitCallback);
}

// ---------------------------------------------------------------------------
// Party bookkeeping
// ---------------------------------------------------------------------------

static struct Pokemon *GetSideParty(u8 side)
{
    return (side == INFO_SIDE_FOE) ? gEnemyParty : gPlayerParty;
}

static u8 GetBattlerSideFor(u8 side)
{
    return (side == INFO_SIDE_FOE) ? B_SIDE_OPPONENT : B_SIDE_PLAYER;
}

// Returns the battler currently holding this party slot, or MAX_BATTLERS_COUNT.
static u8 GetActiveBattlerForSlot(u8 side, u8 partyIndex)
{
    u8 battler;

    for (battler = 0; battler < gBattlersCount; battler++)
    {
        if (GetBattlerSide(battler) != GetBattlerSideFor(side))
            continue;
        if (gAbsentBattlerFlags & gBitTable[battler])
            continue;
        if (gBattlerPartyIndexes[battler] == partyIndex)
            return battler;
    }
    return MAX_BATTLERS_COUNT;
}

static void BuildSideLists(void)
{
    u8 side, i, j;

    for (side = 0; side < INFO_SIDE_COUNT; side++)
    {
        struct Pokemon *party = GetSideParty(side);
        u8 firstActive = 0xFF;
        u8 focusBattler;

        sInfo->count[side] = 0;
        for (i = 0; i < PARTY_SIZE; i++)
        {
            u16 species = GetMonData(&party[i], MON_DATA_SPECIES);
            if (species == SPECIES_NONE || species >= NUM_SPECIES)
                continue;
            if (GetMonData(&party[i], MON_DATA_IS_EGG))
                continue;
            sInfo->slots[side][sInfo->count[side]++] = i;
        }

        // Start each side on the Pokemon that's out: for the player that's the
        // one being given a command, for the foe it's the left-most one.
        if (side == INFO_SIDE_PLAYER)
            focusBattler = gBattlerInMenuId;
        else
            focusBattler = GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT);

        if (focusBattler < gBattlersCount && !(gAbsentBattlerFlags & gBitTable[focusBattler]))
            firstActive = gBattlerPartyIndexes[focusBattler];
        else if (side == INFO_SIDE_FOE && IsDoubleBattle())
            firstActive = gBattlerPartyIndexes[GetBattlerAtPosition(B_POSITION_OPPONENT_RIGHT)];

        sInfo->cursor[side] = 0;
        for (j = 0; j < sInfo->count[side]; j++)
        {
            if (sInfo->slots[side][j] == firstActive)
            {
                sInfo->cursor[side] = j;
                break;
            }
        }
    }

    sInfo->side = INFO_SIDE_FOE;
}

static u8 GetSelectedPartyIndex(void)
{
    return sInfo->slots[sInfo->side][sInfo->cursor[sInfo->side]];
}

// ---------------------------------------------------------------------------
// Party icon strip
// ---------------------------------------------------------------------------

static void CreateSideIcons(void)
{
    u8 i;
    struct Pokemon *party = GetSideParty(sInfo->side);

    sInfo->iconCount = sInfo->count[sInfo->side];
    for (i = 0; i < sInfo->iconCount; i++)
    {
        struct Pokemon *mon = &party[sInfo->slots[sInfo->side][i]];
        u16 species = GetMonData(mon, MON_DATA_SPECIES);
        u32 personality = GetMonData(mon, MON_DATA_PERSONALITY);
        u8 spriteId = CreateMonIcon(species, SpriteCB_MonIcon, ICON_X_START + i * ICON_X_SPACING, ICON_Y, 1, personality, TRUE);

        sInfo->iconSpriteIds[i] = spriteId;
        if (spriteId < MAX_SPRITES)
            gSprites[spriteId].oam.priority = 0;
    }
    UpdateIconSelection();
}

static void DestroySideIcons(void)
{
    u8 i;

    for (i = 0; i < sInfo->iconCount; i++)
    {
        if (sInfo->iconSpriteIds[i] < MAX_SPRITES)
            FreeAndDestroyMonIconSprite(&gSprites[sInfo->iconSpriteIds[i]]);
        sInfo->iconSpriteIds[i] = MAX_SPRITES;
    }
    sInfo->iconCount = 0;
}

static void UpdateIconSelection(void)
{
    u8 i;

    for (i = 0; i < sInfo->iconCount; i++)
    {
        struct Sprite *sprite;

        if (sInfo->iconSpriteIds[i] >= MAX_SPRITES)
            continue;

        sprite = &gSprites[sInfo->iconSpriteIds[i]];
        if (i == sInfo->cursor[sInfo->side])
        {
            sprite->callback = SpriteCB_MonIcon;
            sprite->oam.objMode = ST_OAM_OBJ_NORMAL;
            sprite->y2 = -2;
        }
        else
        {
            sprite->callback = SpriteCallbackDummy;
            sprite->oam.objMode = ST_OAM_OBJ_BLEND;
            sprite->y2 = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void PrintText(u8 windowId, u8 x, u8 y, const u8 *color, const u8 *str)
{
    AddTextPrinterParameterized3(windowId, FONT_SMALL, x, y, color, TEXT_SKIP_DRAW, str);
}

static void PrintTextRightAligned(u8 windowId, u8 rightX, u8 y, const u8 *color, const u8 *str)
{
    s32 width = GetStringWidth(FONT_SMALL, str, 0);
    s32 x = rightX - width;

    if (x < 0)
        x = 0;
    PrintText(windowId, x, y, color, str);
}

static void PrintNumberRightAligned(u8 windowId, u8 rightX, u8 y, const u8 *color, s32 value, u8 digits)
{
    u8 buffer[8];

    ConvertIntToDecimalStringN(buffer, value, STR_CONV_MODE_LEFT_ALIGN, digits);
    PrintTextRightAligned(windowId, rightX, y, color, buffer);
}

static void FormatStage(u8 *dst, s8 stage)
{
    if (stage > 0)
    {
        dst = StringCopy(dst, sText_Plus);
        ConvertIntToDecimalStringN(dst, stage, STR_CONV_MODE_LEFT_ALIGN, 1);
    }
    else if (stage < 0)
    {
        *dst++ = CHAR_HYPHEN;
        ConvertIntToDecimalStringN(dst, -stage, STR_CONV_MODE_LEFT_ALIGN, 1);
    }
    else
    {
        StringCopy(dst, sText_Zero);
    }
}

static const u8 *GetStageColor(s8 stage)
{
    if (stage > 0)
        return sColor_Red;
    if (stage < 0)
        return sColor_Blue;
    return sColor_Label;
}

static void DrawHeader(void)
{
    u8 buffer[16];
    u8 *end;

    FillWindowPixelBuffer(WIN_HEADER, PIXEL_FILL(COLOR_TRANSPARENT));

    AddTextPrinterParameterized3(WIN_HEADER, FONT_NORMAL, 4, 1, sColor_Header, TEXT_SKIP_DRAW,
                                 sInfo->side == INFO_SIDE_FOE ? sText_Foe : sText_Player);

    end = ConvertIntToDecimalStringN(buffer, sInfo->cursor[sInfo->side] + 1, STR_CONV_MODE_LEFT_ALIGN, 1);
    end = StringCopy(end, sText_Of);
    ConvertIntToDecimalStringN(end, sInfo->count[sInfo->side], STR_CONV_MODE_LEFT_ALIGN, 1);
    AddTextPrinterParameterized3(WIN_HEADER, FONT_SMALL, 4, 17, sColor_Header, TEXT_SKIP_DRAW, buffer);

    PutWindowTilemap(WIN_HEADER);
    CopyWindowToVram(WIN_HEADER, COPYWIN_FULL);

    // Side-colored backdrop
    {
        u16 color = (sInfo->side == INFO_SIDE_FOE) ? BG_COLOR_FOE : BG_COLOR_PLAYER;
        LoadPalette(&color, BG_PLTT_ID(0), PLTT_SIZEOF(1));
    }
}

static void DrawLeftPanel(const struct InfoPageData *data)
{
    u8 buffer[32];
    u8 *end;
    u8 y, i;
    const u8 *color;

    FillWindowPixelBuffer(WIN_LEFT, PIXEL_FILL(COLOR_WHITE));

    // Species, level and gender
    y = 2;
    PrintText(WIN_LEFT, 4, y, sColor_Normal, gSpeciesNames[data->species]);
    end = StringCopy(buffer, sText_Lv);
    ConvertIntToDecimalStringN(end, data->level, STR_CONV_MODE_LEFT_ALIGN, 3);
    PrintTextRightAligned(WIN_LEFT, PANEL_WIDTH - 14, y, sColor_Normal, buffer);
    if (data->gender == MON_MALE)
        PrintText(WIN_LEFT, PANEL_WIDTH - 11, y, sColor_Blue, sText_Male);
    else if (data->gender == MON_FEMALE)
        PrintText(WIN_LEFT, PANEL_WIDTH - 11, y, sColor_Red, sText_Female);

    // Nature
    y += LINE_HEIGHT;
    PrintText(WIN_LEFT, 4, y, sColor_Label, sText_Nature);
    PrintText(WIN_LEFT, 44, y, sColor_Normal, gNatureNamePointers[data->nature]);

    // Ability
    y += LINE_HEIGHT;
    PrintText(WIN_LEFT, 4, y, sColor_Label, sText_Ability);
    PrintText(WIN_LEFT, 44, y, sColor_Normal, gAbilityNames[data->ability]);

    // Held item
    y += LINE_HEIGHT;
    PrintText(WIN_LEFT, 4, y, sColor_Label, sText_Item);
    if (data->item != ITEM_NONE)
    {
        CopyItemName(data->item, buffer);
        PrintText(WIN_LEFT, 44, y, sColor_Normal, buffer);
    }
    else
    {
        PrintText(WIN_LEFT, 44, y, sColor_Normal, sText_None);
    }

    // HP and status
    y += LINE_HEIGHT;
    PrintText(WIN_LEFT, 4, y, sColor_Label, sText_HP);
    end = ConvertIntToDecimalStringN(buffer, data->hp, STR_CONV_MODE_LEFT_ALIGN, 3);
    end = StringCopy(end, sText_Slash);
    ConvertIntToDecimalStringN(end, data->maxHp, STR_CONV_MODE_LEFT_ALIGN, 3);
    if (data->hp == 0)
        color = sColor_Red;
    else if (data->hp * 2 > data->maxHp)
        color = sColor_Green;
    else if (data->hp * 5 > data->maxHp)
        color = sColor_Orange;
    else
        color = sColor_Red;
    PrintText(WIN_LEFT, 44, y, color, buffer);
    {
        const u8 *statusText = NULL;
        if (data->hp == 0)
            statusText = sText_StatusFnt;
        else if (data->status & STATUS1_SLEEP)
            statusText = sText_StatusSlp;
        else if (data->status & STATUS1_TOXIC_POISON)
            statusText = sText_StatusTox;
        else if (data->status & STATUS1_POISON)
            statusText = sText_StatusPsn;
        else if (data->status & STATUS1_BURN)
            statusText = sText_StatusBrn;
        else if (data->status & STATUS1_FREEZE)
            statusText = sText_StatusFrz;
        else if (data->status & STATUS1_PARALYSIS)
            statusText = sText_StatusPar;
        if (statusText != NULL)
            PrintTextRightAligned(WIN_LEFT, PANEL_WIDTH - 4, y, sColor_Red, statusText);
    }

    // Hidden Power type and base power
    y += LINE_HEIGHT;
    PrintText(WIN_LEFT, 4, y, sColor_Label, sText_HiddenPower);
    end = StringCopy(buffer, gTypeNames[data->hiddenPowerType]);
    end = StringCopy(end, sText_Space);
    ConvertIntToDecimalStringN(end, data->hiddenPowerPower, STR_CONV_MODE_LEFT_ALIGN, 2);
    PrintText(WIN_LEFT, 44, y, sColor_Normal, buffer);

    // Separator above the moves
    y += LINE_HEIGHT + 1;
    FillWindowPixelRect(WIN_LEFT, PIXEL_FILL(COLOR_SEPARATOR), 4, y, PANEL_WIDTH - 8, 1);
    y += 2;

    // Moves with current/max PP
    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        if (data->moves[i] == MOVE_NONE)
        {
            PrintText(WIN_LEFT, 4, y, sColor_Label, sText_None);
        }
        else
        {
            PrintText(WIN_LEFT, 4, y, sColor_Normal, gMoveNames[data->moves[i]]);
            end = ConvertIntToDecimalStringN(buffer, data->pp[i], STR_CONV_MODE_LEFT_ALIGN, 2);
            end = StringCopy(end, sText_Slash);
            ConvertIntToDecimalStringN(end, data->maxPp[i], STR_CONV_MODE_LEFT_ALIGN, 2);
            if (data->pp[i] == 0)
                color = sColor_Red;
            else if (data->pp[i] * 4 <= data->maxPp[i])
                color = sColor_Orange;
            else
                color = sColor_Normal;
            PrintTextRightAligned(WIN_LEFT, PANEL_WIDTH - 4, y, color, buffer);
        }
        y += LINE_HEIGHT;
    }

    // Vertical divider between the panels
    FillWindowPixelRect(WIN_LEFT, PIXEL_FILL(COLOR_SEPARATOR), PANEL_WIDTH - 1, 2, 1, 16 * 8 - 4);

    PutWindowTilemap(WIN_LEFT);
    CopyWindowToVram(WIN_LEFT, COPYWIN_FULL);
}

#define COL_LABEL  4
#define COL_STAT   46
#define COL_IV     66
#define COL_EV     92
#define COL_STAGE  114

static void DrawRightPanel(const struct InfoPageData *data)
{
    u8 buffer[32];
    u8 *end;
    u8 y, i;
    u16 evTotal = 0;

    FillWindowPixelBuffer(WIN_RIGHT, PIXEL_FILL(COLOR_WHITE));

    // Column headings
    y = 2;
    PrintTextRightAligned(WIN_RIGHT, COL_STAT, y, sColor_Label, sText_ColStat);
    PrintTextRightAligned(WIN_RIGHT, COL_IV, y, sColor_Label, sText_ColIV);
    PrintTextRightAligned(WIN_RIGHT, COL_EV, y, sColor_Label, sText_ColEV);
    if (data->active)
        PrintTextRightAligned(WIN_RIGHT, COL_STAGE, y, sColor_Label, sText_ColStage);

    // One row per stat
    for (i = 0; i < NUM_STATS; i++)
    {
        u8 stat = sStatDisplayOrder[i];
        const u8 *labelColor = sColor_Normal;

        y += LINE_HEIGHT;
        if (stat != STAT_HP)
        {
            s8 natureEffect = gNatureStatTable[data->nature][stat - 1];
            if (natureEffect > 0)
                labelColor = sColor_Red;
            else if (natureEffect < 0)
                labelColor = sColor_Blue;
        }
        PrintText(WIN_RIGHT, COL_LABEL, y, labelColor, sStatNames[stat]);
        PrintNumberRightAligned(WIN_RIGHT, COL_STAT, y, sColor_Normal, data->stats[stat], 3);
        PrintNumberRightAligned(WIN_RIGHT, COL_IV, y, data->ivs[stat] == MAX_PER_STAT_IVS ? sColor_Green : sColor_Normal, data->ivs[stat], 2);
        PrintNumberRightAligned(WIN_RIGHT, COL_EV, y, sColor_Normal, data->evs[stat], 3);
        evTotal += data->evs[stat];

        if (data->active && stat != STAT_HP)
        {
            FormatStage(buffer, data->stages[stat]);
            PrintTextRightAligned(WIN_RIGHT, COL_STAGE, y, GetStageColor(data->stages[stat]), buffer);
        }
    }

    // Accuracy/evasion stages for a Pokemon in battle, plus the EV total
    y += LINE_HEIGHT + 3;
    FillWindowPixelRect(WIN_RIGHT, PIXEL_FILL(COLOR_SEPARATOR), 4, y - 2, PANEL_WIDTH - 8, 1);
    if (data->active)
    {
        PrintText(WIN_RIGHT, COL_LABEL, y, sColor_Label, sText_Acc);
        FormatStage(buffer, data->stages[STAT_ACC]);
        PrintText(WIN_RIGHT, COL_LABEL + 18, y, GetStageColor(data->stages[STAT_ACC]), buffer);
        PrintText(WIN_RIGHT, 40, y, sColor_Label, sText_Eva);
        FormatStage(buffer, data->stages[STAT_EVASION]);
        PrintText(WIN_RIGHT, 40 + 18, y, GetStageColor(data->stages[STAT_EVASION]), buffer);
    }
    end = StringCopy(buffer, sText_EVTotal);
    ConvertIntToDecimalStringN(end, evTotal, STR_CONV_MODE_LEFT_ALIGN, 3);
    PrintTextRightAligned(WIN_RIGHT, COL_STAGE, y, sColor_Label, buffer);

    // Tags
    y += LINE_HEIGHT;
    {
        u8 x = COL_LABEL;
        const u8 *tag;
        const u8 *tagColor;

        if (data->hp == 0)
        {
            tag = sText_Fainted;
            tagColor = sColor_Red;
        }
        else if (data->active)
        {
            tag = sText_InBattle;
            tagColor = sColor_Green;
        }
        else
        {
            tag = sText_InParty;
            tagColor = sColor_Label;
        }
        PrintText(WIN_RIGHT, x, y, tagColor, tag);
        x += GetStringWidth(FONT_SMALL, tag, 0) + 6;

        if (data->transformed)
        {
            PrintText(WIN_RIGHT, x, y, sColor_Blue, sText_Transformed);
            x += GetStringWidth(FONT_SMALL, sText_Transformed, 0) + 6;
        }
        if (data->shiny && x < PANEL_WIDTH - 30)
            PrintText(WIN_RIGHT, x, y, sColor_Orange, sText_Shiny);
    }

    // Controls
    y += LINE_HEIGHT;
    PrintText(WIN_RIGHT, COL_LABEL, y, sColor_Label, sText_Controls);

    PutWindowTilemap(WIN_RIGHT);
    CopyWindowToVram(WIN_RIGHT, COPYWIN_FULL);
}

static void DrawPage(void)
{
    struct InfoPageData data;

    GatherPageData(&data, sInfo->side, GetSelectedPartyIndex());
    DrawLeftPanel(&data);
    DrawRightPanel(&data);
}

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

static void GatherPageData(struct InfoPageData *data, u8 side, u8 partyIndex)
{
    struct Pokemon *mon = &GetSideParty(side)[partyIndex];
    u8 battler = GetActiveBattlerForSlot(side, partyIndex);
    u8 ppBonuses;
    u8 typeBits, powerBits;
    u8 i;

    memset(data, 0, sizeof(*data));

    data->species = GetMonData(mon, MON_DATA_SPECIES);
    data->level = GetMonData(mon, MON_DATA_LEVEL);
    data->gender = GetMonGender(mon);
    data->shiny = IsMonShiny(mon);
    data->nature = GetNature(mon);

    for (i = 0; i < NUM_STATS; i++)
    {
        data->stats[i] = GetMonData(mon, MON_DATA_MAX_HP + i);
        data->ivs[i] = GetMonData(mon, MON_DATA_HP_IV + i);
        data->evs[i] = GetMonData(mon, MON_DATA_HP_EV + i);
    }
    for (i = 0; i < NUM_BATTLE_STATS; i++)
        data->stages[i] = 0;

    if (battler < MAX_BATTLERS_COUNT)
    {
        // In battle: use the live battle copy for anything that can change
        // mid-battle (HP, status, PP, Mimic/Transform moves, Trace, items).
        struct BattlePokemon *bmon = &gBattleMons[battler];

        data->active = TRUE;
        data->transformed = (bmon->status2 & STATUS2_TRANSFORMED) ? TRUE : FALSE;
        data->ability = bmon->ability;
        data->item = bmon->item;
        data->hp = bmon->hp;
        data->maxHp = bmon->maxHP;
        data->status = bmon->status1;
        ppBonuses = bmon->ppBonuses;
        for (i = 0; i < MAX_MON_MOVES; i++)
        {
            data->moves[i] = bmon->moves[i];
            data->pp[i] = bmon->pp[i];
            if (data->moves[i] == MOVE_NONE)
                data->maxPp[i] = 0;
            else if (data->transformed)
                data->maxPp[i] = 5;
            else
                data->maxPp[i] = CalculatePPWithBonus(data->moves[i], ppBonuses, i);
            if (data->maxPp[i] < data->pp[i])
                data->maxPp[i] = data->pp[i];
        }
        for (i = 0; i < NUM_BATTLE_STATS; i++)
            data->stages[i] = bmon->statStages[i] - DEFAULT_STAT_STAGE;
    }
    else
    {
        data->active = FALSE;
        data->ability = GetAbilityBySpecies(data->species, GetMonData(mon, MON_DATA_ABILITY_NUM));
        data->item = GetMonData(mon, MON_DATA_HELD_ITEM);
        data->hp = GetMonData(mon, MON_DATA_HP);
        data->maxHp = GetMonData(mon, MON_DATA_MAX_HP);
        data->status = GetMonData(mon, MON_DATA_STATUS);
        ppBonuses = GetMonData(mon, MON_DATA_PP_BONUSES);
        for (i = 0; i < MAX_MON_MOVES; i++)
        {
            data->moves[i] = GetMonData(mon, MON_DATA_MOVE1 + i);
            data->pp[i] = GetMonData(mon, MON_DATA_PP1 + i);
            data->maxPp[i] = (data->moves[i] == MOVE_NONE) ? 0 : CalculatePPWithBonus(data->moves[i], ppBonuses, i);
        }
    }

    // Hidden Power, same formula as Cmd_hiddenpowercalc
    typeBits  = ((data->ivs[STAT_HP] & 1) << 0)
              | ((data->ivs[STAT_ATK] & 1) << 1)
              | ((data->ivs[STAT_DEF] & 1) << 2)
              | ((data->ivs[STAT_SPEED] & 1) << 3)
              | ((data->ivs[STAT_SPATK] & 1) << 4)
              | ((data->ivs[STAT_SPDEF] & 1) << 5);
    powerBits = ((data->ivs[STAT_HP] & 2) >> 1)
              | ((data->ivs[STAT_ATK] & 2) << 0)
              | ((data->ivs[STAT_DEF] & 2) << 1)
              | ((data->ivs[STAT_SPEED] & 2) << 2)
              | ((data->ivs[STAT_SPATK] & 2) << 3)
              | ((data->ivs[STAT_SPDEF] & 2) << 4);
    data->hiddenPowerType = (15 * typeBits) / 63 + 1;
    if (data->hiddenPowerType >= TYPE_MYSTERY)
        data->hiddenPowerType++;
    data->hiddenPowerPower = (40 * powerBits) / 63 + 30;
}
