// Battle Info Screen
//
// Press L+R on the battle action menu (FIGHT/BAG/POKEMON/RUN) or the move menu
// to open a full-screen view of every Pokemon on both sides of the battle:
// IVs, EVs, nature, stats, in-battle stat stages, ability, held item, HP,
// status, Hidden Power and moves with PP. The screen opens on the player's
// side, on the Pokemon being given a command (v1.34; it used to open on the
// opponent's side).
//
// Controls: D-pad = previous/next Pokemon, L/R = switch side,
//           A = toggle the stats table / damage calc (in battle),
//           SELECT = next target (damage view) or switch side (stats view),
//           B/START = back to the battle.
//
// The damage view shows, for each of this Pokemon's moves, the damage range it
// would do to the Pokemon selected on the other side (see battle_info_damage.c).
// That target is shown at the top of the panel with its icon, HP and status.
//
// The battle is suspended exactly like it is for the in-battle Bag and party
// menu: the battle graphics are torn down, and ReshowBattleScreenAfterMenu
// rebuilds them on exit. Nothing in the battle state is modified.
//
// The same screen can also show arbitrary lists of Pokemon outside battle
// (ShowMonListInfoScreen), used by the Battle Factory rental and swap screens.

#include "global.h"
#include "event_data.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_info_damage.h"
#include "battle_info_screen.h"
#include "battle_main.h"
#include "battle_message.h"
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
// Party icons line up at the right of the header (the last one centered at
// ICON_X_LAST), leaving the left for the names
#define ICON_X_LAST      222
#define ICON_Y           17
#define TITLE_LENGTH     32
#define SHORT_NAME_LENGTH 16

// Damage view: the target's icon sits at the top-left of the right panel
#define TARGET_ICON_X    (PANEL_WIDTH + 16)
#define TARGET_ICON_Y    (4 * 8 + 15)
#define TARGET_TEXT_X    34

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
    bool8 inBattle;
    u8 numSides;
    struct Pokemon *parties[INFO_SIDE_COUNT];
    u8 partySize[INFO_SIDE_COUNT];
    u8 titles[INFO_SIDE_COUNT][TITLE_LENGTH];          // e.g. "ANDREW", "COOLTRAINER JIM"
    u8 shortNames[INFO_SIDE_COUNT][SHORT_NAME_LENGTH]; // e.g. "ANDREW", "JIM" (for the L/R hint)
    u16 colors[INFO_SIDE_COUNT];
    u8 picks[INFO_SIDE_COUNT][PARTY_SIZE];
    bool8 asHealed[INFO_SIDE_COUNT];
    u8 side;
    u8 count[INFO_SIDE_COUNT];
    u8 slots[INFO_SIDE_COUNT][PARTY_SIZE];
    u8 cursor[INFO_SIDE_COUNT];
    u8 iconSpriteIds[PARTY_SIZE];
    u8 iconCount;
    u8 targetIconId;    // damage view: the target's icon, or MAX_SPRITES
    bool8 failed;
    bool8 damageView;   // right panel shows the damage calc instead of stats
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
static void BuildBattleSideLists(void);
static void BuildListSideLists(u8 startSide, u8 startSlot);
static void CreateSideIcons(void);
static void DestroySideIcons(void);
static void UpdateIconSelection(void);
static void DrawHeader(void);
static void DrawPage(void);
static void RedrawDamagePanel(void);
static void DestroyTargetIcon(void);
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
        .width = 30, // full width: long trainer names; the icons sit on top
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
        .baseBlock = 1 + 30 * 4,
    },
    [WIN_RIGHT] = {
        .bg = 0,
        .tilemapLeft = 15,
        .tilemapTop = 4,
        .width = 15,
        .height = 16,
        .paletteNum = 15,
        .baseBlock = 1 + 30 * 4 + 15 * 16,
    },
    DUMMY_WIN_TEMPLATE
};

// Display order of the stats table (party/battle structs use HP, Atk, Def, Spe, SpA, SpD)
static const u8 sStatDisplayOrder[NUM_STATS] = {STAT_HP, STAT_ATK, STAT_DEF, STAT_SPATK, STAT_SPDEF, STAT_SPEED};

// Header names in battle (v1.35): your name, and the opponent's class and name
static const u8 sText_TrainerAFull[] = _("{B_TRAINER1_CLASS} {B_TRAINER1_NAME}");
static const u8 sText_TrainerAName[] = _("{B_TRAINER1_NAME}");
static const u8 sText_TwoTrainers[] = _("{B_TRAINER1_NAME} & {B_TRAINER2_NAME}");
static const u8 sText_Wild[] = _("WILD POKéMON");
static const u8 sText_WildShort[] = _("WILD");
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
// L/R (switch side) is shown in the header, next to the other side's name
static const u8 sText_Controls[] = _("{B_BUTTON} Back");
static const u8 sText_ControlsBattle[] = _("{A_BUTTON} Damage calc  {B_BUTTON} Back");
static const u8 sText_ControlsDamage[] = _("{SELECT_BUTTON} Target  {A_BUTTON} Stats");
static const u8 sText_HeaderSwitch[] = _("{L_BUTTON}{R_BUTTON}");
static const u8 sText_Vs[] = _("vs ");
static const u8 sText_ColDmg[] = _("DMG%");
static const u8 sText_ColKO[] = _("KO");
static const u8 sText_ColCrit[] = _("CRIT%");
static const u8 sText_Dash[] = _("-");
static const u8 sText_Times[] = _("x");
static const u8 sText_HKO[] = _("HKO");
static const u8 sText_OHKO[] = _("OHKO");
static const u8 sText_Maybe[] = _("?");
static const u8 sText_Immune[] = _("IMMUNE");
static const u8 sText_Fails[] = _("FAILS");
static const u8 sText_Varies[] = _("VARIES");
static const u8 sText_SwitchIn[] = _("As if switched in");
static const u8 sText_NoTarget[] = _("No target");
static const u8 sText_FieldRain[] = _("RAIN");
static const u8 sText_FieldSun[] = _("SUN");
static const u8 sText_FieldSand[] = _("SAND");
static const u8 sText_FieldHail[] = _("HAIL");
static const u8 sText_FieldReflect[] = _("REFLECT");
static const u8 sText_FieldLightScreen[] = _("L.SCREEN");
static const u8 sText_FieldMudSport[] = _("MUD SPORT");
static const u8 sText_FieldWaterSport[] = _("WTR SPORT");
static const u8 sText_FieldCharge[] = _("CHARGE");
static const u8 sText_FieldFlashFire[] = _("FLASH FIRE");
static const u8 sText_ControlsOneSide[] = _("{B_BUTTON} Back");
static const u8 sText_Picked[] = _("PICK ");

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

// With the L=A button option (the default on new saves before v1.34; still
// selectable in OPTION), a lone L press counts as A. Hold that A back for a few frames so that
// L landing slightly before R opens this screen instead of choosing FIGHT or a
// move. A real A press, or letting go of L, delivers it immediately.
#define L_EQUALS_A_GRACE_FRAMES 5

static EWRAM_DATA u8 sLEqualsAGraceFrames = 0;
static EWRAM_DATA bool8 sDamageViewLastUsed = FALSE; // reopen on the view used last

static u8 CheckCombo(bool8 enabled)
{
    if (!enabled)
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

// For the battle action/move menus. Disabled in link battles and recorded-battle playback.
u8 BattleInfoScreen_CheckCombo(void)
{
    return CheckCombo(!(gBattleTypeFlags & (BATTLE_TYPE_LINK | BATTLE_TYPE_RECORDED)) && !FlagGet(FLAG_OPT_INFO_SCREEN_OFF));
}

// For menus outside battle (Battle Factory rental/swap screens).
u8 InfoScreen_CheckComboOutsideBattle(void)
{
    return CheckCombo(!FlagGet(FLAG_OPT_INFO_SCREEN_OFF)); // v1.39: OPTION > L+R INFO
}

static bool8 AllocInfoScreen(MainCallback exitCallback)
{
    sInfo = AllocZeroed(sizeof(*sInfo));
    if (sInfo == NULL)
    {
        // Not enough memory: go straight back.
        SetMainCallback2(exitCallback);
        return FALSE;
    }

    sInfo->exitCallback = exitCallback;
    sInfo->targetIconId = MAX_SPRITES;
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
    return TRUE;
}

// Copies at most size - 1 characters
static void CopyName(u8 *dst, const u8 *src, u8 size)
{
    u8 i;

    for (i = 0; i < size - 1 && src[i] != EOS; i++)
        dst[i] = src[i];
    dst[i] = EOS;
}

static void SetSideNames(u8 side, const u8 *title, const u8 *shortName)
{
    CopyName(sInfo->titles[side], title, TITLE_LENGTH);
    CopyName(sInfo->shortNames[side], shortName != NULL ? shortName : title, SHORT_NAME_LENGTH);
}

// The opponent as the battle itself names them ("COOLTRAINER JIM")
static void SetOpponentNames(void)
{
    u8 full[64];
    u8 name[32];

    if (!(gBattleTypeFlags & BATTLE_TYPE_TRAINER))
    {
        SetSideNames(INFO_SIDE_FOE, sText_Wild, sText_WildShort);
    }
    else if (gBattleTypeFlags & BATTLE_TYPE_TWO_OPPONENTS)
    {
        BattleStringExpandPlaceholders(sText_TwoTrainers, full);
        SetSideNames(INFO_SIDE_FOE, full, full);
    }
    else
    {
        BattleStringExpandPlaceholders(sText_TrainerAFull, full);
        BattleStringExpandPlaceholders(sText_TrainerAName, name);
        SetSideNames(INFO_SIDE_FOE, full, name);
    }
}

void ShowBattleInfoScreen(MainCallback exitCallback)
{
    if (!AllocInfoScreen(exitCallback))
        return;

    sInfo->inBattle = TRUE;
    sInfo->damageView = sDamageViewLastUsed;
    sInfo->numSides = INFO_SIDE_COUNT;
    sInfo->parties[INFO_SIDE_FOE] = gEnemyParty;
    sInfo->parties[INFO_SIDE_PLAYER] = gPlayerParty;
    sInfo->partySize[INFO_SIDE_FOE] = PARTY_SIZE;
    sInfo->partySize[INFO_SIDE_PLAYER] = PARTY_SIZE;
    SetOpponentNames();
    SetSideNames(INFO_SIDE_PLAYER, gSaveBlock2Ptr->playerName, NULL);
    sInfo->colors[INFO_SIDE_FOE] = BG_COLOR_FOE;
    sInfo->colors[INFO_SIDE_PLAYER] = BG_COLOR_PLAYER;
    BuildBattleSideLists();

    gMain.state = 0;
    SetMainCallback2(CB2_BattleInfoSetup);
}

void ShowMonListInfoScreen(const struct InfoScreenSideArgs *sides, u8 numSides, u8 startSide, u8 startSlot, MainCallback exitCallback)
{
    u8 side, i;

    if (!AllocInfoScreen(exitCallback))
        return;

    if (numSides > INFO_SIDE_COUNT)
        numSides = INFO_SIDE_COUNT;
    sInfo->inBattle = FALSE;
    sInfo->numSides = numSides;
    for (side = 0; side < numSides; side++)
    {
        sInfo->parties[side] = sides[side].party;
        sInfo->partySize[side] = min(sides[side].count, PARTY_SIZE);
        SetSideNames(side, sides[side].title, sides[side].shortName);
        sInfo->colors[side] = sides[side].color;
        sInfo->asHealed[side] = sides[side].asHealed;
        for (i = 0; i < PARTY_SIZE; i++)
            sInfo->picks[side][i] = (sides[side].picks != NULL && i < sInfo->partySize[side]) ? sides[side].picks[i] : 0;
    }
    BuildListSideLists(startSide < numSides ? startSide : 0, startSlot);
    if (sInfo->count[sInfo->side] == 0)
    {
        // Nothing to show
        FREE_AND_SET_NULL(sInfo);
        SetMainCallback2(exitCallback);
        return;
    }

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
    else if ((gMain.newKeysRaw & A_BUTTON) && sInfo->inBattle)
    {
        // Raw A: with the L=A option, L sets A in newKeys and must only switch sides
        PlaySE(SE_SELECT);
        sInfo->damageView ^= 1;
        sDamageViewLastUsed = sInfo->damageView;
        DrawPage();
    }
    else if (JOY_NEW(SELECT_BUTTON) && sInfo->damageView && sInfo->inBattle)
    {
        // Next target: the selected Pokemon on the other side
        u8 targetSide = side ^ 1;
        if (sInfo->count[targetSide] > 1)
        {
            PlaySE(SE_SELECT);
            if (++sInfo->cursor[targetSide] >= sInfo->count[targetSide])
                sInfo->cursor[targetSide] = 0;
            RedrawDamagePanel();
        }
    }
    else if (JOY_NEW(L_BUTTON | R_BUTTON | SELECT_BUTTON) && sInfo->numSides > 1 && sInfo->count[side ^ 1] != 0)
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
        DestroyTargetIcon();
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
    return sInfo->parties[side];
}

static u8 GetBattlerSideFor(u8 side)
{
    return (side == INFO_SIDE_FOE) ? B_SIDE_OPPONENT : B_SIDE_PLAYER;
}

// Returns the battler currently holding this party slot, or MAX_BATTLERS_COUNT.
static u8 GetActiveBattlerForSlot(u8 side, u8 partyIndex)
{
    u8 battler;

    if (!sInfo->inBattle)
        return MAX_BATTLERS_COUNT;

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

static void BuildSideSlots(u8 side)
{
    struct Pokemon *party = GetSideParty(side);
    u8 i;

    sInfo->count[side] = 0;
    sInfo->cursor[side] = 0;
    if (party == NULL)
        return;

    for (i = 0; i < sInfo->partySize[side]; i++)
    {
        u16 species = GetMonData(&party[i], MON_DATA_SPECIES);
        if (species == SPECIES_NONE || species >= NUM_SPECIES)
            continue;
        if (GetMonData(&party[i], MON_DATA_IS_EGG))
            continue;
        sInfo->slots[side][sInfo->count[side]++] = i;
    }
}

static void SetCursorToSlot(u8 side, u8 partyIndex)
{
    u8 j;

    for (j = 0; j < sInfo->count[side]; j++)
    {
        if (sInfo->slots[side][j] == partyIndex)
        {
            sInfo->cursor[side] = j;
            return;
        }
    }
}

static void BuildBattleSideLists(void)
{
    u8 side;

    for (side = 0; side < INFO_SIDE_COUNT; side++)
    {
        u8 firstActive = 0xFF;
        u8 focusBattler;

        BuildSideSlots(side);

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

        SetCursorToSlot(side, firstActive);
    }

    // v1.37: the foe's side first, unless OPTION > INFO START is YOURS
    sInfo->side = gSaveBlock2Ptr->optionsInfoStartYours ? INFO_SIDE_PLAYER : INFO_SIDE_FOE;
}

static void BuildListSideLists(u8 startSide, u8 startSlot)
{
    u8 side;

    for (side = 0; side < INFO_SIDE_COUNT; side++)
    {
        if (side < sInfo->numSides)
            BuildSideSlots(side);
        else
            sInfo->count[side] = 0;
    }
    SetCursorToSlot(startSide, startSlot);
    sInfo->side = startSide;
}

static u8 GetSelectedPartyIndex(void)
{
    return sInfo->slots[sInfo->side][sInfo->cursor[sInfo->side]];
}

// ---------------------------------------------------------------------------
// Party icon strip
// ---------------------------------------------------------------------------

static u8 GetIconSpacing(void)
{
    return (sInfo->count[sInfo->side] <= 4) ? 28 : 24;
}

static u8 GetIconX(u8 i)
{
    return ICON_X_LAST - GetIconSpacing() * (sInfo->count[sInfo->side] - 1 - i);
}

// Room for the names, left of the first icon
static u8 GetHeaderTextWidth(void)
{
    if (sInfo->count[sInfo->side] == 0)
        return 236;
    return GetIconX(0) - 16 - 6;
}

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
        u8 spriteId = CreateMonIcon(species, SpriteCB_MonIcon, GetIconX(i), ICON_Y, 1, personality, TRUE);

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

// The side's name and position on the first line ("COOLTRAINER JIM 1/3");
// the second line says where L/R goes ("{L}{R} ANDREW"). A name too long
// for the room left of the icons drops to the small font, then to the name
// without the class.
static void DrawHeader(void)
{
    u8 buffer[40];
    u8 position[8];
    u8 *end;
    u8 side = sInfo->side;
    u8 room = GetHeaderTextWidth();
    u8 posWidth;
    const u8 *title = sInfo->titles[side];
    u8 font = FONT_NORMAL;

    FillWindowPixelBuffer(WIN_HEADER, PIXEL_FILL(COLOR_TRANSPARENT));

    end = ConvertIntToDecimalStringN(position, sInfo->cursor[side] + 1, STR_CONV_MODE_LEFT_ALIGN, 1);
    end = StringCopy(end, sText_Slash);
    ConvertIntToDecimalStringN(end, sInfo->count[side], STR_CONV_MODE_LEFT_ALIGN, 1);
    posWidth = GetStringWidth(FONT_SMALL, position, 0);

    if (4 + GetStringWidth(FONT_NORMAL, title, 0) + 4 + posWidth > room)
    {
        if (4 + GetStringWidth(FONT_SMALL, title, 0) + 4 + posWidth <= room)
            font = FONT_SMALL;
        else
            title = sInfo->shortNames[side];
    }
    AddTextPrinterParameterized3(WIN_HEADER, font, 4, font == FONT_NORMAL ? 1 : 3, sColor_Header, TEXT_SKIP_DRAW, title);
    AddTextPrinterParameterized3(WIN_HEADER, FONT_SMALL, 4 + GetStringWidth(font, title, 0) + 4, 3,
                                 sColor_Header, TEXT_SKIP_DRAW, position);

    if (sInfo->numSides > 1 && sInfo->count[side ^ 1] != 0)
    {
        end = StringCopy(buffer, sText_HeaderSwitch);
        end = StringCopy(end, sText_Space);
        StringCopy(end, sInfo->shortNames[side ^ 1]);
        AddTextPrinterParameterized3(WIN_HEADER, FONT_SMALL, 4, 17, sColor_Header, TEXT_SKIP_DRAW, buffer);
    }

    PutWindowTilemap(WIN_HEADER);
    CopyWindowToVram(WIN_HEADER, COPYWIN_FULL);

    // Side-colored backdrop
    {
        u16 color = sInfo->colors[sInfo->side];
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

        u8 pickBuffer[16];
        u8 pick = sInfo->picks[sInfo->side][GetSelectedPartyIndex()];

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
        else if (sInfo->inBattle)
        {
            tag = sText_InParty;
            tagColor = sColor_Label;
        }
        else if (pick != 0)
        {
            ConvertIntToDecimalStringN(StringCopy(pickBuffer, sText_Picked), pick, STR_CONV_MODE_LEFT_ALIGN, 1);
            tag = pickBuffer;
            tagColor = sColor_Green;
        }
        else
        {
            tag = NULL;
            tagColor = sColor_Label;
        }
        if (tag != NULL)
        {
            PrintText(WIN_RIGHT, x, y, tagColor, tag);
            x += GetStringWidth(FONT_SMALL, tag, 0) + 6;
        }

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
    if (sInfo->inBattle)
        PrintText(WIN_RIGHT, COL_LABEL, y, sColor_Label, sText_ControlsBattle);
    else
        PrintText(WIN_RIGHT, COL_LABEL, y, sColor_Label, sInfo->numSides > 1 ? sText_Controls : sText_ControlsOneSide);

    PutWindowTilemap(WIN_RIGHT);
    CopyWindowToVram(WIN_RIGHT, COPYWIN_FULL);
}

// ---------------------------------------------------------------------------
// Damage view (right panel, in battle)
// ---------------------------------------------------------------------------

// Right edges of the damage columns. Widest values: "999-999" (35px), "9HKO?" (25px).
#define DMG_COL_DMG    40
#define DMG_COL_KO     71
#define DMG_COL_CRIT   114
#define MOVE_ROW_Y     77      // first move row, level with the moves on the left panel

static const u8 *GetHpColor(u16 hp, u16 maxHp)
{
    if (hp == 0)
        return sColor_Red;
    if (hp * 2 > maxHp)
        return sColor_Green;
    if (hp * 5 > maxHp)
        return sColor_Orange;
    return sColor_Red;
}

static const u8 *GetStatusText(u16 hp, u32 status)
{
    if (hp == 0)
        return sText_StatusFnt;
    if (status & STATUS1_SLEEP)
        return sText_StatusSlp;
    if (status & STATUS1_TOXIC_POISON)
        return sText_StatusTox;
    if (status & STATUS1_POISON)
        return sText_StatusPsn;
    if (status & STATUS1_BURN)
        return sText_StatusBrn;
    if (status & STATUS1_FREEZE)
        return sText_StatusFrz;
    if (status & STATUS1_PARALYSIS)
        return sText_StatusPar;
    return NULL;
}

// "38-45" (percent of max HP, low end rounded down and high end up)
static void FormatPercentRange(u8 *dst, s32 lo, s32 hi, u16 maxHp)
{
    s32 loPct, hiPct;

    if (maxHp == 0)
        maxHp = 1;
    loPct = lo * 100 / maxHp;
    hiPct = (hi * 100 + maxHp - 1) / maxHp;
    if (loPct > 999)
        loPct = 999;
    if (hiPct > 999)
        hiPct = 999;
    dst = ConvertIntToDecimalStringN(dst, loPct, STR_CONV_MODE_LEFT_ALIGN, 3);
    if (hiPct != loPct)
    {
        dst = StringCopy(dst, sText_Dash);
        ConvertIntToDecimalStringN(dst, hiPct, STR_CONV_MODE_LEFT_ALIGN, 3);
    }
}

// "OHKO" / "2HKO" when every roll does it; "OHKO?" / "2HKO?" when only some rolls do.
static const u8 *FormatKO(u8 *dst, const struct DamageCalcResult *result, u16 hp)
{
    u32 guaranteed, possible;

    *dst = EOS;
    if (result->cantKO || hp == 0 || result->max <= 0)
        return sColor_Normal;

    possible = (hp + result->max - 1) / result->max;
    guaranteed = (result->min > 0) ? (hp + result->min - 1) / result->min : 0xFF;
    if (possible > 9)
        return sColor_Normal;

    if (possible == 1)
    {
        dst = StringCopy(dst, sText_OHKO);
    }
    else
    {
        dst = ConvertIntToDecimalStringN(dst, possible, STR_CONV_MODE_LEFT_ALIGN, 1);
        dst = StringCopy(dst, sText_HKO);
    }

    if (guaranteed != possible)
    {
        StringCopy(dst, sText_Maybe);
        return (possible == 1) ? sColor_Orange : sColor_Normal;
    }
    return (possible == 1) ? sColor_Red : sColor_Normal;
}

// Field conditions that change the numbers, e.g. "RAIN  REFLECT"
static void DrawFieldEffects(u8 x, u8 y, const struct DamageCalcMon *attacker, const struct DamageCalcMon *defender)
{
    const u8 *items[8];
    u8 count = 0, i;
    u16 weather = DamageCalc_GetActiveWeather();
    u16 sideStatus = gSideStatuses[GetBattlerSide(defender->battler)];
    bool8 mudSport = FALSE, waterSport = FALSE;

    if (weather & B_WEATHER_RAIN)
        items[count++] = sText_FieldRain;
    else if (weather & B_WEATHER_SUN)
        items[count++] = sText_FieldSun;
    else if (weather & B_WEATHER_SANDSTORM)
        items[count++] = sText_FieldSand;
    else if (weather & B_WEATHER_HAIL)
        items[count++] = sText_FieldHail;
    if (sideStatus & SIDE_STATUS_REFLECT)
        items[count++] = sText_FieldReflect;
    if (sideStatus & SIDE_STATUS_LIGHTSCREEN)
        items[count++] = sText_FieldLightScreen;
    for (i = 0; i < gBattlersCount; i++)
    {
        if (gStatuses3[i] & STATUS3_MUDSPORT)
            mudSport = TRUE;
        if (gStatuses3[i] & STATUS3_WATERSPORT)
            waterSport = TRUE;
    }
    if (mudSport)
        items[count++] = sText_FieldMudSport;
    if (waterSport)
        items[count++] = sText_FieldWaterSport;
    if (attacker->active && (gStatuses3[attacker->battler] & STATUS3_CHARGED_UP))
        items[count++] = sText_FieldCharge;
    if (attacker->active && (gBattleResources->flags->flags[attacker->battler] & RESOURCE_FLAG_FLASH_FIRE))
        items[count++] = sText_FieldFlashFire;

    for (i = 0; i < count; i++)
    {
        u8 width = GetStringWidth(FONT_SMALL, items[i], 0);
        if (x + width > PANEL_WIDTH - 4)
            break;
        PrintText(WIN_RIGHT, x, y, sColor_Orange, items[i]);
        x += width + 6;
    }
}

// A small HP bar (like the one in battle), width + 2 by 5 pixels with its border
static void DrawHpBar(u8 windowId, u8 x, u8 y, u8 width, u16 hp, u16 maxHp)
{
    u32 fill = 0;
    u8 color;

    if (maxHp != 0 && hp != 0)
    {
        fill = ((u32)hp * width + maxHp - 1) / maxHp; // any HP left shows at least a pixel
        if (fill > width)
            fill = width;
    }
    if (hp * 2 > maxHp)
        color = COLOR_GREEN;
    else if (hp * 5 > maxHp)
        color = COLOR_ORANGE;
    else
        color = COLOR_RED;

    FillWindowPixelRect(windowId, PIXEL_FILL(COLOR_DARK_GRAY), x - 1, y - 1, width + 2, 5);
    FillWindowPixelRect(windowId, PIXEL_FILL(COLOR_LIGHT_GRAY), x, y, width, 3);
    if (fill != 0)
        FillWindowPixelRect(windowId, PIXEL_FILL(color), x, y, fill, 3);
}

static void DestroyTargetIcon(void)
{
    if (sInfo->targetIconId < MAX_SPRITES)
        FreeAndDestroyMonIconSprite(&gSprites[sInfo->targetIconId]);
    sInfo->targetIconId = MAX_SPRITES;
}

// The damage view shows the target's icon at the top of the right panel
static void UpdateTargetIcon(void)
{
    u8 targetSide = sInfo->side ^ 1;

    DestroyTargetIcon();
    if (sInfo->damageView && sInfo->inBattle && sInfo->count[targetSide] != 0)
    {
        struct Pokemon *mon = &GetSideParty(targetSide)[sInfo->slots[targetSide][sInfo->cursor[targetSide]]];
        u8 spriteId = CreateMonIcon(GetMonData(mon, MON_DATA_SPECIES), SpriteCB_MonIcon, TARGET_ICON_X, TARGET_ICON_Y, 1,
                                    GetMonData(mon, MON_DATA_PERSONALITY), TRUE);
        if (spriteId < MAX_SPRITES)
            gSprites[spriteId].oam.priority = 0;
        sInfo->targetIconId = spriteId;
    }
}

static void DrawDamagePanel(const struct InfoPageData *data)
{
    u8 buffer[32];
    u8 *end;
    u8 y, i;
    u8 side = sInfo->side;
    u8 targetSide = side ^ 1;
    u8 partyIndex = GetSelectedPartyIndex();
    u8 targetIndex;
    struct DamageCalcMon attacker, defender;
    const struct BattlePokemon *target;
    const u8 *statusText;

    FillWindowPixelBuffer(WIN_RIGHT, PIXEL_FILL(COLOR_WHITE));

    if (sInfo->count[targetSide] == 0)
    {
        PrintText(WIN_RIGHT, COL_LABEL, 2, sColor_Label, sText_NoTarget);
        PrintText(WIN_RIGHT, COL_LABEL, 50, sColor_Label, sText_ControlsDamage);
        PutWindowTilemap(WIN_RIGHT);
        CopyWindowToVram(WIN_RIGHT, COPYWIN_FULL);
        return;
    }

    // The target is whichever Pokemon is selected on the other side
    targetIndex = sInfo->slots[targetSide][sInfo->cursor[targetSide]];
    DamageCalc_InitMon(&attacker, GetSideParty(side), partyIndex, GetBattlerSideFor(side), GetActiveBattlerForSlot(side, partyIndex));
    DamageCalc_InitMon(&defender, GetSideParty(targetSide), targetIndex, GetBattlerSideFor(targetSide), GetActiveBattlerForSlot(targetSide, targetIndex));
    target = &defender.mon;

    // The target, next to its icon (UpdateTargetIcon): species and level,
    // HP and status, with an HP bar under the icon
    // "vs SPECIES" on the first line, "Lv50 HP 153/153" on the second (a
    // 10-letter name leaves no room for the level next to it)
    y = 2;
    PrintText(WIN_RIGHT, TARGET_TEXT_X, y, sColor_Label, sText_Vs);
    PrintText(WIN_RIGHT, TARGET_TEXT_X + GetStringWidth(FONT_SMALL, sText_Vs, 0), y, sColor_Normal,
              gSpeciesNames[GetMonData(&GetSideParty(targetSide)[targetIndex], MON_DATA_SPECIES)]);

    y += LINE_HEIGHT;
    {
        u8 x = TARGET_TEXT_X;
        end = StringCopy(buffer, sText_Lv);
        ConvertIntToDecimalStringN(end, target->level, STR_CONV_MODE_LEFT_ALIGN, 3);
        PrintText(WIN_RIGHT, x, y, sColor_Label, buffer);
        x += GetStringWidth(FONT_SMALL, buffer, 0) + 4;
        PrintText(WIN_RIGHT, x, y, sColor_Label, sText_HP);
        x += GetStringWidth(FONT_SMALL, sText_HP, 0) + 2;
        end = ConvertIntToDecimalStringN(buffer, target->hp, STR_CONV_MODE_LEFT_ALIGN, 3);
        end = StringCopy(end, sText_Slash);
        ConvertIntToDecimalStringN(end, target->maxHP, STR_CONV_MODE_LEFT_ALIGN, 3);
        PrintText(WIN_RIGHT, x, y, GetHpColor(target->hp, target->maxHP), buffer);
    }
    statusText = GetStatusText(target->hp, target->status1);
    if (statusText != NULL)
        PrintTextRightAligned(WIN_RIGHT, PANEL_WIDTH - 4, y, sColor_Red, statusText);
    DrawHpBar(WIN_RIGHT, 5, 33, 22, target->hp, target->maxHP);

    // Weather, screens and other modifiers in effect
    y += LINE_HEIGHT;
    DrawFieldEffects(TARGET_TEXT_X, y, &attacker, &defender);

    // Either side not in battle: calc assumes it switches in (no stat stages)
    y += LINE_HEIGHT;
    if (!attacker.active || !defender.active)
        PrintText(WIN_RIGHT, COL_LABEL, y, sColor_Orange, sText_SwitchIn);

    y += LINE_HEIGHT;
    PrintText(WIN_RIGHT, COL_LABEL, y, sColor_Label, sText_ControlsDamage);

    // Column headings, then one row per move, level with the move list on the left
    y = MOVE_ROW_Y - LINE_HEIGHT - 2;
    PrintTextRightAligned(WIN_RIGHT, DMG_COL_DMG, y, sColor_Label, sText_ColDmg);
    PrintTextRightAligned(WIN_RIGHT, DMG_COL_KO, y, sColor_Label, sText_ColKO);
    PrintTextRightAligned(WIN_RIGHT, DMG_COL_CRIT, y, sColor_Label, sText_ColCrit);
    FillWindowPixelRect(WIN_RIGHT, PIXEL_FILL(COLOR_SEPARATOR), 4, MOVE_ROW_Y - 2, PANEL_WIDTH - 8, 1);

    y = MOVE_ROW_Y;
    for (i = 0; i < MAX_MON_MOVES; i++, y += LINE_HEIGHT)
    {
        struct DamageCalcResult result;
        const u8 *color;

        if (data->moves[i] == MOVE_NONE)
            continue;

        DamageCalc_Move(&attacker, &defender, data->moves[i], &result);
        switch (result.kind)
        {
        case DMG_CALC_STATUS:
            PrintTextRightAligned(WIN_RIGHT, DMG_COL_DMG, y, sColor_Label, sText_Dash);
            break;
        case DMG_CALC_IMMUNE:
            PrintTextRightAligned(WIN_RIGHT, DMG_COL_DMG, y, sColor_Blue, sText_Immune);
            break;
        case DMG_CALC_FAILS:
            PrintTextRightAligned(WIN_RIGHT, DMG_COL_DMG, y, sColor_Label, sText_Fails);
            break;
        case DMG_CALC_VARIES:
            PrintTextRightAligned(WIN_RIGHT, DMG_COL_DMG, y, sColor_Label, sText_Varies);
            break;
        case DMG_CALC_OHKO:
            PrintTextRightAligned(WIN_RIGHT, DMG_COL_DMG, y, sColor_Red, sText_OHKO);
            break;
        case DMG_CALC_HIT:
            if (result.effectiveness & MOVE_RESULT_SUPER_EFFECTIVE)
                color = sColor_Red;
            else if (result.effectiveness & MOVE_RESULT_NOT_VERY_EFFECTIVE)
                color = sColor_Blue;
            else
                color = sColor_Normal;
            FormatPercentRange(buffer, result.min, result.max, target->maxHP);
            PrintTextRightAligned(WIN_RIGHT, DMG_COL_DMG, y, color, buffer);

            color = FormatKO(buffer, &result, target->hp);
            if (buffer[0] != EOS)
                PrintTextRightAligned(WIN_RIGHT, DMG_COL_KO, y, color, buffer);

            if (result.hasCrit)
            {
                FormatPercentRange(buffer, result.critMin, result.critMax, target->maxHP);
                PrintTextRightAligned(WIN_RIGHT, DMG_COL_CRIT, y, sColor_Label, buffer);
            }
            else if (result.maxHits > 1)
            {
                end = StringCopy(buffer, sText_Times);
                end = ConvertIntToDecimalStringN(end, result.minHits, STR_CONV_MODE_LEFT_ALIGN, 1);
                if (result.maxHits != result.minHits)
                {
                    end = StringCopy(end, sText_Dash);
                    ConvertIntToDecimalStringN(end, result.maxHits, STR_CONV_MODE_LEFT_ALIGN, 1);
                }
                PrintTextRightAligned(WIN_RIGHT, DMG_COL_CRIT, y, sColor_Label, buffer);
            }
            break;
        }
    }

    // Vertical divider is on the left panel; nothing else to draw
    PutWindowTilemap(WIN_RIGHT);
    CopyWindowToVram(WIN_RIGHT, COPYWIN_FULL);
}

static void DrawPage(void)
{
    struct InfoPageData data;

    GatherPageData(&data, sInfo->side, GetSelectedPartyIndex());
    DrawLeftPanel(&data);
    if (sInfo->damageView && sInfo->inBattle)
        DrawDamagePanel(&data);
    else
        DrawRightPanel(&data);
    UpdateTargetIcon();
}

// Only the target changed (SELECT in the damage view)
static void RedrawDamagePanel(void)
{
    struct InfoPageData data;

    GatherPageData(&data, sInfo->side, GetSelectedPartyIndex());
    DrawDamagePanel(&data);
    UpdateTargetIcon();
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

    // Shown as it will be after the next heal (Factory swap candidates)
    if (sInfo->asHealed[side])
    {
        data->hp = data->maxHp;
        data->status = 0;
        for (i = 0; i < MAX_MON_MOVES; i++)
            data->pp[i] = data->maxPp[i];
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
