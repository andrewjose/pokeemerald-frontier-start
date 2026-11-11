#include "global.h"
#include "option_menu.h"
#include "bg.h"
#include "gpu_regs.h"
#include "international_string_util.h"
#include "main.h"
#include "menu.h"
#include "palette.h"
#include "scanline_effect.h"
#include "sprite.h"
#include "strings.h"
#include "task.h"
#include "text.h"
#include "event_data.h"
#include "sound.h"
#include "constants/songs.h"
#include "list_menu.h"
#include "text_window.h"
#include "window.h"
#include "gba/m4a_internal.h"
#include "constants/rgb.h"

#define tMenuSelection data[0]
#define tTextSpeed data[1]
#define tBattleSceneOff data[2]
#define tBattleStyle data[3]
#define tSound data[4]
#define tButtonMode data[5]
#define tWindowFrameType data[6]
#define tDamagePreviewOff data[7]
#define tInfoStartYours data[8]
#define tArrowTaskId data[9]
#define tFlagOptions(i) data[10 + (i)] // v1.39: the flag options, as left (0) / right (1)

enum
{
    MENUITEM_TEXTSPEED,
    MENUITEM_BATTLESCENE,
    MENUITEM_BATTLESTYLE,
    MENUITEM_SOUND,
    MENUITEM_BUTTONMODE,
    MENUITEM_FRAMETYPE,
    MENUITEM_INFOSCREEN,
    MENUITEM_INFOSTART,
    MENUITEM_DMGPREVIEW,
    MENUITEM_EDITMENU,
    MENUITEM_FREETUTORS,
    MENUITEM_FACTORYIVS,
    MENUITEM_KEEPSTREAK,
    MENUITEM_GUIDE,
    MENUITEM_CANCEL,
    MENUITEM_COUNT,
};

enum
{
    WIN_HEADER,
    WIN_OPTIONS,
    WIN_GUIDE, // v1.41: the Frontier Start overview, over the bottom of the list
};

#define YPOS_TEXTSPEED    (MENUITEM_TEXTSPEED * 16)
#define YPOS_BATTLESCENE  (MENUITEM_BATTLESCENE * 16)
#define YPOS_BATTLESTYLE  (MENUITEM_BATTLESTYLE * 16)
#define YPOS_SOUND        (MENUITEM_SOUND * 16)
#define YPOS_BUTTONMODE   (MENUITEM_BUTTONMODE * 16)
#define YPOS_FRAMETYPE    (MENUITEM_FRAMETYPE * 16)
#define YPOS_DMGPREVIEW   (MENUITEM_DMGPREVIEW * 16)
#define YPOS_INFOSTART    (MENUITEM_INFOSTART * 16)

// v1.37: more options than fit, so the list scrolls 7 rows at a time
#define VISIBLE_ITEMS     7
#define LIST_TOP_Y        40
#define SCROLL_ARROW_TAG  0x1100

// The frame tiles, moved past the taller options window
#define FRAME_TILES       0x350
#define GUIDE_TILES       0x360
#define GUIDE_TOP         15 // tile row; the panel covers rows 15-18 of the list

static void Task_OptionMenuFadeIn(u8 taskId);
static void Task_OptionMenuProcessInput(u8 taskId);
static void Task_OptionMenuSave(u8 taskId);
static void Task_OptionMenuFadeOut(u8 taskId);
static void HighlightOptionMenuItem(u8 selection);
static u8 TextSpeed_ProcessInput(u8 selection);
static void TextSpeed_DrawChoices(u8 selection);
static u8 BattleScene_ProcessInput(u8 selection);
static void BattleScene_DrawChoices(u8 selection);
static u8 BattleStyle_ProcessInput(u8 selection);
static void BattleStyle_DrawChoices(u8 selection);
static u8 Sound_ProcessInput(u8 selection);
static void Sound_DrawChoices(u8 selection);
static u8 FrameType_ProcessInput(u8 selection);
static void FrameType_DrawChoices(u8 selection);
static u8 ButtonMode_ProcessInput(u8 selection);
static void ButtonMode_DrawChoices(u8 selection);
static u8 OnOff_ProcessInput(u8 selection);
static void DamagePreview_DrawChoices(u8 selection);
static void InfoStart_DrawChoices(u8 selection);
static void FlagOption_DrawChoices(u8 i, u8 selection);
static void UpdateScroll(u8 selection);
static void DrawOptionMenuChoice(const u8 *text, u8 x, u8 y, u8 style);
static void Task_OptionMenuGuide(u8 taskId);
static void ShowGuide(void);
static void HideGuide(void);
static void DrawHeaderText(void);
static void DrawOptionMenuTexts(void);
static void DrawBgWindowFrames(void);

EWRAM_DATA static bool8 sArrowPressed = FALSE;
EWRAM_DATA static u16 sScrollTop = 0;

static const u8 sText_DamagePreview[] = _("DMG PREVIEW");
static const u8 sText_InfoStart[] = _("INFO START");
static const u8 sText_InfoStartFoe[] = _("{COLOR GREEN}{SHADOW LIGHT_GREEN}FOE");
static const u8 sText_InfoStartYours[] = _("{COLOR GREEN}{SHADOW LIGHT_GREEN}YOURS");

// v1.39: options for Frontier Start's changes to the original game. Each is an
// event flag that is clear for Frontier Start's behavior, so saves from before
// v1.39 (and new games) keep it.
static const u8 sText_InfoScreen[] = _("L+R INFO");
static const u8 sText_EditMenu[] = _("EDIT MENU");
static const u8 sText_FreeTutors[] = _("FREE TUTORS");
static const u8 sText_FactoryIVs[] = _("FACTORY IVS");
static const u8 sText_KeepStreak[] = _("KEEP STREAK");
static const u8 sText_Fixed[] = _("{COLOR GREEN}{SHADOW LIGHT_GREEN}FIXED");
static const u8 sText_Vanilla[] = _("{COLOR GREEN}{SHADOW LIGHT_GREEN}VANILLA");
static const u8 sText_Guide[] = _("GUIDE");
static const u8 sText_GuideRead[] = _("{A_BUTTON} READ");

// v1.41: the overview of Frontier Start, shown before the fighting-style menu
// on a new game (data/scripts/ArchetypeSelect.inc) and from OPTION > GUIDE.
const u8 gText_FrontierGuide[] = _(
    "Welcome to POKéMON EMERALD\n"
    "BATTLE FRONTIER!\p"
    "Every facility is open from the\n"
    "start. Pick a team and go!\p"
    "Your PC boxes hold ready-made\n"
    "teams at Lv50 and Lv100, the\l"
    "legendaries, and Battle Tower\l"
    "sample teams.\p"
    "In battle, press L+R to see stats,\n"
    "IVs, EVs and damage for both sides.\p"
    "When you pick a move, the foe's\n"
    "HP bar shows the damage it can do.\p"
    "Choose EDIT in the party menu or\n"
    "the PC to change any POKéMON.\p"
    "The move tutors' house has free\n"
    "tutors, a move relearner and a\l"
    "move deleter.\p"
    "You can switch these features on\n"
    "and off in OPTION.\p"
    "Press R + START on the map for\n"
    "the debug menu.\p"
    "To read this again, choose GUIDE\n"
    "in OPTION.");

struct FlagOption
{
    u8 menuItem;
    u16 flag;
    const u8 *left;
    const u8 *right;
    bool8 setIsLeft; // the flag set means the left choice
};

static const struct FlagOption sFlagOptions[] =
{
    {MENUITEM_INFOSCREEN, FLAG_OPT_INFO_SCREEN_OFF,    gText_BattleSceneOn, gText_BattleSceneOff, FALSE},
    {MENUITEM_EDITMENU,   FLAG_OPT_EDIT_OFF,           gText_BattleSceneOn, gText_BattleSceneOff, FALSE},
    {MENUITEM_FREETUTORS, FLAG_OPT_TUTOR_COSTS_BP,     gText_BattleSceneOn, gText_BattleSceneOff, FALSE},
    {MENUITEM_FACTORYIVS, FLAG_OPT_FACTORY_IV_VANILLA, sText_Fixed,         sText_Vanilla,        FALSE},
    {MENUITEM_KEEPSTREAK, FLAG_OPT_KEEP_STREAK,        gText_BattleSceneOn, gText_BattleSceneOff, TRUE},
};

static const u16 sOptionMenuText_Pal[] = INCGFX_U16("graphics/interface/option_menu_text.pal", ".gbapal");
// note: this is only used in the Japanese release
static const u8 sEqualSignGfx[] = INCGFX_U8("graphics/interface/option_menu_equals_sign.png", ".4bpp");

static const u8 *const sOptionMenuItemsNames[MENUITEM_COUNT] =
{
    [MENUITEM_TEXTSPEED]   = gText_TextSpeed,
    [MENUITEM_BATTLESCENE] = gText_BattleScene,
    [MENUITEM_BATTLESTYLE] = gText_BattleStyle,
    [MENUITEM_SOUND]       = gText_Sound,
    [MENUITEM_BUTTONMODE]  = gText_ButtonMode,
    [MENUITEM_FRAMETYPE]   = gText_Frame,
    [MENUITEM_INFOSCREEN]  = sText_InfoScreen,
    [MENUITEM_INFOSTART]   = sText_InfoStart,
    [MENUITEM_DMGPREVIEW]  = sText_DamagePreview,
    [MENUITEM_EDITMENU]    = sText_EditMenu,
    [MENUITEM_FREETUTORS]  = sText_FreeTutors,
    [MENUITEM_FACTORYIVS]  = sText_FactoryIVs,
    [MENUITEM_KEEPSTREAK]  = sText_KeepStreak,
    [MENUITEM_GUIDE]       = sText_Guide,
    [MENUITEM_CANCEL]      = gText_OptionMenuCancel,
};

static const struct WindowTemplate sOptionMenuWinTemplates[] =
{
    [WIN_HEADER] = {
        .bg = 1,
        .tilemapLeft = 2,
        .tilemapTop = 1,
        .width = 26,
        .height = 2,
        .paletteNum = 1,
        .baseBlock = 2
    },
    [WIN_OPTIONS] = {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 0, // v1.39: the list is taller than the screen; BG0 is scrolled to place it
        .width = 26,
        .height = MENUITEM_COUNT * 2,
        .paletteNum = 1,
        .baseBlock = 0x36
    },
    [WIN_GUIDE] = {
        .bg = 1, // above the list (BG0), and not scrolled
        .tilemapLeft = 2,
        .tilemapTop = GUIDE_TOP,
        .width = 26,
        .height = 4,
        .paletteNum = 1,
        .baseBlock = GUIDE_TILES
    },
    DUMMY_WIN_TEMPLATE
};

static const struct BgTemplate sOptionMenuBgTemplates[] =
{
    {
        .bg = 1,
        .charBaseIndex = 1,
        .mapBaseIndex = 30,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 0,
        .baseTile = 0
    },
    {
        .bg = 0,
        .charBaseIndex = 1,
        .mapBaseIndex = 31,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 1,
        .baseTile = 0
    }
};

static const u16 sOptionMenuBg_Pal[] = {RGB(17, 18, 31)};

static void MainCB2(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    UpdatePaletteFade();
}

static void VBlankCB(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

void CB2_InitOptionMenu(void)
{
    switch (gMain.state)
    {
    default:
    case 0:
        SetVBlankCallback(NULL);
        gMain.state++;
        break;
    case 1:
        DmaClearLarge16(3, (void *)(VRAM), VRAM_SIZE, 0x1000);
        DmaClear32(3, OAM, OAM_SIZE);
        DmaClear16(3, PLTT, PLTT_SIZE);
        SetGpuReg(REG_OFFSET_DISPCNT, 0);
        ResetBgsAndClearDma3BusyFlags(0);
        InitBgsFromTemplates(0, sOptionMenuBgTemplates, ARRAY_COUNT(sOptionMenuBgTemplates));
        ChangeBgX(0, 0, BG_COORD_SET);
        ChangeBgY(0, 0, BG_COORD_SET);
        ChangeBgX(1, 0, BG_COORD_SET);
        ChangeBgY(1, 0, BG_COORD_SET);
        ChangeBgX(2, 0, BG_COORD_SET);
        ChangeBgY(2, 0, BG_COORD_SET);
        ChangeBgX(3, 0, BG_COORD_SET);
        ChangeBgY(3, 0, BG_COORD_SET);
        InitWindows(sOptionMenuWinTemplates);
        DeactivateAllTextPrinters();
        SetGpuReg(REG_OFFSET_WIN0H, 0);
        SetGpuReg(REG_OFFSET_WIN0V, 0);
        // WIN1 clips the scrolling list to the inside of its frame
        SetGpuReg(REG_OFFSET_WIN1H, WIN_RANGE(16, DISPLAY_WIDTH - 16));
        SetGpuReg(REG_OFFSET_WIN1V, WIN_RANGE(LIST_TOP_Y, LIST_TOP_Y + VISIBLE_ITEMS * 16));
        SetGpuReg(REG_OFFSET_WININ, WININ_WIN0_BG0 | WININ_WIN1_BG0 | WININ_WIN1_BG1 | WININ_WIN1_OBJ | WININ_WIN1_CLR);
        SetGpuReg(REG_OFFSET_WINOUT, WINOUT_WIN01_BG1 | WINOUT_WIN01_OBJ | WINOUT_WIN01_CLR);
        SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_TGT1_BG0 | BLDCNT_EFFECT_DARKEN);
        SetGpuReg(REG_OFFSET_BLDALPHA, 0);
        SetGpuReg(REG_OFFSET_BLDY, 4);
        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_WIN0_ON | DISPCNT_WIN1_ON | DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP);
        ShowBg(0);
        ShowBg(1);
        gMain.state++;
        break;
    case 2:
        ResetPaletteFade();
        ScanlineEffect_Stop();
        ResetTasks();
        ResetSpriteData();
        gMain.state++;
        break;
    case 3:
        LoadBgTiles(1, GetWindowFrameTilesPal(gSaveBlock2Ptr->optionsWindowFrameType)->tiles, 0x120, FRAME_TILES);
        gMain.state++;
        break;
    case 4:
        LoadPalette(sOptionMenuBg_Pal, BG_PLTT_ID(0), sizeof(sOptionMenuBg_Pal));
        LoadPalette(GetWindowFrameTilesPal(gSaveBlock2Ptr->optionsWindowFrameType)->pal, BG_PLTT_ID(7), PLTT_SIZE_4BPP);
        gMain.state++;
        break;
    case 5:
        LoadPalette(sOptionMenuText_Pal, BG_PLTT_ID(1), sizeof(sOptionMenuText_Pal));
        gMain.state++;
        break;
    case 6:
        PutWindowTilemap(WIN_HEADER);
        DrawHeaderText();
        gMain.state++;
        break;
    case 7:
        gMain.state++;
        break;
    case 8:
        PutWindowTilemap(WIN_OPTIONS);
        DrawOptionMenuTexts();
        gMain.state++;
    case 9:
        DrawBgWindowFrames();
        gMain.state++;
        break;
    case 10:
    {
        u8 taskId = CreateTask(Task_OptionMenuFadeIn, 0);

        gTasks[taskId].tMenuSelection = 0;
        gTasks[taskId].tTextSpeed = gSaveBlock2Ptr->optionsTextSpeed;
        gTasks[taskId].tBattleSceneOff = gSaveBlock2Ptr->optionsBattleSceneOff;
        gTasks[taskId].tBattleStyle = gSaveBlock2Ptr->optionsBattleStyle;
        gTasks[taskId].tSound = gSaveBlock2Ptr->optionsSound;
        gTasks[taskId].tButtonMode = gSaveBlock2Ptr->optionsButtonMode;
        gTasks[taskId].tWindowFrameType = gSaveBlock2Ptr->optionsWindowFrameType;
        gTasks[taskId].tDamagePreviewOff = gSaveBlock2Ptr->optionsDamagePreviewOff;
        gTasks[taskId].tInfoStartYours = gSaveBlock2Ptr->optionsInfoStartYours;

        TextSpeed_DrawChoices(gTasks[taskId].tTextSpeed);
        BattleScene_DrawChoices(gTasks[taskId].tBattleSceneOff);
        BattleStyle_DrawChoices(gTasks[taskId].tBattleStyle);
        Sound_DrawChoices(gTasks[taskId].tSound);
        ButtonMode_DrawChoices(gTasks[taskId].tButtonMode);
        FrameType_DrawChoices(gTasks[taskId].tWindowFrameType);
        DamagePreview_DrawChoices(gTasks[taskId].tDamagePreviewOff);
        InfoStart_DrawChoices(gTasks[taskId].tInfoStartYours);
        {
            u8 i;

            for (i = 0; i < ARRAY_COUNT(sFlagOptions); i++)
            {
                gTasks[taskId].tFlagOptions(i) = (FlagGet(sFlagOptions[i].flag) == sFlagOptions[i].setIsLeft) ? 0 : 1;
                FlagOption_DrawChoices(i, gTasks[taskId].tFlagOptions(i));
            }
        }
        DrawOptionMenuChoice(sText_GuideRead, GetStringRightAlignXOffset(FONT_NORMAL, sText_GuideRead, 198), MENUITEM_GUIDE * 16, 0);
        sScrollTop = 0;
        ChangeBgY(0, 0, BG_COORD_SET);
        gTasks[taskId].tArrowTaskId = AddScrollIndicatorArrowPairParameterized(SCROLL_ARROW_UP, DISPLAY_WIDTH / 2, LIST_TOP_Y - 6,
                                                                              LIST_TOP_Y + VISIBLE_ITEMS * 16 + 5, MENUITEM_COUNT - VISIBLE_ITEMS,
                                                                              SCROLL_ARROW_TAG, SCROLL_ARROW_TAG, &sScrollTop);
        HighlightOptionMenuItem(gTasks[taskId].tMenuSelection);

        CopyWindowToVram(WIN_OPTIONS, COPYWIN_FULL);
        gMain.state++;
        break;
    }
    case 11:
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        SetVBlankCallback(VBlankCB);
        SetMainCallback2(MainCB2);
        return;
    }
}

static void Task_OptionMenuFadeIn(u8 taskId)
{
    if (!gPaletteFade.active)
        gTasks[taskId].func = Task_OptionMenuProcessInput;
}

static void Task_OptionMenuProcessInput(u8 taskId)
{
    if (JOY_NEW(A_BUTTON))
    {
        if (gTasks[taskId].tMenuSelection == MENUITEM_CANCEL)
            gTasks[taskId].func = Task_OptionMenuSave;
        else if (gTasks[taskId].tMenuSelection == MENUITEM_GUIDE)
        {
            PlaySE(SE_SELECT);
            ShowGuide();
            gTasks[taskId].func = Task_OptionMenuGuide;
        }
    }
    else if (JOY_NEW(B_BUTTON))
    {
        gTasks[taskId].func = Task_OptionMenuSave;
    }
    else if (JOY_NEW(DPAD_UP))
    {
        if (gTasks[taskId].tMenuSelection > 0)
            gTasks[taskId].tMenuSelection--;
        else
            gTasks[taskId].tMenuSelection = MENUITEM_CANCEL;
        HighlightOptionMenuItem(gTasks[taskId].tMenuSelection);
    }
    else if (JOY_NEW(DPAD_DOWN))
    {
        if (gTasks[taskId].tMenuSelection < MENUITEM_CANCEL)
            gTasks[taskId].tMenuSelection++;
        else
            gTasks[taskId].tMenuSelection = 0;
        HighlightOptionMenuItem(gTasks[taskId].tMenuSelection);
    }
    else
    {
        u8 previousOption;

        switch (gTasks[taskId].tMenuSelection)
        {
        case MENUITEM_TEXTSPEED:
            previousOption = gTasks[taskId].tTextSpeed;
            gTasks[taskId].tTextSpeed = TextSpeed_ProcessInput(gTasks[taskId].tTextSpeed);

            if (previousOption != gTasks[taskId].tTextSpeed)
                TextSpeed_DrawChoices(gTasks[taskId].tTextSpeed);
            break;
        case MENUITEM_BATTLESCENE:
            previousOption = gTasks[taskId].tBattleSceneOff;
            gTasks[taskId].tBattleSceneOff = BattleScene_ProcessInput(gTasks[taskId].tBattleSceneOff);

            if (previousOption != gTasks[taskId].tBattleSceneOff)
                BattleScene_DrawChoices(gTasks[taskId].tBattleSceneOff);
            break;
        case MENUITEM_BATTLESTYLE:
            previousOption = gTasks[taskId].tBattleStyle;
            gTasks[taskId].tBattleStyle = BattleStyle_ProcessInput(gTasks[taskId].tBattleStyle);

            if (previousOption != gTasks[taskId].tBattleStyle)
                BattleStyle_DrawChoices(gTasks[taskId].tBattleStyle);
            break;
        case MENUITEM_SOUND:
            previousOption = gTasks[taskId].tSound;
            gTasks[taskId].tSound = Sound_ProcessInput(gTasks[taskId].tSound);

            if (previousOption != gTasks[taskId].tSound)
                Sound_DrawChoices(gTasks[taskId].tSound);
            break;
        case MENUITEM_BUTTONMODE:
            previousOption = gTasks[taskId].tButtonMode;
            gTasks[taskId].tButtonMode = ButtonMode_ProcessInput(gTasks[taskId].tButtonMode);

            if (previousOption != gTasks[taskId].tButtonMode)
                ButtonMode_DrawChoices(gTasks[taskId].tButtonMode);
            break;
        case MENUITEM_DMGPREVIEW:
            previousOption = gTasks[taskId].tDamagePreviewOff;
            gTasks[taskId].tDamagePreviewOff = OnOff_ProcessInput(gTasks[taskId].tDamagePreviewOff);

            if (previousOption != gTasks[taskId].tDamagePreviewOff)
                DamagePreview_DrawChoices(gTasks[taskId].tDamagePreviewOff);
            break;
        case MENUITEM_INFOSTART:
            previousOption = gTasks[taskId].tInfoStartYours;
            gTasks[taskId].tInfoStartYours = OnOff_ProcessInput(gTasks[taskId].tInfoStartYours);

            if (previousOption != gTasks[taskId].tInfoStartYours)
                InfoStart_DrawChoices(gTasks[taskId].tInfoStartYours);
            break;
        case MENUITEM_INFOSCREEN:
        case MENUITEM_EDITMENU:
        case MENUITEM_FREETUTORS:
        case MENUITEM_FACTORYIVS:
        case MENUITEM_KEEPSTREAK:
        {
            u8 i;

            for (i = 0; i < ARRAY_COUNT(sFlagOptions); i++)
            {
                if (sFlagOptions[i].menuItem == gTasks[taskId].tMenuSelection)
                {
                    previousOption = gTasks[taskId].tFlagOptions(i);
                    gTasks[taskId].tFlagOptions(i) = OnOff_ProcessInput(previousOption);
                    if (previousOption != gTasks[taskId].tFlagOptions(i))
                        FlagOption_DrawChoices(i, gTasks[taskId].tFlagOptions(i));
                }
            }
            break;
        }
        case MENUITEM_FRAMETYPE:
            previousOption = gTasks[taskId].tWindowFrameType;
            gTasks[taskId].tWindowFrameType = FrameType_ProcessInput(gTasks[taskId].tWindowFrameType);

            if (previousOption != gTasks[taskId].tWindowFrameType)
                FrameType_DrawChoices(gTasks[taskId].tWindowFrameType);
            break;
        default:
            return;
        }

        if (sArrowPressed)
        {
            sArrowPressed = FALSE;
            CopyWindowToVram(WIN_OPTIONS, COPYWIN_GFX);
        }
    }
}

static void Task_OptionMenuSave(u8 taskId)
{
    gSaveBlock2Ptr->optionsTextSpeed = gTasks[taskId].tTextSpeed;
    gSaveBlock2Ptr->optionsBattleSceneOff = gTasks[taskId].tBattleSceneOff;
    gSaveBlock2Ptr->optionsBattleStyle = gTasks[taskId].tBattleStyle;
    gSaveBlock2Ptr->optionsSound = gTasks[taskId].tSound;
    gSaveBlock2Ptr->optionsButtonMode = gTasks[taskId].tButtonMode;
    gSaveBlock2Ptr->optionsWindowFrameType = gTasks[taskId].tWindowFrameType;
    gSaveBlock2Ptr->optionsDamagePreviewOff = gTasks[taskId].tDamagePreviewOff;
    gSaveBlock2Ptr->optionsInfoStartYours = gTasks[taskId].tInfoStartYours;
    RemoveScrollIndicatorArrowPair(gTasks[taskId].tArrowTaskId);
    {
        u8 i;

        for (i = 0; i < ARRAY_COUNT(sFlagOptions); i++)
        {
            // left (0) is the flag set exactly when setIsLeft
            if ((gTasks[taskId].tFlagOptions(i) == 0) == sFlagOptions[i].setIsLeft)
                FlagSet(sFlagOptions[i].flag);
            else
                FlagClear(sFlagOptions[i].flag);
        }
    }

    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
    gTasks[taskId].func = Task_OptionMenuFadeOut;
}

static void Task_OptionMenuFadeOut(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        DestroyTask(taskId);
        FreeAllWindowBuffers();
        SetMainCallback2(gMain.savedCallback);
    }
}

static void UpdateScroll(u8 selection)
{
    if (selection < sScrollTop)
        sScrollTop = selection;
    else if (selection >= sScrollTop + VISIBLE_ITEMS)
        sScrollTop = selection - (VISIBLE_ITEMS - 1);
    // The window starts at the top of BG0's tilemap; scroll it down to the list area
    ChangeBgY(0, ((sScrollTop * 16 - LIST_TOP_Y) & 0xFF) << 8, BG_COORD_SET);
}

static void HighlightOptionMenuItem(u8 index)
{
    u8 y;

    UpdateScroll(index);
    y = (index - sScrollTop) * 16 + LIST_TOP_Y;
    SetGpuReg(REG_OFFSET_WIN0H, WIN_RANGE(16, DISPLAY_WIDTH - 16));
    SetGpuReg(REG_OFFSET_WIN0V, WIN_RANGE(y, y + 16));
}

static void DrawOptionMenuChoice(const u8 *text, u8 x, u8 y, u8 style)
{
    u8 dst[16];
    u16 i;

    for (i = 0; *text != EOS && i < ARRAY_COUNT(dst) - 1; i++)
        dst[i] = *(text++);

    if (style != 0)
    {
        dst[2] = TEXT_COLOR_RED;
        dst[5] = TEXT_COLOR_LIGHT_RED;
    }

    dst[i] = EOS;
    AddTextPrinterParameterized(WIN_OPTIONS, FONT_NORMAL, dst, x, y + 1, TEXT_SKIP_DRAW, NULL);
}

static u8 TextSpeed_ProcessInput(u8 selection)
{
    if (JOY_NEW(DPAD_RIGHT))
    {
        if (selection <= 1)
            selection++;
        else
            selection = 0;

        sArrowPressed = TRUE;
    }
    if (JOY_NEW(DPAD_LEFT))
    {
        if (selection != 0)
            selection--;
        else
            selection = 2;

        sArrowPressed = TRUE;
    }
    return selection;
}

static void TextSpeed_DrawChoices(u8 selection)
{
    u8 styles[3];
    s32 widthSlow, widthMid, widthFast, xMid;

    styles[0] = 0;
    styles[1] = 0;
    styles[2] = 0;
    styles[selection] = 1;

    DrawOptionMenuChoice(gText_TextSpeedSlow, 104, YPOS_TEXTSPEED, styles[0]);

    widthSlow = GetStringWidth(FONT_NORMAL, gText_TextSpeedSlow, 0);
    widthMid = GetStringWidth(FONT_NORMAL, gText_TextSpeedMid, 0);
    widthFast = GetStringWidth(FONT_NORMAL, gText_TextSpeedFast, 0);

    widthMid -= 94;
    xMid = (widthSlow - widthMid - widthFast) / 2 + 104;
    DrawOptionMenuChoice(gText_TextSpeedMid, xMid, YPOS_TEXTSPEED, styles[1]);

    DrawOptionMenuChoice(gText_TextSpeedFast, GetStringRightAlignXOffset(FONT_NORMAL, gText_TextSpeedFast, 198), YPOS_TEXTSPEED, styles[2]);
}

static u8 BattleScene_ProcessInput(u8 selection)
{
    if (JOY_NEW(DPAD_LEFT | DPAD_RIGHT))
    {
        selection ^= 1;
        sArrowPressed = TRUE;
    }

    return selection;
}

static void BattleScene_DrawChoices(u8 selection)
{
    u8 styles[2];

    styles[0] = 0;
    styles[1] = 0;
    styles[selection] = 1;

    DrawOptionMenuChoice(gText_BattleSceneOn, 104, YPOS_BATTLESCENE, styles[0]);
    DrawOptionMenuChoice(gText_BattleSceneOff, GetStringRightAlignXOffset(FONT_NORMAL, gText_BattleSceneOff, 198), YPOS_BATTLESCENE, styles[1]);
}

static u8 BattleStyle_ProcessInput(u8 selection)
{
    if (JOY_NEW(DPAD_LEFT | DPAD_RIGHT))
    {
        selection ^= 1;
        sArrowPressed = TRUE;
    }

    return selection;
}

static void BattleStyle_DrawChoices(u8 selection)
{
    u8 styles[2];

    styles[0] = 0;
    styles[1] = 0;
    styles[selection] = 1;

    DrawOptionMenuChoice(gText_BattleStyleShift, 104, YPOS_BATTLESTYLE, styles[0]);
    DrawOptionMenuChoice(gText_BattleStyleSet, GetStringRightAlignXOffset(FONT_NORMAL, gText_BattleStyleSet, 198), YPOS_BATTLESTYLE, styles[1]);
}

static u8 Sound_ProcessInput(u8 selection)
{
    if (JOY_NEW(DPAD_LEFT | DPAD_RIGHT))
    {
        selection ^= 1;
        SetPokemonCryStereo(selection);
        sArrowPressed = TRUE;
    }

    return selection;
}

static void Sound_DrawChoices(u8 selection)
{
    u8 styles[2];

    styles[0] = 0;
    styles[1] = 0;
    styles[selection] = 1;

    DrawOptionMenuChoice(gText_SoundMono, 104, YPOS_SOUND, styles[0]);
    DrawOptionMenuChoice(gText_SoundStereo, GetStringRightAlignXOffset(FONT_NORMAL, gText_SoundStereo, 198), YPOS_SOUND, styles[1]);
}

static u8 FrameType_ProcessInput(u8 selection)
{
    if (JOY_NEW(DPAD_RIGHT))
    {
        if (selection < WINDOW_FRAMES_COUNT - 1)
            selection++;
        else
            selection = 0;

        LoadBgTiles(1, GetWindowFrameTilesPal(selection)->tiles, 0x120, 0x1A2);
        LoadPalette(GetWindowFrameTilesPal(selection)->pal, BG_PLTT_ID(7), PLTT_SIZE_4BPP);
        sArrowPressed = TRUE;
    }
    if (JOY_NEW(DPAD_LEFT))
    {
        if (selection != 0)
            selection--;
        else
            selection = WINDOW_FRAMES_COUNT - 1;

        LoadBgTiles(1, GetWindowFrameTilesPal(selection)->tiles, 0x120, 0x1A2);
        LoadPalette(GetWindowFrameTilesPal(selection)->pal, BG_PLTT_ID(7), PLTT_SIZE_4BPP);
        sArrowPressed = TRUE;
    }
    return selection;
}

static void FrameType_DrawChoices(u8 selection)
{
    u8 text[16];
    u8 n = selection + 1;
    u16 i;

    for (i = 0; gText_FrameTypeNumber[i] != EOS && i <= 5; i++)
        text[i] = gText_FrameTypeNumber[i];

    // Convert a number to decimal string
    if (n / 10 != 0)
    {
        text[i] = n / 10 + CHAR_0;
        i++;
        text[i] = n % 10 + CHAR_0;
        i++;
    }
    else
    {
        text[i] = n % 10 + CHAR_0;
        i++;
        text[i] = CHAR_SPACER;
        i++;
    }

    text[i] = EOS;

    DrawOptionMenuChoice(gText_FrameType, 104, YPOS_FRAMETYPE, 0);
    DrawOptionMenuChoice(text, 128, YPOS_FRAMETYPE, 1);
}

static u8 ButtonMode_ProcessInput(u8 selection)
{
    if (JOY_NEW(DPAD_RIGHT))
    {
        if (selection <= 1)
            selection++;
        else
            selection = 0;

        sArrowPressed = TRUE;
    }
    if (JOY_NEW(DPAD_LEFT))
    {
        if (selection != 0)
            selection--;
        else
            selection = 2;

        sArrowPressed = TRUE;
    }
    return selection;
}

static void ButtonMode_DrawChoices(u8 selection)
{
    s32 widthNormal, widthLR, widthLA, xLR;
    u8 styles[3];

    styles[0] = 0;
    styles[1] = 0;
    styles[2] = 0;
    styles[selection] = 1;

    DrawOptionMenuChoice(gText_ButtonTypeNormal, 104, YPOS_BUTTONMODE, styles[0]);

    widthNormal = GetStringWidth(FONT_NORMAL, gText_ButtonTypeNormal, 0);
    widthLR = GetStringWidth(FONT_NORMAL, gText_ButtonTypeLR, 0);
    widthLA = GetStringWidth(FONT_NORMAL, gText_ButtonTypeLEqualsA, 0);

    widthLR -= 94;
    xLR = (widthNormal - widthLR - widthLA) / 2 + 104;
    DrawOptionMenuChoice(gText_ButtonTypeLR, xLR, YPOS_BUTTONMODE, styles[1]);

    DrawOptionMenuChoice(gText_ButtonTypeLEqualsA, GetStringRightAlignXOffset(FONT_NORMAL, gText_ButtonTypeLEqualsA, 198), YPOS_BUTTONMODE, styles[2]);
}

// Two-choice options: left/right flips between 0 and 1
static u8 OnOff_ProcessInput(u8 selection)
{
    if (JOY_NEW(DPAD_LEFT | DPAD_RIGHT))
    {
        selection ^= 1;
        sArrowPressed = TRUE;
    }
    return selection;
}

static void DamagePreview_DrawChoices(u8 selection)
{
    u8 styles[2];

    styles[0] = 0;
    styles[1] = 0;
    styles[selection] = 1;
    DrawOptionMenuChoice(gText_BattleSceneOn, 104, YPOS_DMGPREVIEW, styles[0]);
    DrawOptionMenuChoice(gText_BattleSceneOff, GetStringRightAlignXOffset(FONT_NORMAL, gText_BattleSceneOff, 198), YPOS_DMGPREVIEW, styles[1]);
}

static void InfoStart_DrawChoices(u8 selection)
{
    u8 styles[2];

    styles[0] = 0;
    styles[1] = 0;
    styles[selection] = 1;
    DrawOptionMenuChoice(sText_InfoStartFoe, 104, YPOS_INFOSTART, styles[0]);
    DrawOptionMenuChoice(sText_InfoStartYours, GetStringRightAlignXOffset(FONT_NORMAL, sText_InfoStartYours, 198), YPOS_INFOSTART, styles[1]);
}

static void FlagOption_DrawChoices(u8 i, u8 selection)
{
    u8 styles[2];
    u8 y = sFlagOptions[i].menuItem * 16;

    styles[0] = 0;
    styles[1] = 0;
    styles[selection] = 1;
    DrawOptionMenuChoice(sFlagOptions[i].left, 104, y, styles[0]);
    DrawOptionMenuChoice(sFlagOptions[i].right, GetStringRightAlignXOffset(FONT_NORMAL, sFlagOptions[i].right, 198), y, styles[1]);
}

static void DrawHeaderText(void)
{
    FillWindowPixelBuffer(WIN_HEADER, PIXEL_FILL(1));
    AddTextPrinterParameterized(WIN_HEADER, FONT_NORMAL, gText_Option, 8, 1, TEXT_SKIP_DRAW, NULL);
    CopyWindowToVram(WIN_HEADER, COPYWIN_FULL);
}

static void DrawOptionMenuTexts(void)
{
    u8 i;

    FillWindowPixelBuffer(WIN_OPTIONS, PIXEL_FILL(1));
    for (i = 0; i < MENUITEM_COUNT; i++)
        AddTextPrinterParameterized(WIN_OPTIONS, FONT_NORMAL, sOptionMenuItemsNames[i], 8, (i * 16) + 1, TEXT_SKIP_DRAW, NULL);
    CopyWindowToVram(WIN_OPTIONS, COPYWIN_FULL);
}

#define TILE_TOP_CORNER_L (FRAME_TILES + 0)
#define TILE_TOP_EDGE     (FRAME_TILES + 1)
#define TILE_TOP_CORNER_R (FRAME_TILES + 2)
#define TILE_LEFT_EDGE    (FRAME_TILES + 3)
#define TILE_RIGHT_EDGE   (FRAME_TILES + 5)
#define TILE_BOT_CORNER_L (FRAME_TILES + 6)
#define TILE_BOT_EDGE     (FRAME_TILES + 7)
#define TILE_BOT_CORNER_R (FRAME_TILES + 8)

static void DrawBgWindowFrames(void)
{
    //                     bg, tile,              x, y, width, height, palNum
    // Draw title window frame
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_L,  1,  0,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_EDGE,      2,  0, 27,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_R, 28,  0,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_LEFT_EDGE,     1,  1,  1,  2,  7);
    FillBgTilemapBufferRect(1, TILE_RIGHT_EDGE,   28,  1,  1,  2,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_L,  1,  3,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_EDGE,      2,  3, 27,  1,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_R, 28,  3,  1,  1,  7);

    // Draw options list window frame
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_L,  1,  4,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_EDGE,      2,  4, 26,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_R, 28,  4,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_LEFT_EDGE,     1,  5,  1, 18,  7);
    FillBgTilemapBufferRect(1, TILE_RIGHT_EDGE,   28,  5,  1, 18,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_L,  1, 19,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_EDGE,      2, 19, 26,  1,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_R, 28, 19,  1,  1,  7);

    CopyBgTilemapBufferToVram(1);
}

// v1.41: OPTION > GUIDE prints the overview in a panel over the bottom of
// the list, page by page (A or B for the next page, as in a message box).
static void ShowGuide(void)
{
    // Gray text, like the unselected choices (colors 6 and 7 of this screen's text palette)
    static const u8 colors[3] = {TEXT_COLOR_WHITE, 6, 7};

    // The panel's top border, then the panel; WIN0 (the highlighted row) shows
    // only BG0, so turn it off while the panel is up
    FillBgTilemapBufferRect(1, TILE_TOP_EDGE, 2, GUIDE_TOP - 1, 26, 1, 7);
    SetGpuReg(REG_OFFSET_WIN0H, 0);
    SetGpuReg(REG_OFFSET_WIN0V, 0);
    FillWindowPixelBuffer(WIN_GUIDE, PIXEL_FILL(1));
    PutWindowTilemap(WIN_GUIDE);
    CopyWindowToVram(WIN_GUIDE, COPYWIN_FULL);
    CopyBgTilemapBufferToVram(1);
    AddTextPrinterParameterized4(WIN_GUIDE, FONT_NORMAL, 0, 1, 0, 0, colors, GetPlayerTextSpeedDelay(), gText_FrontierGuide);
}

static void HideGuide(void)
{
    ClearWindowTilemap(WIN_GUIDE);
    FillBgTilemapBufferRect(1, 0, 2, GUIDE_TOP - 1, 26, 1, 7);
    CopyBgTilemapBufferToVram(1);
}

static void Task_OptionMenuGuide(u8 taskId)
{
    RunTextPrinters();
    if (IsTextPrinterActive(WIN_GUIDE))
        return;
    // The last page stays up until A or B
    if (JOY_NEW(A_BUTTON | B_BUTTON))
    {
        PlaySE(SE_SELECT);
        HideGuide();
        HighlightOptionMenuItem(gTasks[taskId].tMenuSelection);
        gTasks[taskId].func = Task_OptionMenuProcessInput;
    }
}
