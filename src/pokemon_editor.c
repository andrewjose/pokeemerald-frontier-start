// Pokemon Editor (Frontier Start v1.33)
//
// A full-screen editor laid out like the L+R info screen: the same header,
// party icon strip and two panels, but every value on the page can be
// selected with the D-pad and changed with A.
//
//   Nickname (header)            A: naming screen
//   Species / Nature / Item /    A: pick from a list (L/R jump by letter)
//   Ball / Hidden Power / Moves
//   Level, IVs, EVs, Friendship  A: edit the number (D-pad, L/R = min/max)
//   Gender, Ability, Shiny, PP   A: change
//   Max IVs                      A: set all IVs to 31
//
// START saves and leaves, B leaves (asking first if anything changed), and in
// the party or a box L/R moves to the previous/next Pokemon.
//
// Nature, gender and shininess all come from the personality value, which
// also keys the encryption of the rest of the Pokemon's data. Changing any of
// them rebuilds the Pokemon with CreateMon around a new personality and then
// copies every other field back, so nothing else is lost.

#include "global.h"
#include "event_data.h"
#include "battle_main.h"
#include "battle_pike.h"
#include "battle_pyramid.h"
#include "bg.h"
#include "data.h"
#include "gpu_regs.h"
#include "item.h"
#include "mail.h"
#include "main.h"
#include "malloc.h"
#include "menu.h"
#include "menu_helpers.h"
#include "naming_screen.h"
#include "palette.h"
#include "party_menu.h"
#include "pokemon.h"
#include "pokemon_editor.h"
#include "pokemon_icon.h"
#include "pokemon_storage_system.h"
#include "pokemon_summary_screen.h"
#include "random.h"
#include "scanline_effect.h"
#include "sound.h"
#include "sprite.h"
#include "string_util.h"
#include "task.h"
#include "text.h"
#include "window.h"
#include "constants/abilities.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/party_menu.h"
#include "constants/rgb.h"
#include "constants/songs.h"
#include "constants/species.h"

extern const struct Evolution gEvolutionTable[][EVOS_PER_MON];
extern const u16 gEggMoves[];
#define EGG_MOVES_SPECIES_OFFSET 20000
#define EGG_MOVES_TERMINATOR     0xFFFF

enum {
    WIN_HEADER,
    WIN_LEFT,
    WIN_RIGHT,
    WIN_PICKER, // lists and the save prompt, shown over WIN_RIGHT
};

enum {
    EDMODE_PARTY,
    EDMODE_BOX,
    EDMODE_NEW,
};

enum {
    EDSTATE_NAV,
    EDSTATE_NUMBER,
    EDSTATE_PICKER,
    EDSTATE_PROMPT,
};

enum {
    PROMPT_EXIT,
    PROMPT_PREV,
    PROMPT_NEXT,
};

enum {
    PICK_SPECIES,
    PICK_NATURE,
    PICK_ITEM,
    PICK_BALL,
    PICK_HPTYPE,
    PICK_MOVE,
};

enum {
    CELL_NICK,
    CELL_SPECIES,
    CELL_LEVEL,
    CELL_GENDER,
    CELL_NATURE,
    CELL_ABILITY,
    CELL_ITEM,
    CELL_BALL,
    CELL_HPTYPE,
    CELL_MOVE1,
    CELL_MOVE2,
    CELL_MOVE3,
    CELL_MOVE4,
    CELL_PP1,
    CELL_PP2,
    CELL_PP3,
    CELL_PP4,
    CELL_IV1, // rows in display order: HP, ATK, DEF, SPA, SPD, SPE
    CELL_IV2,
    CELL_IV3,
    CELL_IV4,
    CELL_IV5,
    CELL_IV6,
    CELL_EV1,
    CELL_EV2,
    CELL_EV3,
    CELL_EV4,
    CELL_EV5,
    CELL_EV6,
    CELL_MAXIV,
    CELL_SHINY,
    CELL_FRIEND,
    CELL_COUNT
};

#define NO_CELL 0xFF

#define PANEL_WIDTH       (15 * 8)
#define ICON_X_START      76
#define ICON_X_SPACING    28
#define ICON_Y            17
#define ICON_X_SINGLE     128 // box / new: one icon, clear of the BOX line
#define PICKER_ROWS       8
#define PICKER_ROW_Y(i)   (15 + (i) * 12)

// Palette 15 (standard menu palette plus extras)
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
#define COLOR_CURSOR       14
#define COLOR_EDITING      15

#define BG_COLOR_EDITOR    RGB(10, 6, 17)

struct EditorCell
{
    u8 win;
    u8 x;
    u8 y;
    u8 w;
    u8 up;
    u8 down;
    u8 left;
    u8 right;
};

struct PokemonEditor
{
    MainCallback exitCallback;
    u16 tilemap[BG_SCREEN_SIZE / 2];
    struct Pokemon work;
    struct Pokemon orig;
    u8 mode;
    u8 state;
    u8 cursor;
    u8 slot;            // party slot, or position in the box
    u8 boxId;
    bool8 allBoxes;
    bool8 movesTouched; // NEW: moves follow species/level until edited
    bool8 failed;
    bool8 noRoom; // a new Pokemon couldn't be added; shown in the hint row
    u8 promptAction;
    u16 numberOrig;
    // Picker
    u8 pickKind;
    bool8 pickShowAll;
    u8 pickSlot;        // move slot being picked
    u16 pickCount;
    u16 pickCursor;
    u16 pickTop;
    u16 *pickIds;
    u8 learnable[(MOVES_COUNT + 7) / 8];
    u16 learnableCount;
    u16 learnableSpecies;
    // Name-sorted lists, built the first time they're needed
    u16 *sortedSpecies;
    u16 sortedSpeciesCount;
    u16 *sortedMoves;
    u16 sortedMovesCount;
    u16 *sortedItems;
    u16 sortedItemsCount;
    // Icons
    u8 iconSpriteIds[PARTY_SIZE];
    u8 iconSlots[PARTY_SIZE];
    u8 iconCount;
    u8 nameBuffer[POKEMON_NAME_LENGTH + 10];
};

static EWRAM_DATA struct PokemonEditor *sEd = NULL;

static void CB2_EditorSetup(void);
static void CB2_Editor(void);
static void VBlankCB_Editor(void);
static void Task_EditorInput(u8 taskId);
static void Task_EditorExit(u8 taskId);
static void Task_EditorGoToNaming(u8 taskId);
static void CB2_EditorReturnFromNaming(void);
static void DrawAll(void);
static void CreateIcons(void);
static void DestroyIcons(void);
static void StartExit(u8 taskId);

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------

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
        .width = 30, // full width so the BOX line fits; icons sit on top
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
    [WIN_PICKER] = {
        .bg = 0,
        .tilemapLeft = 15,
        .tilemapTop = 4,
        .width = 15,
        .height = 16,
        .paletteNum = 15,
        .baseBlock = 1 + 30 * 4 + 15 * 16 * 2,
    },
    DUMMY_WIN_TEMPLATE
};

#define ROW_Y(r)  (2 + (r) * 12)
#define MOVE_Y(r) (77 + (r) * 12)

// win, x, y, w, up, down, left, right
static const struct EditorCell sCells[CELL_COUNT] =
{
    [CELL_NICK]    = {WIN_HEADER,  2, 1,  60, NO_CELL,      CELL_SPECIES, NO_CELL,      NO_CELL},
    [CELL_SPECIES] = {WIN_LEFT,    2, ROW_Y(0), 68, CELL_NICK, CELL_NATURE, NO_CELL,     CELL_LEVEL},
    [CELL_LEVEL]   = {WIN_LEFT,   72, ROW_Y(0), 30, CELL_NICK, CELL_NATURE, CELL_SPECIES, CELL_GENDER},
    [CELL_GENDER]  = {WIN_LEFT,  104, ROW_Y(0), 13, CELL_NICK, CELL_NATURE, CELL_LEVEL,   CELL_IV1},
    [CELL_NATURE]  = {WIN_LEFT,   42, ROW_Y(1), 76, CELL_SPECIES, CELL_ABILITY, NO_CELL,  CELL_IV1},
    [CELL_ABILITY] = {WIN_LEFT,   42, ROW_Y(2), 76, CELL_NATURE, CELL_ITEM,    NO_CELL,   CELL_IV2},
    [CELL_ITEM]    = {WIN_LEFT,   42, ROW_Y(3), 76, CELL_ABILITY, CELL_BALL,   NO_CELL,   CELL_IV3},
    [CELL_BALL]    = {WIN_LEFT,   42, ROW_Y(4), 76, CELL_ITEM,   CELL_HPTYPE,  NO_CELL,   CELL_IV4},
    [CELL_HPTYPE]  = {WIN_LEFT,   42, ROW_Y(5), 76, CELL_BALL,   CELL_MOVE1,   NO_CELL,   CELL_IV5},
    [CELL_MOVE1]   = {WIN_LEFT,    2, MOVE_Y(0), 80, CELL_HPTYPE, CELL_MOVE2, NO_CELL,    CELL_PP1},
    [CELL_MOVE2]   = {WIN_LEFT,    2, MOVE_Y(1), 80, CELL_MOVE1, CELL_MOVE3,  NO_CELL,    CELL_PP2},
    [CELL_MOVE3]   = {WIN_LEFT,    2, MOVE_Y(2), 80, CELL_MOVE2, CELL_MOVE4,  NO_CELL,    CELL_PP3},
    [CELL_MOVE4]   = {WIN_LEFT,    2, MOVE_Y(3), 80, CELL_MOVE3, NO_CELL,     NO_CELL,    CELL_PP4},
    [CELL_PP1]     = {WIN_LEFT,   84, MOVE_Y(0), 34, CELL_HPTYPE, CELL_PP2,   CELL_MOVE1, CELL_IV6},
    [CELL_PP2]     = {WIN_LEFT,   84, MOVE_Y(1), 34, CELL_PP1,   CELL_PP3,    CELL_MOVE2, CELL_MAXIV},
    [CELL_PP3]     = {WIN_LEFT,   84, MOVE_Y(2), 34, CELL_PP2,   CELL_PP4,    CELL_MOVE3, CELL_SHINY},
    [CELL_PP4]     = {WIN_LEFT,   84, MOVE_Y(3), 34, CELL_PP3,   NO_CELL,     CELL_MOVE4, CELL_SHINY},
    [CELL_IV1]     = {WIN_RIGHT,  56, ROW_Y(1), 20, NO_CELL,     CELL_IV2,    CELL_NATURE,  CELL_EV1},
    [CELL_IV2]     = {WIN_RIGHT,  56, ROW_Y(2), 20, CELL_IV1,    CELL_IV3,    CELL_ABILITY, CELL_EV2},
    [CELL_IV3]     = {WIN_RIGHT,  56, ROW_Y(3), 20, CELL_IV2,    CELL_IV4,    CELL_ITEM,    CELL_EV3},
    [CELL_IV4]     = {WIN_RIGHT,  56, ROW_Y(4), 20, CELL_IV3,    CELL_IV5,    CELL_BALL,    CELL_EV4},
    [CELL_IV5]     = {WIN_RIGHT,  56, ROW_Y(5), 20, CELL_IV4,    CELL_IV6,    CELL_HPTYPE,  CELL_EV5},
    [CELL_IV6]     = {WIN_RIGHT,  56, ROW_Y(6), 20, CELL_IV5,    CELL_MAXIV,  CELL_PP1,     CELL_EV6},
    [CELL_EV1]     = {WIN_RIGHT,  80, ROW_Y(1), 26, NO_CELL,     CELL_EV2,    CELL_IV1,   NO_CELL},
    [CELL_EV2]     = {WIN_RIGHT,  80, ROW_Y(2), 26, CELL_EV1,    CELL_EV3,    CELL_IV2,   NO_CELL},
    [CELL_EV3]     = {WIN_RIGHT,  80, ROW_Y(3), 26, CELL_EV2,    CELL_EV4,    CELL_IV3,   NO_CELL},
    [CELL_EV4]     = {WIN_RIGHT,  80, ROW_Y(4), 26, CELL_EV3,    CELL_EV5,    CELL_IV4,   NO_CELL},
    [CELL_EV5]     = {WIN_RIGHT,  80, ROW_Y(5), 26, CELL_EV4,    CELL_EV6,    CELL_IV5,   NO_CELL},
    [CELL_EV6]     = {WIN_RIGHT,  80, ROW_Y(6), 26, CELL_EV5,    CELL_FRIEND, CELL_IV6,   NO_CELL},
    [CELL_MAXIV]   = {WIN_RIGHT,   2, 88,       50, CELL_IV6,    CELL_SHINY,  CELL_PP2,   NO_CELL},
    [CELL_SHINY]   = {WIN_RIGHT,   2, 103,      52, CELL_MAXIV,  NO_CELL,     CELL_PP3,   CELL_FRIEND},
    [CELL_FRIEND]  = {WIN_RIGHT,  58, 103,      60, CELL_EV6,    NO_CELL,     CELL_SHINY, NO_CELL},
};

// Stat table rows (party/battle structs use HP, Atk, Def, Spe, SpA, SpD)
static const u8 sStatDisplayOrder[NUM_STATS] = {STAT_HP, STAT_ATK, STAT_DEF, STAT_SPATK, STAT_SPDEF, STAT_SPEED};

static const u8 sText_New[] = _("NEW");
static const u8 sText_Box[] = _("BOX ");
static const u8 sText_Lv[] = _("Lv");
static const u8 sText_Male[] = _("♂");
static const u8 sText_Female[] = _("♀");
static const u8 sText_Genderless[] = _("-");
static const u8 sText_Nature[] = _("Nature");
static const u8 sText_Ability[] = _("Ability");
static const u8 sText_Item[] = _("Item");
static const u8 sText_Ball[] = _("Ball");
static const u8 sText_HiddenPower[] = _("Hid.Pow");
static const u8 sText_None[] = _("-");
static const u8 sText_NoneWord[] = _("NONE");
static const u8 sText_Slash[] = _("/");
static const u8 sText_Space[] = _(" ");
static const u8 sText_Plus[] = _("+");
static const u8 sText_Minus[] = _("-");
static const u8 sText_DexNo[] = _("No.");
static const u8 sText_ColStat[] = _("STAT");
static const u8 sText_ColIV[] = _("IV");
static const u8 sText_ColEV[] = _("EV");
static const u8 sText_MaxIVs[] = _("Max IVs");
static const u8 sText_EVTotal[] = _("EVs ");
static const u8 sText_Shiny[] = _("Shiny ");
static const u8 sText_Yes[] = _("YES");
static const u8 sText_No[] = _("NO");
static const u8 sText_Friend[] = _("Friend ");

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

// Hints
static const u8 sText_HintA[] = _("{A_BUTTON}");
static const u8 sText_HintChoose[] = _("Choose");
static const u8 sText_HintEdit[] = _("Edit");
static const u8 sText_HintChange[] = _("Change");
static const u8 sText_HintRename[] = _("Rename");
static const u8 sText_HintPPUp[] = _("PP Up");
static const u8 sText_HintAll31[] = _("All 31");
static const u8 sText_HintSave[] = _(" {START_BUTTON}Save");
static const u8 sText_HintAdd[] = _(" {START_BUTTON}Add");
static const u8 sText_HintBack[] = _(" {B_BUTTON}Back");
static const u8 sText_HintFixed[] = _("Fixed for this species");
static const u8 sText_HintNoMove[] = _("Choose a move first");
static const u8 sText_HintMail[] = _("Take the MAIL off first");
static const u8 sText_HintNoRoom[] = _("No room in the party or PC");
static const u8 sText_HintUpDown[] = _("{DPAD_UPDOWN}");
static const u8 sText_HintLeftRight[] = _(" {DPAD_LEFTRIGHT}");
static const u8 sText_HintL[] = _(" {L_BUTTON}");
static const u8 sText_HintR[] = _(" {R_BUTTON}");
static const u8 sText_HintPicker[] = _("{L_BUTTON}{R_BUTTON}A-Z {A_BUTTON}OK {B_BUTTON}Back");
static const u8 sText_HintPickerShort[] = _("{A_BUTTON}OK {B_BUTTON}Back");
static const u8 sText_HintSelectAll[] = _("{SELECT_BUTTON}All");
static const u8 sText_HintSelectLearn[] = _("{SELECT_BUTTON}Learn");
static const u8 sText_LR[] = _("{L_BUTTON}");
static const u8 sText_RR[] = _("{R_BUTTON}");

// Picker titles
static const u8 sText_PickSpecies[] = _("SPECIES");
static const u8 sText_PickNature[] = _("NATURE");
static const u8 sText_PickItem[] = _("HELD ITEM");
static const u8 sText_PickBall[] = _("BALL");
static const u8 sText_PickHPType[] = _("HIDDEN POWER");
static const u8 sText_PickLearnable[] = _("LEARNABLE");
static const u8 sText_PickAllMoves[] = _("ALL MOVES");

// Prompt
static const u8 sText_PromptSave[] = _("Save changes?");
static const u8 sText_PromptAdd[] = _("Keep this one?");
static const u8 sText_PromptOptSave[] = _("{A_BUTTON} Save");
static const u8 sText_PromptOptAddParty[] = _("{A_BUTTON} Add to party");
static const u8 sText_PromptOptAddPC[] = _("{A_BUTTON} Send to PC");
static const u8 sText_PromptOptDiscard[] = _("{SELECT_BUTTON} Discard");
static const u8 sText_PromptOptBack[] = _("{B_BUTTON} Keep editing");
static const u8 sText_PromptPartyFull[] = _("Your party is full.");

// {bg, fg, shadow}
static const u8 sColor_Header[] = {COLOR_TRANSPARENT, COLOR_WHITE, COLOR_DARK_GRAY};
static const u8 sColor_Normal[] = {COLOR_WHITE, COLOR_DARK_GRAY, COLOR_LIGHT_GRAY};
static const u8 sColor_Label[]  = {COLOR_WHITE, COLOR_MID_GRAY, COLOR_LIGHT_GRAY};

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

bool8 PokemonEditor_IsAllowed(void)
{
    if (InBattlePyramid_() || InBattlePike())
        return FALSE;
    if (FlagGet(FLAG_OPT_EDIT_OFF)) // v1.39: OPTION > EDIT MENU
        return FALSE;
    return TRUE;
}

static bool8 AllocEditor(MainCallback exitCallback)
{
    sEd = AllocZeroed(sizeof(*sEd));
    if (sEd == NULL)
    {
        SetMainCallback2(exitCallback);
        return FALSE;
    }
    sEd->exitCallback = exitCallback;
    sEd->cursor = CELL_SPECIES;
    sEd->state = EDSTATE_NAV;
    return TRUE;
}

static void LoadCurrentMon(void)
{
    if (sEd->mode == EDMODE_PARTY)
        sEd->work = gPlayerParty[sEd->slot];
    else if (sEd->mode == EDMODE_BOX)
        BoxMonToMon(GetBoxedMonPtr(sEd->boxId, sEd->slot), &sEd->work);
    sEd->orig = sEd->work;
}

static void StartEditor(void)
{
    gMain.state = 0;
    SetMainCallback2(CB2_EditorSetup);
}

void PokemonEditor_OpenParty(u8 slot, MainCallback exitCallback)
{
    if (!AllocEditor(exitCallback))
        return;
    sEd->mode = EDMODE_PARTY;
    sEd->slot = slot;
    LoadCurrentMon();
    StartEditor();
}

void PokemonEditor_OpenBox(u8 boxId, u8 boxPos, bool8 allBoxes, MainCallback exitCallback)
{
    if (!AllocEditor(exitCallback))
        return;
    sEd->mode = EDMODE_BOX;
    sEd->boxId = boxId;
    sEd->slot = boxPos;
    sEd->allBoxes = allBoxes;
    LoadCurrentMon();
    StartEditor();
}

static u32 FindPersonality(u16 species, u32 otId, u8 nature, u8 gender, bool8 shiny, u32 fallback);

void PokemonEditor_OpenNew(MainCallback exitCallback)
{
    u32 otId, pid;

    if (!AllocEditor(exitCallback))
        return;
    sEd->mode = EDMODE_NEW;
    otId = gSaveBlock2Ptr->playerTrainerId[0]
         | (gSaveBlock2Ptr->playerTrainerId[1] << 8)
         | (gSaveBlock2Ptr->playerTrainerId[2] << 16)
         | (gSaveBlock2Ptr->playerTrainerId[3] << 24);
    pid = FindPersonality(SPECIES_BULBASAUR, otId, Random() % NUM_NATURES, MON_MALE, FALSE, Random32());
    CreateMon(&sEd->work, SPECIES_BULBASAUR, 50, MAX_PER_STAT_IVS, TRUE, pid, OT_ID_PLAYER_ID, 0);
    sEd->orig = sEd->work;
    StartEditor();
}

// ---------------------------------------------------------------------------
// Pokemon data helpers
// ---------------------------------------------------------------------------

static struct Pokemon *Work(void)
{
    return &sEd->work;
}

static u16 GetSpecies(void)
{
    return GetMonData(Work(), MON_DATA_SPECIES);
}

static bool8 IsDirty(void)
{
    if (sEd->mode == EDMODE_NEW)
        return TRUE;
    {
        const u8 *a = (const u8 *)&sEd->work.box;
        const u8 *b = (const u8 *)&sEd->orig.box;
        u32 i;
        for (i = 0; i < sizeof(struct BoxPokemon); i++)
        {
            if (a[i] != b[i])
                return TRUE;
        }
    }
    return FALSE;
}

static u8 GetEditorNature(void)
{
    return GetNatureFromPersonality(GetMonData(Work(), MON_DATA_PERSONALITY));
}

static bool8 HasFixedGender(u16 species)
{
    u8 ratio = gSpeciesInfo[species].genderRatio;
    return ratio == MON_MALE || ratio == MON_FEMALE || ratio == MON_GENDERLESS;
}

static bool8 HasTwoAbilities(u16 species)
{
    return gSpeciesInfo[species].abilities[1] != ABILITY_NONE
        && gSpeciesInfo[species].abilities[1] != gSpeciesInfo[species].abilities[0];
}

// Picks a personality with the given nature, gender and shininess.
// Gender comes from the low byte, shininess from both halves XORed with the
// OT ID, and nature from pid % 25, so the halves are chosen to satisfy the
// first two and the search only has to hit the nature (about 1 in 25).
static u32 FindPersonality(u16 species, u32 otId, u8 nature, u8 gender, bool8 shiny, u32 fallback)
{
    u8 ratio = gSpeciesInfo[species].genderRatio;
    u32 tries;

    for (tries = 0; tries < 20000; tries++)
    {
        u32 lo = Random();
        u32 hi;
        u32 pid;

        if (!HasFixedGender(species))
        {
            if (gender == MON_FEMALE)
                lo = (lo & 0xFF00) | (Random() % ratio);
            else
                lo = (lo & 0xFF00) | (ratio + Random() % (256 - ratio));
        }

        if (shiny)
            hi = ((otId >> 16) ^ (otId & 0xFFFF) ^ lo ^ (Random() & 7)) & 0xFFFF;
        else
            hi = Random();

        pid = (hi << 16) | lo;
        if (!shiny && IsShinyOtIdPersonality(otId, pid))
            continue;
        if (GetNatureFromPersonality(pid) == nature)
            return pid;
    }
    return fallback;
}

// Personality changes (nature, gender, shininess) keep everything else
static void RebuildWithPersonality(struct Pokemon *mon, u32 pid)
{
    SetBoxMonPersonalityAndOtId(&mon->box, pid, GetMonData(mon, MON_DATA_OT_ID));
    CalculateMonStats(mon);
}

static void SetPersonalityTraits(u8 nature, u8 gender, bool8 shiny)
{
    struct Pokemon *mon = Work();
    u32 oldPid = GetMonData(mon, MON_DATA_PERSONALITY);
    u32 pid = FindPersonality(GetSpecies(), GetMonData(mon, MON_DATA_OT_ID), nature, gender, shiny, oldPid);

    if (pid != oldPid)
        RebuildWithPersonality(mon, pid);
}

static void RecalcStats(void)
{
    CalculateMonStats(Work());
}

static void ResetMovesToLevelUp(void)
{
    struct Pokemon *mon = Work();
    u32 zero = 0;
    u8 i;

    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        SetMonData(mon, MON_DATA_MOVE1 + i, &zero);
        SetMonData(mon, MON_DATA_PP1 + i, &zero);
    }
    SetMonData(mon, MON_DATA_PP_BONUSES, &zero);
    GiveMonInitialMoveset(mon);
}

static void SetLevel(u8 level)
{
    struct Pokemon *mon = Work();
    u32 exp = gExperienceTables[gSpeciesInfo[GetSpecies()].growthRate][level];

    SetMonData(mon, MON_DATA_EXP, &exp);
    RecalcStats();
    if (sEd->mode == EDMODE_NEW && !sEd->movesTouched)
        ResetMovesToLevelUp();
}

static void SetSpecies(u16 species)
{
    struct Pokemon *mon = Work();
    u16 oldSpecies = GetSpecies();
    u8 level = GetMonData(mon, MON_DATA_LEVEL);
    u8 oldGender = GetMonGender(mon);
    u8 nameBuf[POKEMON_NAME_LENGTH + 10];
    bool8 defaultName;
    u32 value;

    if (species == oldSpecies)
        return;

    GetMonData(mon, MON_DATA_NICKNAME, nameBuf);
    StringGet_Nickname(nameBuf);
    defaultName = (StringCompare(nameBuf, gSpeciesNames[oldSpecies]) == 0);

    SetMonData(mon, MON_DATA_SPECIES, &species);
    value = gExperienceTables[gSpeciesInfo[species].growthRate][level];
    SetMonData(mon, MON_DATA_EXP, &value);

    if (GetMonData(mon, MON_DATA_ABILITY_NUM) && !HasTwoAbilities(species))
    {
        value = 0;
        SetMonData(mon, MON_DATA_ABILITY_NUM, &value);
    }

    // Keep the gender when the new species can have it
    if (!HasFixedGender(species) && oldGender != MON_GENDERLESS && GetMonGender(mon) != oldGender)
        SetPersonalityTraits(GetEditorNature(), oldGender, IsMonShiny(mon));

    if (defaultName)
        SetMonData(mon, MON_DATA_NICKNAME, gSpeciesNames[species]);

    RecalcStats();
    if (sEd->mode == EDMODE_NEW && !sEd->movesTouched)
        ResetMovesToLevelUp();
}

static u16 GetEVTotal(void)
{
    u16 total = 0;
    u8 i;

    for (i = 0; i < NUM_STATS; i++)
        total += GetMonData(Work(), MON_DATA_HP_EV + i);
    return total;
}

static u8 GetHiddenPowerTypeIndex(void)
{
    u8 bits = 0;
    static const u8 sOrder[NUM_STATS] = {STAT_HP, STAT_ATK, STAT_DEF, STAT_SPEED, STAT_SPATK, STAT_SPDEF};
    u8 i;

    for (i = 0; i < NUM_STATS; i++)
        bits |= (GetMonData(Work(), MON_DATA_HP_IV + sOrder[i]) & 1) << i;
    return (15 * bits) / 63;
}

static u8 GetHiddenPowerPower(void)
{
    u8 bits = 0;
    static const u8 sOrder[NUM_STATS] = {STAT_HP, STAT_ATK, STAT_DEF, STAT_SPEED, STAT_SPATK, STAT_SPDEF};
    u8 i;

    for (i = 0; i < NUM_STATS; i++)
        bits |= ((GetMonData(Work(), MON_DATA_HP_IV + sOrder[i]) & 2) >> 1) << i;
    return (40 * bits) / 63 + 30;
}

static u8 HiddenPowerIndexToType(u8 index)
{
    u8 type = index + 1;
    if (type >= TYPE_MYSTERY)
        type++;
    return type;
}

static u8 CountBits6(u8 v)
{
    u8 n = 0;
    while (v)
    {
        n += v & 1;
        v >>= 1;
    }
    return n;
}

// Bit pattern of IV low bits for a Hidden Power type that changes as few
// IVs as possible from the current ones (ties: more 31s than 30s).
static u8 GetHiddenPowerPattern(u8 typeIndex)
{
    static const u8 sOrder[NUM_STATS] = {STAT_HP, STAT_ATK, STAT_DEF, STAT_SPEED, STAT_SPATK, STAT_SPDEF};
    u8 current = 0, best = 0, bestDiff = 0xFF, bestOnes = 0;
    u8 p, i;

    for (i = 0; i < NUM_STATS; i++)
        current |= (GetMonData(Work(), MON_DATA_HP_IV + sOrder[i]) & 1) << i;

    for (p = 0; p < 64; p++)
    {
        u8 diff, ones;
        if ((15 * p) / 63 != typeIndex)
            continue;
        diff = CountBits6(p ^ current);
        ones = CountBits6(p);
        if (diff < bestDiff || (diff == bestDiff && ones > bestOnes))
        {
            best = p;
            bestDiff = diff;
            bestOnes = ones;
        }
    }
    return best;
}

static void SetHiddenPowerType(u8 typeIndex)
{
    static const u8 sOrder[NUM_STATS] = {STAT_HP, STAT_ATK, STAT_DEF, STAT_SPEED, STAT_SPATK, STAT_SPDEF};
    u8 pattern = GetHiddenPowerPattern(typeIndex);
    u8 i;

    for (i = 0; i < NUM_STATS; i++)
    {
        u32 iv = GetMonData(Work(), MON_DATA_HP_IV + sOrder[i]);
        iv = (iv & ~1) | ((pattern >> i) & 1);
        SetMonData(Work(), MON_DATA_HP_IV + sOrder[i], &iv);
    }
    RecalcStats();
}


// Moves --------------------------------------------------------------------

struct MoveSlot
{
    u16 move;
    u8 pp;
    u8 ppUps;
};

static void ReadMoveSlots(struct MoveSlot *slots)
{
    u8 bonuses = GetMonData(Work(), MON_DATA_PP_BONUSES);
    u8 i;

    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        slots[i].move = GetMonData(Work(), MON_DATA_MOVE1 + i);
        slots[i].pp = GetMonData(Work(), MON_DATA_PP1 + i);
        slots[i].ppUps = (bonuses >> (i * 2)) & 3;
    }
}

// Writes the moves back packed to the front (no gaps before a move)
static void WriteMoveSlots(struct MoveSlot *slots)
{
    struct MoveSlot packed[MAX_MON_MOVES];
    u32 value, bonuses = 0;
    u8 i, n = 0;

    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        if (slots[i].move != MOVE_NONE)
            packed[n++] = slots[i];
    }
    for (i = n; i < MAX_MON_MOVES; i++)
    {
        packed[i].move = MOVE_NONE;
        packed[i].pp = 0;
        packed[i].ppUps = 0;
    }
    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        value = packed[i].move;
        SetMonData(Work(), MON_DATA_MOVE1 + i, &value);
        value = packed[i].pp;
        SetMonData(Work(), MON_DATA_PP1 + i, &value);
        bonuses |= (packed[i].ppUps & 3) << (i * 2);
    }
    SetMonData(Work(), MON_DATA_PP_BONUSES, &bonuses);
}

static u8 CountMoves(void)
{
    u8 i, n = 0;
    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        if (GetMonData(Work(), MON_DATA_MOVE1 + i) != MOVE_NONE)
            n++;
    }
    return n;
}

static u8 MaxPPForSlot(struct MoveSlot *slot, u8 index)
{
    return CalculatePPWithBonus(slot->move, slot->ppUps << (index * 2), index);
}

static void SetMoveInSlot(u8 index, u16 move)
{
    struct MoveSlot slots[MAX_MON_MOVES];
    u8 i;

    ReadMoveSlots(slots);
    if (move == MOVE_NONE)
    {
        slots[index].move = MOVE_NONE;
    }
    else
    {
        for (i = 0; i < MAX_MON_MOVES; i++)
        {
            if (i != index && slots[i].move == move)
                break;
        }
        if (i < MAX_MON_MOVES)
        {
            // Already known in another slot: swap the two
            struct MoveSlot tmp = slots[i];
            slots[i] = slots[index];
            slots[index] = tmp;
        }
        else
        {
            slots[index].move = move;
            slots[index].pp = MaxPPForSlot(&slots[index], index);
        }
    }
    WriteMoveSlots(slots);
    sEd->movesTouched = TRUE;
}

static void CyclePPUps(u8 index)
{
    struct MoveSlot slots[MAX_MON_MOVES];

    ReadMoveSlots(slots);
    if (slots[index].move == MOVE_NONE)
        return;
    slots[index].ppUps = (slots[index].ppUps + 1) & 3;
    slots[index].pp = MaxPPForSlot(&slots[index], index);
    WriteMoveSlots(slots);
    sEd->movesTouched = TRUE;
}

// Learnable moves -----------------------------------------------------------

static void AddLearnable(u16 move)
{
    if (move == MOVE_NONE || move >= MOVES_COUNT)
        return;
    if (!(sEd->learnable[move / 8] & (1 << (move % 8))))
    {
        sEd->learnable[move / 8] |= 1 << (move % 8);
        sEd->learnableCount++;
    }
}

static bool8 IsLearnable(u16 move)
{
    return (sEd->learnable[move / 8] & (1 << (move % 8))) != 0;
}

static u16 GetPreEvolution(u16 species)
{
    u16 i;
    u8 j;

    for (i = 1; i < NUM_SPECIES; i++)
    {
        for (j = 0; j < EVOS_PER_MON; j++)
        {
            if (gEvolutionTable[i][j].method != 0 && gEvolutionTable[i][j].targetSpecies == species)
                return i;
        }
    }
    return SPECIES_NONE;
}

static void AddSpeciesMoves(u16 species)
{
    const u16 *learnset = gLevelUpLearnsets[species];
    u16 i;

    for (i = 0; learnset[i] != LEVEL_UP_END; i++)
        AddLearnable(learnset[i] & LEVEL_UP_MOVE_ID);
    for (i = 0; i < NUM_TECHNICAL_MACHINES + NUM_HIDDEN_MACHINES; i++)
    {
        if (CanSpeciesLearnTMHM(species, i))
            AddLearnable(ItemIdToBattleMoveId(ITEM_TM01 + i));
    }
    for (i = 0; i < TUTOR_MOVE_COUNT; i++)
    {
        if (CanSpeciesLearnTutorMove(species, i))
            AddLearnable(gTutorMoves[i]);
    }
}

static void AddEggMoves(u16 species)
{
    u16 i;

    for (i = 0; gEggMoves[i] != EGG_MOVES_TERMINATOR; i++)
    {
        if (gEggMoves[i] == species + EGG_MOVES_SPECIES_OFFSET)
        {
            for (i++; gEggMoves[i] != EGG_MOVES_TERMINATOR && gEggMoves[i] < EGG_MOVES_SPECIES_OFFSET; i++)
                AddLearnable(gEggMoves[i]);
            return;
        }
    }
}

// Level-up, TM/HM and tutor moves of the species and its pre-evolutions,
// plus the egg moves of the base form.
static void BuildLearnable(u16 species)
{
    u8 depth;

    if (sEd->learnableSpecies == species && sEd->learnableCount != 0)
        return;
    memset(sEd->learnable, 0, sizeof(sEd->learnable));
    sEd->learnableCount = 0;
    sEd->learnableSpecies = species;

    for (depth = 0; depth < 3 && species != SPECIES_NONE; depth++)
    {
        u16 prev;
        AddSpeciesMoves(species);
        prev = GetPreEvolution(species);
        if (prev == SPECIES_NONE)
        {
            AddEggMoves(species);
            break;
        }
        species = prev;
    }
}

// ---------------------------------------------------------------------------
// Sorted lists
// ---------------------------------------------------------------------------

static const u8 *GetPickName(u8 kind, u16 id)
{
    switch (kind)
    {
    case PICK_SPECIES:
        return gSpeciesNames[id];
    case PICK_NATURE:
        return gNatureNamePointers[id];
    case PICK_ITEM:
    case PICK_BALL:
        if (id == ITEM_NONE)
            return sText_NoneWord;
        return gItems[id].name;
    case PICK_HPTYPE:
        return gTypeNames[HiddenPowerIndexToType(id)];
    case PICK_MOVE:
    default:
        if (id == MOVE_NONE)
            return sText_None;
        return gMoveNames[id];
    }
}

// Name order with shorter names first ("NIDORAN" before "NIDORINA")
static s32 CompareNames(const u8 *a, const u8 *b)
{
    while (*a == *b)
    {
        if (*a == EOS)
            return 0;
        a++;
        b++;
    }
    if (*a == EOS)
        return -1;
    if (*b == EOS)
        return 1;
    return *a - *b;
}

// First four characters packed so most comparisons are one integer compare
static u32 NameSortKey(const u8 *name)
{
    u32 key = 0;
    u8 i;

    for (i = 0; i < 4; i++)
    {
        key <<= 8;
        if (*name != EOS)
            key |= *name++;
    }
    return key;
}

// Shellsort by name (the lists are a few hundred entries)
static void SortByName(u16 *ids, u16 count, u8 kind)
{
    static const u16 sGaps[] = {132, 57, 23, 10, 4, 1};
    u32 *keys = Alloc(sizeof(u32) * count);
    u16 g, i, j, tmp;
    u32 tmpKey;

    if (keys == NULL)
        return; // unsorted, but still usable
    for (i = 0; i < count; i++)
        keys[i] = NameSortKey(GetPickName(kind, ids[i]));

    for (g = 0; g < ARRAY_COUNT(sGaps); g++)
    {
        u16 gap = sGaps[g];
        for (i = gap; i < count; i++)
        {
            tmp = ids[i];
            tmpKey = keys[i];
            for (j = i; j >= gap; j -= gap)
            {
                if (keys[j - gap] < tmpKey)
                    break;
                if (keys[j - gap] == tmpKey
                 && CompareNames(GetPickName(kind, ids[j - gap]), GetPickName(kind, tmp)) <= 0)
                    break;
                ids[j] = ids[j - gap];
                keys[j] = keys[j - gap];
            }
            ids[j] = tmp;
            keys[j] = tmpKey;
        }
    }
    Free(keys);
}

static void EnsureSortedSpecies(void)
{
    u16 s;

    if (sEd->sortedSpecies != NULL)
        return;
    sEd->sortedSpecies = Alloc(sizeof(u16) * NUM_SPECIES);
    if (sEd->sortedSpecies == NULL)
        return;
    sEd->sortedSpeciesCount = 0;
    for (s = 1; s < NUM_SPECIES; s++)
    {
        if (gSpeciesNames[s][0] == CHAR_QUESTION_MARK)
            continue; // unused Unown placeholders
        sEd->sortedSpecies[sEd->sortedSpeciesCount++] = s;
    }
    SortByName(sEd->sortedSpecies, sEd->sortedSpeciesCount, PICK_SPECIES);
}

static void EnsureSortedMoves(void)
{
    u16 m;

    if (sEd->sortedMoves != NULL)
        return;
    sEd->sortedMoves = Alloc(sizeof(u16) * MOVES_COUNT);
    if (sEd->sortedMoves == NULL)
        return;
    sEd->sortedMovesCount = 0;
    for (m = 1; m < MOVES_COUNT; m++)
        sEd->sortedMoves[sEd->sortedMovesCount++] = m;
    SortByName(sEd->sortedMoves, sEd->sortedMovesCount, PICK_MOVE);
}

static bool8 IsHoldableItem(u16 item)
{
    if (item == ITEM_NONE || item >= ITEMS_COUNT)
        return FALSE;
    if (gItems[item].name[0] == CHAR_QUESTION_MARK)
        return FALSE; // unused item slots
    if (gItems[item].pocket == POCKET_KEY_ITEMS)
        return FALSE;
    if (item >= ITEM_HM01 && item <= ITEM_HM08)
        return FALSE;
    if (ItemIsMail(item))
        return FALSE;
    return TRUE;
}

static void EnsureSortedItems(void)
{
    u16 i;

    if (sEd->sortedItems != NULL)
        return;
    sEd->sortedItems = Alloc(sizeof(u16) * ITEMS_COUNT);
    if (sEd->sortedItems == NULL)
        return;
    sEd->sortedItemsCount = 0;
    for (i = 1; i < ITEMS_COUNT; i++)
    {
        if (IsHoldableItem(i))
            sEd->sortedItems[sEd->sortedItemsCount++] = i;
    }
    SortByName(sEd->sortedItems, sEd->sortedItemsCount, PICK_ITEM);
}

static void FreeSortedLists(void)
{
    TRY_FREE_AND_SET_NULL(sEd->sortedSpecies);
    TRY_FREE_AND_SET_NULL(sEd->sortedMoves);
    TRY_FREE_AND_SET_NULL(sEd->sortedItems);
    TRY_FREE_AND_SET_NULL(sEd->pickIds);
}

// ---------------------------------------------------------------------------
// Picker
// ---------------------------------------------------------------------------

static u16 GetCurrentPickValue(void)
{
    switch (sEd->pickKind)
    {
    case PICK_SPECIES:
        return GetSpecies();
    case PICK_NATURE:
        return GetEditorNature();
    case PICK_ITEM:
        return GetMonData(Work(), MON_DATA_HELD_ITEM);
    case PICK_BALL:
        return GetMonData(Work(), MON_DATA_POKEBALL);
    case PICK_HPTYPE:
        return GetHiddenPowerTypeIndex();
    case PICK_MOVE:
    default:
        return GetMonData(Work(), MON_DATA_MOVE1 + sEd->pickSlot);
    }
}

static void BuildPickList(void)
{
    u16 i, n = 0;
    u16 *ids = sEd->pickIds;

    switch (sEd->pickKind)
    {
    case PICK_SPECIES:
        EnsureSortedSpecies();
        for (i = 0; i < sEd->sortedSpeciesCount; i++)
            ids[n++] = sEd->sortedSpecies[i];
        break;
    case PICK_NATURE:
        for (i = 0; i < NUM_NATURES; i++)
            ids[n++] = i;
        SortByName(ids, n, PICK_NATURE);
        break;
    case PICK_ITEM:
        EnsureSortedItems();
        ids[n++] = ITEM_NONE;
        for (i = 0; i < sEd->sortedItemsCount; i++)
            ids[n++] = sEd->sortedItems[i];
        break;
    case PICK_BALL:
        for (i = FIRST_BALL; i <= LAST_BALL; i++)
            ids[n++] = i;
        break;
    case PICK_HPTYPE:
        for (i = 0; i < 16; i++)
            ids[n++] = i;
        break;
    case PICK_MOVE:
        EnsureSortedMoves();
        BuildLearnable(GetSpecies());
        // "-" clears the slot, unless it's the only move left
        if (!(CountMoves() <= 1 && GetMonData(Work(), MON_DATA_MOVE1 + sEd->pickSlot) != MOVE_NONE))
            ids[n++] = MOVE_NONE;
        for (i = 0; i < sEd->sortedMovesCount; i++)
        {
            u16 move = sEd->sortedMoves[i];
            if (sEd->pickShowAll || IsLearnable(move))
                ids[n++] = move;
        }
        break;
    }
    sEd->pickCount = n;
}

static void SetPickCursorTo(u16 value)
{
    u16 i;

    sEd->pickCursor = 0;
    for (i = 0; i < sEd->pickCount; i++)
    {
        if (sEd->pickIds[i] == value)
        {
            sEd->pickCursor = i;
            break;
        }
    }
    if (sEd->pickCursor < PICKER_ROWS / 2)
        sEd->pickTop = 0;
    else
        sEd->pickTop = sEd->pickCursor - PICKER_ROWS / 2;
    if (sEd->pickCount > PICKER_ROWS && sEd->pickTop > sEd->pickCount - PICKER_ROWS)
        sEd->pickTop = sEd->pickCount - PICKER_ROWS;
    if (sEd->pickCount <= PICKER_ROWS)
        sEd->pickTop = 0;
}

static void OpenPicker(u8 kind, u8 slot)
{
    u16 capacity = NUM_SPECIES;

    if (capacity < MOVES_COUNT + 1)
        capacity = MOVES_COUNT + 1;
    if (capacity < ITEMS_COUNT + 1)
        capacity = ITEMS_COUNT + 1;

    TRY_FREE_AND_SET_NULL(sEd->pickIds);
    sEd->pickIds = Alloc(sizeof(u16) * capacity);
    if (sEd->pickIds == NULL)
    {
        PlaySE(SE_FAILURE);
        return;
    }
    sEd->pickKind = kind;
    sEd->pickSlot = slot;
    if (kind == PICK_MOVE)
        sEd->pickShowAll = FALSE;
    BuildPickList();
    if (kind == PICK_MOVE && sEd->pickCount <= 1)
    {
        // Nothing learnable found: start on the full list
        sEd->pickShowAll = TRUE;
        BuildPickList();
    }
    SetPickCursorTo(GetCurrentPickValue());
    sEd->state = EDSTATE_PICKER;
}

static void ClosePicker(void)
{
    TRY_FREE_AND_SET_NULL(sEd->pickIds);
    sEd->state = EDSTATE_NAV;
}

static void MovePickCursor(s16 delta)
{
    s32 c = (s32)sEd->pickCursor + delta;

    if (sEd->pickCount == 0)
        return;
    if (c < 0)
        c = (delta == -1) ? sEd->pickCount - 1 : 0;
    else if (c >= sEd->pickCount)
        c = (delta == 1) ? 0 : sEd->pickCount - 1;
    sEd->pickCursor = c;
    if (sEd->pickCursor < sEd->pickTop)
        sEd->pickTop = sEd->pickCursor;
    else if (sEd->pickCursor >= sEd->pickTop + PICKER_ROWS)
        sEd->pickTop = sEd->pickCursor - PICKER_ROWS + 1;
}

static u8 GetPickInitial(u16 index)
{
    const u8 *name = GetPickName(sEd->pickKind, sEd->pickIds[index]);
    if ((sEd->pickKind == PICK_MOVE && sEd->pickIds[index] == MOVE_NONE)
     || (sEd->pickKind == PICK_ITEM && sEd->pickIds[index] == ITEM_NONE))
        return 0; // "-" / NONE are their own group
    return name[0];
}

// L/R: jump to the start of the previous/next first-letter group
static void JumpPickLetter(bool8 forward)
{
    u16 c = sEd->pickCursor;
    u8 initial;

    if (sEd->pickCount == 0)
        return;
    if (forward)
    {
        initial = GetPickInitial(c);
        while (c < sEd->pickCount - 1 && GetPickInitial(c) == initial)
            c++;
        if (GetPickInitial(c) == initial)
            c = 0;
    }
    else
    {
        // Go to the start of this group, or the start of the previous one
        if (c > 0 && GetPickInitial(c - 1) == GetPickInitial(c))
        {
            initial = GetPickInitial(c);
            while (c > 0 && GetPickInitial(c - 1) == initial)
                c--;
        }
        else
        {
            if (c == 0)
                c = sEd->pickCount;
            c--;
            initial = GetPickInitial(c);
            while (c > 0 && GetPickInitial(c - 1) == initial)
                c--;
        }
    }
    sEd->pickCursor = c;
    if (sEd->pickCursor < sEd->pickTop || sEd->pickCursor >= sEd->pickTop + PICKER_ROWS)
    {
        sEd->pickTop = sEd->pickCursor;
        if (sEd->pickCount > PICKER_ROWS && sEd->pickTop > sEd->pickCount - PICKER_ROWS)
            sEd->pickTop = sEd->pickCount - PICKER_ROWS;
    }
}

static void ApplyPick(void)
{
    u16 id = sEd->pickIds[sEd->pickCursor];
    u32 value = id;

    switch (sEd->pickKind)
    {
    case PICK_SPECIES:
        SetSpecies(id);
        break;
    case PICK_NATURE:
        if (id != GetEditorNature())
            SetPersonalityTraits(id, GetMonGender(Work()), IsMonShiny(Work()));
        RecalcStats();
        break;
    case PICK_ITEM:
        SetMonData(Work(), MON_DATA_HELD_ITEM, &value);
        break;
    case PICK_BALL:
        SetMonData(Work(), MON_DATA_POKEBALL, &value);
        break;
    case PICK_HPTYPE:
        SetHiddenPowerType(id);
        break;
    case PICK_MOVE:
        SetMoveInSlot(sEd->pickSlot, id);
        break;
    }
}

// ---------------------------------------------------------------------------
// Number editing
// ---------------------------------------------------------------------------

struct NumberSpec
{
    u16 min;
    u16 max;    // highest allowed
    u16 rValue; // what R jumps to
    u8 small;
    u8 large;
};

static bool8 IsNumberCell(u8 cell)
{
    return cell == CELL_LEVEL || cell == CELL_FRIEND
        || (cell >= CELL_IV1 && cell <= CELL_EV6);
}

static u8 CellStat(u8 cell)
{
    if (cell >= CELL_EV1)
        return sStatDisplayOrder[cell - CELL_EV1];
    return sStatDisplayOrder[cell - CELL_IV1];
}

static u16 GetCellNumber(u8 cell)
{
    if (cell == CELL_LEVEL)
        return GetMonData(Work(), MON_DATA_LEVEL);
    if (cell == CELL_FRIEND)
        return GetMonData(Work(), MON_DATA_FRIENDSHIP);
    if (cell >= CELL_EV1)
        return GetMonData(Work(), MON_DATA_HP_EV + CellStat(cell));
    return GetMonData(Work(), MON_DATA_HP_IV + CellStat(cell));
}

static void SetCellNumber(u8 cell, u16 v)
{
    u32 value = v;

    if (cell == CELL_LEVEL)
    {
        SetLevel(v);
        return;
    }
    if (cell == CELL_FRIEND)
        SetMonData(Work(), MON_DATA_FRIENDSHIP, &value);
    else if (cell >= CELL_EV1)
        SetMonData(Work(), MON_DATA_HP_EV + CellStat(cell), &value);
    else
        SetMonData(Work(), MON_DATA_HP_IV + CellStat(cell), &value);
    RecalcStats();
}

static void GetNumberSpec(u8 cell, struct NumberSpec *spec)
{
    if (cell == CELL_LEVEL)
    {
        spec->min = 1;
        spec->max = MAX_LEVEL;
        spec->small = 1;
        spec->large = 10;
    }
    else if (cell == CELL_FRIEND)
    {
        spec->min = 0;
        spec->max = 255;
        spec->small = 1;
        spec->large = 10;
    }
    else if (cell >= CELL_EV1)
    {
        u16 others = GetEVTotal() - GetCellNumber(cell);
        spec->min = 0;
        spec->max = MAX_PER_STAT_EVS;
        if (others >= MAX_TOTAL_EVS)
            spec->max = 0;
        else if (others + spec->max > MAX_TOTAL_EVS)
            spec->max = MAX_TOTAL_EVS - others;
        spec->small = 4;
        spec->large = 32;
    }
    else
    {
        spec->min = 0;
        spec->max = MAX_PER_STAT_IVS;
        spec->small = 1;
        spec->large = 10;
    }
    spec->rValue = spec->max;
    if (cell >= CELL_EV1 && spec->rValue >= 252)
        spec->rValue = 252; // 255 wastes 3 EVs
}

static bool8 AdjustNumber(u8 cell, s16 delta)
{
    struct NumberSpec spec;
    s32 v = GetCellNumber(cell);

    GetNumberSpec(cell, &spec);
    v += delta;
    if (v < spec.min)
        v = spec.min;
    if (v > spec.max)
        v = spec.max;
    if (v != GetCellNumber(cell))
    {
        PlaySE(SE_SELECT);
        SetCellNumber(cell, v);
        return TRUE;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Drawing
//
// Text rendering is the slow part of this screen, so after the first draw
// only what changed is redrawn: moving the cursor redraws the two cells and
// the hint, editing a number redraws that row, and moving through a list
// redraws two rows unless it scrolls.
// ---------------------------------------------------------------------------

// FillWindowPixelRect sets one pixel at a time; this writes whole 8-pixel
// tile rows where it can, which matters with as many cells as this screen.
static void FastFillRect(u8 win, u8 color, u16 x, u16 y, u16 w, u16 h)
{
    u32 *tiles = (u32 *)GetWindowAttribute(win, WINDOW_TILE_DATA);
    u16 width = GetWindowAttribute(win, WINDOW_WIDTH);
    u32 fill = (color & 0xF) * 0x11111111;
    u16 py, px, xEnd = x + w;

    for (py = y; py < y + h; py++)
    {
        u32 *row = &tiles[(py / 8) * width * 8 + (py % 8)];
        px = x;
        while (px < xEnd)
        {
            u32 *word = &row[(px / 8) * 8];
            if ((px % 8) == 0 && px + 8 <= xEnd)
            {
                *word = fill;
                px += 8;
            }
            else
            {
                u32 shift = (px % 8) * 4;
                *word = (*word & ~(0xFu << shift)) | ((color & 0xFu) << shift);
                px++;
            }
        }
    }
}

static u8 GetCellBg(u8 cell)
{
    if (cell == sEd->cursor && sEd->state != EDSTATE_PROMPT)
    {
        if (sEd->state == EDSTATE_NUMBER)
            return COLOR_EDITING;
        return COLOR_CURSOR;
    }
    if (sCells[cell].win == WIN_HEADER)
        return COLOR_TRANSPARENT;
    return COLOR_WHITE;
}

static void PrintAt(u8 win, u8 font, u8 x, u8 y, u8 bg, u8 fg, u8 shadow, const u8 *str)
{
    u8 color[3];
    color[0] = bg;
    color[1] = fg;
    color[2] = shadow;
    AddTextPrinterParameterized3(win, font, x, y, color, TEXT_SKIP_DRAW, str);
}

static void PrintRightAligned(u8 win, u8 rightX, u8 y, const u8 *color, const u8 *str)
{
    s32 x = rightX - GetStringWidth(FONT_SMALL, str, 0);
    if (x < 0)
        x = 0;
    AddTextPrinterParameterized3(win, FONT_SMALL, x, y, color, TEXT_SKIP_DRAW, str);
}

// Clears the cell's area (highlighted if selected) and prints its text.
// Small-font glyphs are 13px tall on a 12px row pitch, so a cell's area
// shares its top row with the glyphs above (a blank row in every glyph);
// when two cells are redrawn the selected one goes last.
static void PrintCell(u8 cell, const u8 *str, u8 fg, u8 shadow, bool8 rightAlign)
{
    const struct EditorCell *c = &sCells[cell];
    u8 bg = GetCellBg(cell);
    u8 font = (c->win == WIN_HEADER) ? FONT_NORMAL : FONT_SMALL;
    s32 x;

    if (c->win == WIN_HEADER)
        FastFillRect(c->win, bg, c->x, 0, c->w, 16);
    else
        FastFillRect(c->win, bg, c->x, c->y, c->w, 13);
    if ((bg == COLOR_CURSOR || bg == COLOR_EDITING) && fg == COLOR_WHITE)
    {
        fg = COLOR_DARK_GRAY;
        shadow = COLOR_LIGHT_GRAY;
    }

    if (rightAlign)
        x = c->x + c->w - 2 - GetStringWidth(font, str, 0);
    else
        x = c->x + 2;
    if (x < 0)
        x = 0;
    PrintAt(c->win, font, x, c->y, bg, fg, shadow, str);
}

static void DrawCell(u8 cell)
{
    struct Pokemon *mon = Work();
    u8 buffer[32];
    u8 *end;
    u16 species = GetSpecies();

    switch (cell)
    {
    case CELL_NICK:
        GetMonData(mon, MON_DATA_NICKNAME, buffer);
        StringGet_Nickname(buffer);
        PrintCell(cell, buffer, COLOR_WHITE, COLOR_DARK_GRAY, FALSE);
        break;
    case CELL_SPECIES:
        PrintCell(cell, gSpeciesNames[species], COLOR_DARK_GRAY, COLOR_LIGHT_GRAY, FALSE);
        break;
    case CELL_LEVEL:
        end = StringCopy(buffer, sText_Lv);
        ConvertIntToDecimalStringN(end, GetMonData(mon, MON_DATA_LEVEL), STR_CONV_MODE_LEFT_ALIGN, 3);
        PrintCell(cell, buffer, COLOR_DARK_GRAY, COLOR_LIGHT_GRAY, TRUE);
        break;
    case CELL_GENDER:
        switch (GetMonGender(mon))
        {
        case MON_MALE:
            PrintCell(cell, sText_Male, COLOR_BLUE, COLOR_LIGHT_BLUE, FALSE);
            break;
        case MON_FEMALE:
            PrintCell(cell, sText_Female, COLOR_RED, COLOR_LIGHT_RED, FALSE);
            break;
        default:
            PrintCell(cell, sText_Genderless, COLOR_MID_GRAY, COLOR_LIGHT_GRAY, FALSE);
            break;
        }
        break;
    case CELL_NATURE:
        PrintCell(cell, gNatureNamePointers[GetEditorNature()], COLOR_DARK_GRAY, COLOR_LIGHT_GRAY, FALSE);
        break;
    case CELL_ABILITY:
        PrintCell(cell, gAbilityNames[GetAbilityBySpecies(species, GetMonData(mon, MON_DATA_ABILITY_NUM))],
                  COLOR_DARK_GRAY, COLOR_LIGHT_GRAY, FALSE);
        break;
    case CELL_ITEM:
    {
        u16 item = GetMonData(mon, MON_DATA_HELD_ITEM);
        if (item == ITEM_NONE)
            PrintCell(cell, sText_None, COLOR_MID_GRAY, COLOR_LIGHT_GRAY, FALSE);
        else
            PrintCell(cell, gItems[item].name, COLOR_DARK_GRAY, COLOR_LIGHT_GRAY, FALSE);
        break;
    }
    case CELL_BALL:
        PrintCell(cell, gItems[GetMonData(mon, MON_DATA_POKEBALL)].name, COLOR_DARK_GRAY, COLOR_LIGHT_GRAY, FALSE);
        break;
    case CELL_HPTYPE:
        end = StringCopy(buffer, gTypeNames[HiddenPowerIndexToType(GetHiddenPowerTypeIndex())]);
        end = StringCopy(end, sText_Space);
        ConvertIntToDecimalStringN(end, GetHiddenPowerPower(), STR_CONV_MODE_LEFT_ALIGN, 2);
        PrintCell(cell, buffer, COLOR_DARK_GRAY, COLOR_LIGHT_GRAY, FALSE);
        break;
    case CELL_MOVE1:
    case CELL_MOVE2:
    case CELL_MOVE3:
    case CELL_MOVE4:
    {
        u16 move = GetMonData(mon, MON_DATA_MOVE1 + (cell - CELL_MOVE1));
        if (move == MOVE_NONE)
            PrintCell(cell, sText_None, COLOR_MID_GRAY, COLOR_LIGHT_GRAY, FALSE);
        else
            PrintCell(cell, gMoveNames[move], COLOR_DARK_GRAY, COLOR_LIGHT_GRAY, FALSE);
        break;
    }
    case CELL_PP1:
    case CELL_PP2:
    case CELL_PP3:
    case CELL_PP4:
    {
        struct MoveSlot slots[MAX_MON_MOVES];
        u8 i = cell - CELL_PP1;
        ReadMoveSlots(slots);
        if (slots[i].move == MOVE_NONE)
        {
            PrintCell(cell, sText_None, COLOR_MID_GRAY, COLOR_LIGHT_GRAY, TRUE);
        }
        else
        {
            end = StringCopy(buffer, sText_Plus);
            end = ConvertIntToDecimalStringN(end, slots[i].ppUps, STR_CONV_MODE_LEFT_ALIGN, 1);
            end = StringCopy(end, sText_Space);
            ConvertIntToDecimalStringN(end, MaxPPForSlot(&slots[i], i), STR_CONV_MODE_LEFT_ALIGN, 2);
            if (slots[i].ppUps == 3)
                PrintCell(cell, buffer, COLOR_GREEN, COLOR_LIGHT_GREEN, TRUE);
            else
                PrintCell(cell, buffer, COLOR_DARK_GRAY, COLOR_LIGHT_GRAY, TRUE);
        }
        break;
    }
    case CELL_IV1:
    case CELL_IV2:
    case CELL_IV3:
    case CELL_IV4:
    case CELL_IV5:
    case CELL_IV6:
    {
        u8 iv = GetMonData(mon, MON_DATA_HP_IV + CellStat(cell));
        ConvertIntToDecimalStringN(buffer, iv, STR_CONV_MODE_LEFT_ALIGN, 2);
        if (iv == MAX_PER_STAT_IVS)
            PrintCell(cell, buffer, COLOR_GREEN, COLOR_LIGHT_GREEN, TRUE);
        else
            PrintCell(cell, buffer, COLOR_DARK_GRAY, COLOR_LIGHT_GRAY, TRUE);
        break;
    }
    case CELL_EV1:
    case CELL_EV2:
    case CELL_EV3:
    case CELL_EV4:
    case CELL_EV5:
    case CELL_EV6:
        ConvertIntToDecimalStringN(buffer, GetMonData(mon, MON_DATA_HP_EV + CellStat(cell)), STR_CONV_MODE_LEFT_ALIGN, 3);
        PrintCell(cell, buffer, COLOR_DARK_GRAY, COLOR_LIGHT_GRAY, TRUE);
        break;
    case CELL_MAXIV:
        PrintCell(cell, sText_MaxIVs, COLOR_DARK_GRAY, COLOR_LIGHT_GRAY, FALSE);
        break;
    case CELL_SHINY:
        end = StringCopy(buffer, sText_Shiny);
        if (IsMonShiny(mon))
        {
            StringCopy(end, sText_Yes);
            PrintCell(cell, buffer, COLOR_ORANGE, COLOR_LIGHT_ORANGE, FALSE);
        }
        else
        {
            StringCopy(end, sText_No);
            PrintCell(cell, buffer, COLOR_DARK_GRAY, COLOR_LIGHT_GRAY, FALSE);
        }
        break;
    case CELL_FRIEND:
        end = StringCopy(buffer, sText_Friend);
        ConvertIntToDecimalStringN(end, GetMonData(mon, MON_DATA_FRIENDSHIP), STR_CONV_MODE_LEFT_ALIGN, 3);
        PrintCell(cell, buffer, COLOR_DARK_GRAY, COLOR_LIGHT_GRAY, FALSE);
        break;
    }
}

static void CopyGfx(u8 win)
{
    CopyWindowToVram(win, COPYWIN_GFX);
}

static void DrawHeader(void)
{
    u8 buffer[24];
    u8 *end;
    u16 color = BG_COLOR_EDITOR;

    FillWindowPixelBuffer(WIN_HEADER, PIXEL_FILL(COLOR_TRANSPARENT));
    DrawCell(CELL_NICK);

    if (sEd->mode == EDMODE_NEW)
    {
        StringCopy(buffer, sText_New);
    }
    else if (sEd->mode == EDMODE_PARTY)
    {
        end = StringCopy(buffer, sText_LR);
        end = ConvertIntToDecimalStringN(end, sEd->slot + 1, STR_CONV_MODE_LEFT_ALIGN, 1);
        end = StringCopy(end, sText_Slash);
        end = ConvertIntToDecimalStringN(end, CalculatePlayerPartyCount(), STR_CONV_MODE_LEFT_ALIGN, 1);
        StringCopy(end, sText_RR);
    }
    else
    {
        end = StringCopy(buffer, sText_LR);
        end = StringCopy(end, sText_Box);
        end = ConvertIntToDecimalStringN(end, sEd->boxId + 1, STR_CONV_MODE_LEFT_ALIGN, 2);
        end = StringCopy(end, sText_Space);
        end = StringCopy(end, sText_DexNo);
        end = ConvertIntToDecimalStringN(end, sEd->slot + 1, STR_CONV_MODE_LEFT_ALIGN, 2);
        StringCopy(end, sText_RR);
    }
    AddTextPrinterParameterized3(WIN_HEADER, FONT_SMALL, 4, 17, sColor_Header, TEXT_SKIP_DRAW, buffer);

    PutWindowTilemap(WIN_HEADER);
    CopyWindowToVram(WIN_HEADER, COPYWIN_FULL);
    LoadPalette(&color, BG_PLTT_ID(0), PLTT_SIZEOF(1));
}

static void DrawLeftPanel(void)
{
    u8 cell;

    FillWindowPixelBuffer(WIN_LEFT, PIXEL_FILL(COLOR_WHITE));

    AddTextPrinterParameterized3(WIN_LEFT, FONT_SMALL, 4, ROW_Y(1), sColor_Label, TEXT_SKIP_DRAW, sText_Nature);
    AddTextPrinterParameterized3(WIN_LEFT, FONT_SMALL, 4, ROW_Y(2), sColor_Label, TEXT_SKIP_DRAW, sText_Ability);
    AddTextPrinterParameterized3(WIN_LEFT, FONT_SMALL, 4, ROW_Y(3), sColor_Label, TEXT_SKIP_DRAW, sText_Item);
    AddTextPrinterParameterized3(WIN_LEFT, FONT_SMALL, 4, ROW_Y(4), sColor_Label, TEXT_SKIP_DRAW, sText_Ball);
    AddTextPrinterParameterized3(WIN_LEFT, FONT_SMALL, 4, ROW_Y(5), sColor_Label, TEXT_SKIP_DRAW, sText_HiddenPower);
    FastFillRect(WIN_LEFT, COLOR_SEPARATOR, 4, 75, PANEL_WIDTH - 8, 1);

    for (cell = CELL_SPECIES; cell <= CELL_PP4; cell++)
    {
        if (cell != sEd->cursor)
            DrawCell(cell);
    }
    if (sCells[sEd->cursor].win == WIN_LEFT)
        DrawCell(sEd->cursor);

    // Vertical divider between the panels
    FastFillRect(WIN_LEFT, COLOR_SEPARATOR, PANEL_WIDTH - 1, 2, 1, 16 * 8 - 4);

    PutWindowTilemap(WIN_LEFT);
    CopyWindowToVram(WIN_LEFT, COPYWIN_FULL);
}

static void BuildHint(u8 *dst)
{
    u8 cell = sEd->cursor;
    const u8 *verb = sText_HintEdit;
    u16 species = GetSpecies();

    if (sEd->noRoom)
    {
        StringCopy(dst, sText_HintNoRoom);
        return;
    }
    if (sEd->state == EDSTATE_NUMBER)
    {
        struct NumberSpec spec;
        GetNumberSpec(cell, &spec);
        dst = StringCopy(dst, sText_HintUpDown);
        dst = ConvertIntToDecimalStringN(dst, spec.small, STR_CONV_MODE_LEFT_ALIGN, 2);
        dst = StringCopy(dst, sText_HintLeftRight);
        dst = ConvertIntToDecimalStringN(dst, spec.large, STR_CONV_MODE_LEFT_ALIGN, 2);
        dst = StringCopy(dst, sText_HintL);
        dst = ConvertIntToDecimalStringN(dst, spec.min, STR_CONV_MODE_LEFT_ALIGN, 3);
        dst = StringCopy(dst, sText_HintR);
        ConvertIntToDecimalStringN(dst, spec.rValue, STR_CONV_MODE_LEFT_ALIGN, 3);
        return;
    }

    switch (cell)
    {
    case CELL_NICK:
        verb = sText_HintRename;
        break;
    case CELL_SPECIES:
    case CELL_NATURE:
    case CELL_BALL:
    case CELL_HPTYPE:
    case CELL_MOVE1:
    case CELL_MOVE2:
    case CELL_MOVE3:
    case CELL_MOVE4:
        verb = sText_HintChoose;
        break;
    case CELL_ITEM:
        if (ItemIsMail(GetMonData(Work(), MON_DATA_HELD_ITEM)))
        {
            StringCopy(dst, sText_HintMail);
            return;
        }
        verb = sText_HintChoose;
        break;
    case CELL_GENDER:
        if (HasFixedGender(species))
        {
            StringCopy(dst, sText_HintFixed);
            return;
        }
        verb = sText_HintChange;
        break;
    case CELL_ABILITY:
        if (!HasTwoAbilities(species))
        {
            StringCopy(dst, sText_HintFixed);
            return;
        }
        verb = sText_HintChange;
        break;
    case CELL_SHINY:
        verb = sText_HintChange;
        break;
    case CELL_PP1:
    case CELL_PP2:
    case CELL_PP3:
    case CELL_PP4:
        if (GetMonData(Work(), MON_DATA_MOVE1 + (cell - CELL_PP1)) == MOVE_NONE)
        {
            StringCopy(dst, sText_HintNoMove);
            return;
        }
        verb = sText_HintPPUp;
        break;
    case CELL_MAXIV:
        verb = sText_HintAll31;
        break;
    default:
        verb = sText_HintEdit;
        break;
    }
    dst = StringCopy(dst, sText_HintA);
    dst = StringCopy(dst, verb);
    dst = StringCopy(dst, sEd->mode == EDMODE_NEW ? sText_HintAdd : sText_HintSave);
    StringCopy(dst, sText_HintBack);
}

static void DrawHintRow(void)
{
    u8 buffer[48];

    BuildHint(buffer);
    FastFillRect(WIN_RIGHT, COLOR_WHITE, 0, 116, PANEL_WIDTH, 12);
    AddTextPrinterParameterized3(WIN_RIGHT, FONT_SMALL, 4, 116, sColor_Label, TEXT_SKIP_DRAW, buffer);
}

// Stat value for display row r (the IV/EV cells are separate)
static void DrawStatValue(u8 r)
{
    u8 buffer[8];

    ConvertIntToDecimalStringN(buffer, GetMonData(Work(), MON_DATA_MAX_HP + sStatDisplayOrder[r]), STR_CONV_MODE_LEFT_ALIGN, 3);
    FastFillRect(WIN_RIGHT, COLOR_WHITE, 22, ROW_Y(r + 1), 29, 13);
    PrintRightAligned(WIN_RIGHT, 48, ROW_Y(r + 1), sColor_Normal, buffer);
}

static void DrawEVTotal(void)
{
    static const u8 sColor_Full[] = {COLOR_WHITE, COLOR_RED, COLOR_LIGHT_RED};
    u8 buffer[16];
    u16 evTotal = GetEVTotal();

    ConvertIntToDecimalStringN(StringCopy(buffer, sText_EVTotal), evTotal, STR_CONV_MODE_LEFT_ALIGN, 3);
    FastFillRect(WIN_RIGHT, COLOR_WHITE, 54, 88, 54, 13);
    PrintRightAligned(WIN_RIGHT, 104, 88, evTotal >= MAX_TOTAL_EVS ? sColor_Full : sColor_Label, buffer);
}

// Stat label for display row r: nature-boosted stat in red, lowered in blue
static void DrawStatName(u8 r)
{
    u8 stat = sStatDisplayOrder[r];
    u8 fg = COLOR_DARK_GRAY, shadow = COLOR_LIGHT_GRAY;

    if (stat != STAT_HP)
    {
        s8 effect = gNatureStatTable[GetEditorNature()][stat - 1];
        if (effect > 0)
        {
            fg = COLOR_RED;
            shadow = COLOR_LIGHT_RED;
        }
        else if (effect < 0)
        {
            fg = COLOR_BLUE;
            shadow = COLOR_LIGHT_BLUE;
        }
    }
    FastFillRect(WIN_RIGHT, COLOR_WHITE, 0, ROW_Y(r + 1), 22, 13);
    PrintAt(WIN_RIGHT, FONT_SMALL, 4, ROW_Y(r + 1), COLOR_WHITE, fg, shadow, sStatNames[stat]);
}

static void ShowRightPanel(void)
{
    PutWindowTilemap(WIN_RIGHT);
    CopyWindowToVram(WIN_RIGHT, COPYWIN_FULL);
}

static void DrawRightPanel(void)
{
    u8 i;

    FillWindowPixelBuffer(WIN_RIGHT, PIXEL_FILL(COLOR_WHITE));

    PrintRightAligned(WIN_RIGHT, 48, ROW_Y(0), sColor_Label, sText_ColStat);
    PrintRightAligned(WIN_RIGHT, 74, ROW_Y(0), sColor_Label, sText_ColIV);
    PrintRightAligned(WIN_RIGHT, 104, ROW_Y(0), sColor_Label, sText_ColEV);

    for (i = 0; i < NUM_STATS; i++)
    {
        DrawStatName(i);
        DrawStatValue(i);
        DrawCell(CELL_IV1 + i);
        DrawCell(CELL_EV1 + i);
    }
    DrawCell(CELL_MAXIV);
    DrawEVTotal();
    FastFillRect(WIN_RIGHT, COLOR_SEPARATOR, 4, 101, PANEL_WIDTH - 8, 1);
    DrawCell(CELL_SHINY);
    DrawCell(CELL_FRIEND);
    if (sCells[sEd->cursor].win == WIN_RIGHT)
        DrawCell(sEd->cursor);
    DrawHintRow();
    ShowRightPanel();
}

// Cursor moved from one cell to another
static void RedrawCursorMove(u8 old)
{
    u8 next = sEd->cursor;

    DrawCell(old);
    DrawHintRow();
    DrawCell(next);
    CopyGfx(sCells[old].win);
    if (sCells[next].win != sCells[old].win)
        CopyGfx(sCells[next].win);
    if (sCells[old].win != WIN_RIGHT && sCells[next].win != WIN_RIGHT)
        CopyGfx(WIN_RIGHT);
}

// The selected cell changed look (number editing started/ended) or value
static void RedrawNumberCell(void)
{
    u8 cell = sEd->cursor;
    u8 i;

    if (cell == CELL_LEVEL)
    {
        for (i = 0; i < NUM_STATS; i++)
            DrawStatValue(i);
        if (sEd->mode == EDMODE_NEW && !sEd->movesTouched)
        {
            for (i = 0; i < MAX_MON_MOVES; i++)
            {
                DrawCell(CELL_MOVE1 + i);
                DrawCell(CELL_PP1 + i);
            }
        }
    }
    else if (cell >= CELL_IV1 && cell <= CELL_IV6)
    {
        DrawStatValue(cell - CELL_IV1);
        DrawCell(CELL_HPTYPE);
    }
    else if (cell >= CELL_EV1 && cell <= CELL_EV6)
    {
        DrawStatValue(cell - CELL_EV1);
        DrawEVTotal();
    }
    DrawCell(cell);
    DrawHintRow();
    if (sCells[cell].win == WIN_LEFT || (cell >= CELL_IV1 && cell <= CELL_IV6))
        CopyGfx(WIN_LEFT);
    CopyGfx(WIN_RIGHT);
}

// Picker ------------------------------------------------------------------

static void DrawPickerCount(void)
{
    u8 buffer[24];
    u8 *end;

    FastFillRect(WIN_PICKER, COLOR_WHITE, 60, 0, PANEL_WIDTH - 60, 13);
    if (sEd->pickKind == PICK_MOVE)
    {
        PrintRightAligned(WIN_PICKER, 116, 0, sColor_Label, sEd->pickShowAll ? sText_HintSelectLearn : sText_HintSelectAll);
    }
    else
    {
        end = ConvertIntToDecimalStringN(buffer, sEd->pickCursor + 1, STR_CONV_MODE_LEFT_ALIGN, 3);
        end = StringCopy(end, sText_Slash);
        ConvertIntToDecimalStringN(end, sEd->pickCount, STR_CONV_MODE_LEFT_ALIGN, 3);
        PrintRightAligned(WIN_PICKER, 116, 0, sColor_Label, buffer);
    }
}

static void DrawPickerRow(u8 row)
{
    u8 buffer[24];
    u8 *end;
    u16 index = sEd->pickTop + row;
    u16 id;
    u8 y = PICKER_ROW_Y(row);
    u8 bg, fg = COLOR_DARK_GRAY, shadow = COLOR_LIGHT_GRAY;
    u8 color[3];

    if (index >= sEd->pickCount)
    {
        FastFillRect(WIN_PICKER, COLOR_WHITE, 2, y, PANEL_WIDTH - 4, 13);
        return;
    }
    id = sEd->pickIds[index];
    bg = (index == sEd->pickCursor) ? COLOR_CURSOR : COLOR_WHITE;
    FastFillRect(WIN_PICKER, bg, 2, y, PANEL_WIDTH - 4, 13);

    if (id == GetCurrentPickValue())
    {
        fg = COLOR_GREEN;
        shadow = COLOR_LIGHT_GREEN;
    }
    else if (sEd->pickKind == PICK_MOVE && id != MOVE_NONE)
    {
        u8 j;
        for (j = 0; j < MAX_MON_MOVES; j++)
        {
            if (GetMonData(Work(), MON_DATA_MOVE1 + j) == id)
            {
                fg = COLOR_BLUE; // known in another slot: picking it swaps the two
                shadow = COLOR_LIGHT_BLUE;
            }
        }
        if (fg == COLOR_DARK_GRAY && !IsLearnable(id))
        {
            // Not learnable by this species: faded (no shadow)
            fg = COLOR_MID_GRAY;
            shadow = COLOR_WHITE;
        }
    }
    PrintAt(WIN_PICKER, FONT_SMALL, 6, y, bg, fg, shadow, GetPickName(sEd->pickKind, id));

    color[0] = bg;
    color[1] = COLOR_MID_GRAY;
    color[2] = COLOR_LIGHT_GRAY;
    switch (sEd->pickKind)
    {
    case PICK_SPECIES:
        end = StringCopy(buffer, sText_DexNo);
        ConvertIntToDecimalStringN(end, SpeciesToNationalPokedexNum(id), STR_CONV_MODE_LEADING_ZEROS, 3);
        PrintRightAligned(WIN_PICKER, 116, y, color, buffer);
        break;
    case PICK_NATURE:
    {
        s8 up = -1, down = -1;
        u8 s;
        for (s = 0; s < NUM_NATURE_STATS; s++)
        {
            if (gNatureStatTable[id][s] > 0)
                up = s + 1;
            else if (gNatureStatTable[id][s] < 0)
                down = s + 1;
        }
        if (up > 0)
        {
            end = StringCopy(buffer, sText_Plus);
            end = StringCopy(end, sStatNames[up]);
            end = StringCopy(end, sText_Space);
            end = StringCopy(end, sText_Minus);
            StringCopy(end, sStatNames[down]);
            PrintRightAligned(WIN_PICKER, 116, y, color, buffer);
        }
        break;
    }
    case PICK_MOVE:
        if (id != MOVE_NONE)
        {
            PrintRightAligned(WIN_PICKER, 96, y, color, gTypeNames[gBattleMoves[id].type]);
            if (gBattleMoves[id].power > 1)
                ConvertIntToDecimalStringN(buffer, gBattleMoves[id].power, STR_CONV_MODE_LEFT_ALIGN, 3);
            else
                StringCopy(buffer, sText_None);
            PrintRightAligned(WIN_PICKER, 116, y, color, buffer);
        }
        break;
    case PICK_HPTYPE:
        ConvertIntToDecimalStringN(buffer, GetHiddenPowerPower(), STR_CONV_MODE_LEFT_ALIGN, 2);
        PrintRightAligned(WIN_PICKER, 116, y, color, buffer);
        break;
    }
}

static void DrawPicker(void)
{
    const u8 *title;
    u8 i;

    FillWindowPixelBuffer(WIN_PICKER, PIXEL_FILL(COLOR_WHITE));

    switch (sEd->pickKind)
    {
    case PICK_SPECIES:
        title = sText_PickSpecies;
        break;
    case PICK_NATURE:
        title = sText_PickNature;
        break;
    case PICK_ITEM:
        title = sText_PickItem;
        break;
    case PICK_BALL:
        title = sText_PickBall;
        break;
    case PICK_HPTYPE:
        title = sText_PickHPType;
        break;
    case PICK_MOVE:
    default:
        title = sEd->pickShowAll ? sText_PickAllMoves : sText_PickLearnable;
        break;
    }
    AddTextPrinterParameterized3(WIN_PICKER, FONT_SMALL, 4, 0, sColor_Label, TEXT_SKIP_DRAW, title);
    DrawPickerCount();
    FastFillRect(WIN_PICKER, COLOR_SEPARATOR, 4, 13, PANEL_WIDTH - 8, 1);

    for (i = 0; i < PICKER_ROWS; i++)
    {
        if (sEd->pickTop + i != sEd->pickCursor)
            DrawPickerRow(i);
    }
    if (sEd->pickCursor >= sEd->pickTop && sEd->pickCursor < sEd->pickTop + PICKER_ROWS)
        DrawPickerRow(sEd->pickCursor - sEd->pickTop);

    FastFillRect(WIN_PICKER, COLOR_SEPARATOR, 4, 112, PANEL_WIDTH - 8, 1);
    AddTextPrinterParameterized3(WIN_PICKER, FONT_SMALL, 4, 115, sColor_Label, TEXT_SKIP_DRAW,
                                 sEd->pickCount > PICKER_ROWS * 2 ? sText_HintPicker : sText_HintPickerShort);

    PutWindowTilemap(WIN_PICKER);
    CopyWindowToVram(WIN_PICKER, COPYWIN_FULL);
}

#define PICKER_TOP_Y    PICKER_ROW_Y(0)
#define PICKER_BOTTOM_Y (PICKER_ROW_Y(PICKER_ROWS - 1) + 12)

// Shifts the list's pixels one row up (dir 1) or down (dir -1), so a
// one-row scroll only has to draw the row that comes into view.
static void ScrollPickerPixels(s8 dir)
{
    u32 *tiles = (u32 *)GetWindowAttribute(WIN_PICKER, WINDOW_TILE_DATA);
    u8 width = GetWindowAttribute(WIN_PICKER, WINDOW_WIDTH);
    u16 y, tx;

#define PIXEL_ROW(py, t) (tiles[(((py) / 8) * width + (t)) * 8 + ((py) % 8)])
    if (dir > 0)
    {
        for (y = PICKER_TOP_Y; y + 12 <= PICKER_BOTTOM_Y; y++)
        {
            for (tx = 0; tx < width; tx++)
                PIXEL_ROW(y, tx) = PIXEL_ROW(y + 12, tx);
        }
    }
    else
    {
        for (y = PICKER_BOTTOM_Y; y >= PICKER_TOP_Y + 12; y--)
        {
            for (tx = 0; tx < width; tx++)
                PIXEL_ROW(y, tx) = PIXEL_ROW(y - 12, tx);
        }
    }
#undef PIXEL_ROW
}

static void RedrawPickerMove(u16 oldCursor, u16 oldTop)
{
    s32 shift = (s32)sEd->pickTop - (s32)oldTop;

    if (shift == 1 || shift == -1)
    {
        u8 exposed = (shift > 0) ? PICKER_ROWS - 1 : 0;
        ScrollPickerPixels(shift);
        if (oldCursor >= sEd->pickTop && oldCursor < sEd->pickTop + PICKER_ROWS)
            DrawPickerRow(oldCursor - sEd->pickTop);
        if (sEd->pickTop + exposed != sEd->pickCursor)
            DrawPickerRow(exposed);
        DrawPickerRow(sEd->pickCursor - sEd->pickTop);
    }
    else if (shift != 0)
    {
        DrawPicker();
        return;
    }
    else if (oldCursor != sEd->pickCursor)
    {
        DrawPickerRow(oldCursor - sEd->pickTop);
        DrawPickerRow(sEd->pickCursor - sEd->pickTop);
    }
    if (sEd->pickKind != PICK_MOVE)
        DrawPickerCount();
    CopyGfx(WIN_PICKER);
}

// Prompt ------------------------------------------------------------------

static bool8 IsPartyFull(void)
{
    return CalculatePlayerPartyCount() >= PARTY_SIZE;
}

static void DrawPrompt(void)
{
    static const u8 sColor_Title[] = {COLOR_WHITE, COLOR_DARK_GRAY, COLOR_LIGHT_GRAY};
    u8 y = 30;

    FillWindowPixelBuffer(WIN_PICKER, PIXEL_FILL(COLOR_WHITE));
    if (sEd->mode == EDMODE_NEW)
    {
        AddTextPrinterParameterized3(WIN_PICKER, FONT_NORMAL, 6, 8, sColor_Title, TEXT_SKIP_DRAW, sText_PromptAdd);
        if (IsPartyFull())
        {
            AddTextPrinterParameterized3(WIN_PICKER, FONT_SMALL, 6, 24, sColor_Label, TEXT_SKIP_DRAW, sText_PromptPartyFull);
            y = 42;
            AddTextPrinterParameterized3(WIN_PICKER, FONT_SMALL, 6, y, sColor_Normal, TEXT_SKIP_DRAW, sText_PromptOptAddPC);
        }
        else
        {
            AddTextPrinterParameterized3(WIN_PICKER, FONT_SMALL, 6, y, sColor_Normal, TEXT_SKIP_DRAW, sText_PromptOptAddParty);
        }
    }
    else
    {
        AddTextPrinterParameterized3(WIN_PICKER, FONT_NORMAL, 6, 8, sColor_Title, TEXT_SKIP_DRAW, sText_PromptSave);
        AddTextPrinterParameterized3(WIN_PICKER, FONT_SMALL, 6, y, sColor_Normal, TEXT_SKIP_DRAW, sText_PromptOptSave);
    }
    AddTextPrinterParameterized3(WIN_PICKER, FONT_SMALL, 6, y + 16, sColor_Normal, TEXT_SKIP_DRAW, sText_PromptOptDiscard);
    AddTextPrinterParameterized3(WIN_PICKER, FONT_SMALL, 6, y + 32, sColor_Normal, TEXT_SKIP_DRAW, sText_PromptOptBack);

    PutWindowTilemap(WIN_PICKER);
    CopyWindowToVram(WIN_PICKER, COPYWIN_FULL);
}

// The right side after a state change: a list or prompt goes in WIN_PICKER
// on top; WIN_RIGHT is kept up to date underneath, so closing one just
// shows it again.
static void DrawRightSide(void)
{
    if (sEd->state == EDSTATE_PICKER)
        DrawPicker();
    else if (sEd->state == EDSTATE_PROMPT)
        DrawPrompt();
    else
        ShowRightPanel();
}

static void DrawAll(void)
{
    DrawHeader();
    DrawLeftPanel();
    DrawRightPanel();
    if (sEd->state == EDSTATE_PICKER || sEd->state == EDSTATE_PROMPT)
        DrawRightSide();
}

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------

static void UpdateIconSelection(void)
{
    u8 i;

    for (i = 0; i < sEd->iconCount; i++)
    {
        struct Sprite *sprite;
        if (sEd->iconSpriteIds[i] >= MAX_SPRITES)
            continue;
        sprite = &gSprites[sEd->iconSpriteIds[i]];
        if (sEd->mode != EDMODE_PARTY || sEd->iconSlots[i] == sEd->slot)
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

static void CreateIcons(void)
{
    u8 i;

    sEd->iconCount = 0;
    if (sEd->mode == EDMODE_PARTY)
    {
        for (i = 0; i < PARTY_SIZE; i++)
        {
            struct Pokemon *mon = (i == sEd->slot) ? Work() : &gPlayerParty[i];
            u16 species = GetMonData(mon, MON_DATA_SPECIES_OR_EGG);
            u8 spriteId;
            if (species == SPECIES_NONE)
                continue;
            spriteId = CreateMonIcon(species, SpriteCB_MonIcon, ICON_X_START + i * ICON_X_SPACING, ICON_Y, 1,
                                     GetMonData(mon, MON_DATA_PERSONALITY), TRUE);
            if (spriteId < MAX_SPRITES)
                gSprites[spriteId].oam.priority = 0;
            sEd->iconSpriteIds[sEd->iconCount] = spriteId;
            sEd->iconSlots[sEd->iconCount] = i;
            sEd->iconCount++;
        }
    }
    else
    {
        u8 spriteId = CreateMonIcon(GetSpecies(), SpriteCB_MonIcon, ICON_X_SINGLE, ICON_Y, 1,
                                    GetMonData(Work(), MON_DATA_PERSONALITY), TRUE);
        if (spriteId < MAX_SPRITES)
            gSprites[spriteId].oam.priority = 0;
        sEd->iconSpriteIds[0] = spriteId;
        sEd->iconSlots[0] = 0;
        sEd->iconCount = 1;
    }
    UpdateIconSelection();
}

static void DestroyIcons(void)
{
    u8 i;

    for (i = 0; i < sEd->iconCount; i++)
    {
        if (sEd->iconSpriteIds[i] < MAX_SPRITES)
            FreeAndDestroyMonIconSprite(&gSprites[sEd->iconSpriteIds[i]]);
        sEd->iconSpriteIds[i] = MAX_SPRITES;
    }
    sEd->iconCount = 0;
}

static void RefreshIcons(void)
{
    DestroyIcons();
    CreateIcons();
}

// ---------------------------------------------------------------------------
// Screen setup / main loop
// ---------------------------------------------------------------------------

static void CB2_EditorSetup(void)
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
        SetBgTilemapBuffer(0, sEd->tilemap);
        ResetAllBgsCoordinates();
        if (!InitWindows(sWindowTemplates))
        {
            sEd->failed = TRUE;
            CreateTask(Task_EditorExit, 0);
            SetMainCallback2(CB2_Editor);
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
        pal[COLOR_CURSOR]       = RGB(31, 29, 17);
        pal[COLOR_EDITING]      = RGB(20, 28, 31);
        LoadPalette(pal, BG_PLTT_ID(15), PLTT_SIZE_4BPP);
        LoadMonIconPalettes();
        gMain.state++;
        break;
    }
    case 5:
        CreateIcons();
        DrawAll();
        gMain.state++;
        break;
    default:
        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP);
        SetGpuReg(REG_OFFSET_WININ, 0);
        SetGpuReg(REG_OFFSET_WINOUT, 0);
        SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_TGT2_BG0 | BLDCNT_TGT2_BD);
        SetGpuReg(REG_OFFSET_BLDALPHA, BLDALPHA_BLEND(7, 9));
        SetGpuReg(REG_OFFSET_BLDY, 0);
        ShowBg(0);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        SetVBlankCallback(VBlankCB_Editor);
        CreateTask(Task_EditorInput, 0);
        SetMainCallback2(CB2_Editor);
        break;
    }
}

static void CB2_Editor(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    DoScheduledBgTilemapCopiesToVram();
    UpdatePaletteFade();
}

static void VBlankCB_Editor(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

// Tear down everything but sEd (the naming screen comes back to us)
static void TeardownScreen(void)
{
    DestroyIcons();
    FreeMonIconPalettes();
    FreeAllWindowBuffers();
    UnsetBgTilemapBuffer(0);
    SetGpuReg(REG_OFFSET_BLDCNT, 0);
    SetGpuReg(REG_OFFSET_BLDALPHA, 0);
}

// ---------------------------------------------------------------------------
// Saving and switching
// ---------------------------------------------------------------------------

// FALSE only for a new Pokemon when the party and the PC are both full
static bool8 SaveWork(void)
{
    switch (sEd->mode)
    {
    case EDMODE_PARTY:
        gPlayerParty[sEd->slot] = sEd->work;
        break;
    case EDMODE_BOX:
        *GetBoxedMonPtr(sEd->boxId, sEd->slot) = sEd->work.box;
        break;
    case EDMODE_NEW:
    {
        u32 metLevel = GetMonData(&sEd->work, MON_DATA_LEVEL);
        SetMonData(&sEd->work, MON_DATA_MET_LEVEL, &metLevel);
        if (GiveMonToPlayer(&sEd->work) == MON_CANT_GIVE)
            return FALSE;
        break;
    }
    }
    sEd->orig = sEd->work;
    return TRUE;
}

static void ShowNoRoom(void)
{
    PlaySE(SE_FAILURE);
    sEd->noRoom = TRUE;
    DrawHintRow();
    CopyGfx(WIN_RIGHT);
}

static bool8 IsEditableBoxMon(struct BoxPokemon *boxMon)
{
    return GetBoxMonData(boxMon, MON_DATA_SPECIES) != SPECIES_NONE
        && !GetBoxMonData(boxMon, MON_DATA_IS_EGG);
}

// Moves to the previous/next Pokemon that isn't an Egg. FALSE if there's none.
static bool8 FindNeighbor(bool8 forward, u8 *outBox, u8 *outSlot)
{
    s16 box = sEd->boxId, slot = sEd->slot;
    u16 tries;

    if (sEd->mode == EDMODE_PARTY)
    {
        u8 count = CalculatePlayerPartyCount();
        for (tries = 0; tries < PARTY_SIZE; tries++)
        {
            slot += forward ? 1 : -1;
            if (slot < 0)
                slot = count - 1;
            if (slot >= count)
                slot = 0;
            if (slot == sEd->slot)
                return FALSE;
            if (!GetMonData(&gPlayerParty[slot], MON_DATA_IS_EGG)
             && GetMonData(&gPlayerParty[slot], MON_DATA_SPECIES) != SPECIES_NONE)
            {
                *outBox = 0;
                *outSlot = slot;
                return TRUE;
            }
        }
        return FALSE;
    }

    for (tries = 0; tries < TOTAL_BOXES_COUNT * IN_BOX_COUNT; tries++)
    {
        slot += forward ? 1 : -1;
        if (slot < 0)
        {
            slot = IN_BOX_COUNT - 1;
            if (sEd->allBoxes)
                box = (box == 0) ? TOTAL_BOXES_COUNT - 1 : box - 1;
        }
        else if (slot >= IN_BOX_COUNT)
        {
            slot = 0;
            if (sEd->allBoxes)
                box = (box + 1) % TOTAL_BOXES_COUNT;
        }
        if (box == sEd->boxId && slot == sEd->slot)
            return FALSE;
        if (IsEditableBoxMon(GetBoxedMonPtr(box, slot)))
        {
            *outBox = box;
            *outSlot = slot;
            return TRUE;
        }
    }
    return FALSE;
}

static bool8 SwitchMon(bool8 forward)
{
    u8 box, slot;

    if (sEd->mode == EDMODE_NEW || !FindNeighbor(forward, &box, &slot))
    {
        PlaySE(SE_FAILURE);
        return FALSE;
    }
    PlaySE(SE_SELECT);
    sEd->boxId = box;
    sEd->slot = slot;
    LoadCurrentMon();
    RefreshIcons();
    DrawAll();
    return TRUE;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

// The editor uses L and R itself, so read A/L/R raw (no L=A remap)
#define NEW_RAW(keys) (gMain.newKeysRaw & (keys))

static void ActivateCell(u8 taskId)
{
    u8 cell = sEd->cursor;
    u16 species = GetSpecies();
    struct Pokemon *mon = Work();

    if (IsNumberCell(cell))
    {
        PlaySE(SE_SELECT);
        sEd->numberOrig = GetCellNumber(cell);
        sEd->state = EDSTATE_NUMBER;
        RedrawNumberCell();
        return;
    }

    switch (cell)
    {
    case CELL_NICK:
        PlaySE(SE_SELECT);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
        gTasks[taskId].func = Task_EditorGoToNaming;
        return;
    case CELL_SPECIES:
        PlaySE(SE_SELECT);
        OpenPicker(PICK_SPECIES, 0);
        DrawRightSide();
        return;
    case CELL_NATURE:
        PlaySE(SE_SELECT);
        OpenPicker(PICK_NATURE, 0);
        DrawRightSide();
        return;
    case CELL_ITEM:
        if (ItemIsMail(GetMonData(mon, MON_DATA_HELD_ITEM)))
        {
            PlaySE(SE_FAILURE);
            return;
        }
        PlaySE(SE_SELECT);
        OpenPicker(PICK_ITEM, 0);
        DrawRightSide();
        return;
    case CELL_BALL:
        PlaySE(SE_SELECT);
        OpenPicker(PICK_BALL, 0);
        DrawRightSide();
        return;
    case CELL_HPTYPE:
        PlaySE(SE_SELECT);
        OpenPicker(PICK_HPTYPE, 0);
        DrawRightSide();
        return;
    case CELL_MOVE1:
    case CELL_MOVE2:
    case CELL_MOVE3:
    case CELL_MOVE4:
        PlaySE(SE_SELECT);
        OpenPicker(PICK_MOVE, cell - CELL_MOVE1);
        DrawRightSide();
        return;
    case CELL_GENDER:
        if (HasFixedGender(species))
        {
            PlaySE(SE_FAILURE);
            return;
        }
        PlaySE(SE_SELECT);
        SetPersonalityTraits(GetEditorNature(), GetMonGender(mon) == MON_MALE ? MON_FEMALE : MON_MALE, IsMonShiny(mon));
        RefreshIcons();
        DrawCell(cell);
        CopyGfx(WIN_LEFT);
        return;
    case CELL_ABILITY:
    {
        u32 num = GetMonData(mon, MON_DATA_ABILITY_NUM) ^ 1;
        if (!HasTwoAbilities(species))
        {
            PlaySE(SE_FAILURE);
            return;
        }
        PlaySE(SE_SELECT);
        SetMonData(mon, MON_DATA_ABILITY_NUM, &num);
        DrawCell(cell);
        CopyGfx(WIN_LEFT);
        return;
    }
    case CELL_SHINY:
        PlaySE(SE_SELECT);
        SetPersonalityTraits(GetEditorNature(), GetMonGender(mon), !IsMonShiny(mon));
        RefreshIcons();
        DrawCell(cell);
        CopyGfx(WIN_RIGHT);
        return;
    case CELL_PP1:
    case CELL_PP2:
    case CELL_PP3:
    case CELL_PP4:
        if (GetMonData(mon, MON_DATA_MOVE1 + (cell - CELL_PP1)) == MOVE_NONE)
        {
            PlaySE(SE_FAILURE);
            return;
        }
        PlaySE(SE_SELECT);
        CyclePPUps(cell - CELL_PP1);
        DrawCell(cell);
        CopyGfx(WIN_LEFT);
        return;
    case CELL_MAXIV:
    {
        u32 iv = MAX_PER_STAT_IVS;
        u8 i;
        PlaySE(SE_SELECT);
        for (i = 0; i < NUM_STATS; i++)
            SetMonData(mon, MON_DATA_HP_IV + i, &iv);
        RecalcStats();
        DrawCell(CELL_HPTYPE);
        CopyGfx(WIN_LEFT);
        for (i = 0; i < NUM_STATS; i++)
        {
            DrawStatValue(i);
            DrawCell(CELL_IV1 + i);
        }
        DrawCell(cell);
        CopyGfx(WIN_RIGHT);
        return;
    }
    }
}

static void OpenPrompt(u8 action)
{
    PlaySE(SE_SELECT);
    sEd->promptAction = action;
    sEd->state = EDSTATE_PROMPT;
    // Hide the cursor while the prompt is up
    DrawCell(sEd->cursor);
    if (sCells[sEd->cursor].win != WIN_RIGHT)
        CopyGfx(sCells[sEd->cursor].win);
    DrawPrompt();
}

static void HandleNavInput(u8 taskId)
{
    u8 next = NO_CELL;

    if (JOY_REPEAT(DPAD_UP))
        next = sCells[sEd->cursor].up;
    else if (JOY_REPEAT(DPAD_DOWN))
        next = sCells[sEd->cursor].down;
    else if (JOY_REPEAT(DPAD_LEFT))
        next = sCells[sEd->cursor].left;
    else if (JOY_REPEAT(DPAD_RIGHT))
        next = sCells[sEd->cursor].right;

    if (next != NO_CELL)
    {
        u8 old = sEd->cursor;
        sEd->noRoom = FALSE;
        PlaySE(SE_SELECT);
        sEd->cursor = next;
        RedrawCursorMove(old);
        return;
    }

    if (NEW_RAW(L_BUTTON | R_BUTTON))
    {
        bool8 forward = NEW_RAW(R_BUTTON) ? TRUE : FALSE;
        u8 box, slot;
        if (sEd->mode == EDMODE_NEW || !FindNeighbor(forward, &box, &slot))
            PlaySE(SE_FAILURE);
        else if (IsDirty())
            OpenPrompt(forward ? PROMPT_NEXT : PROMPT_PREV);
        else
            SwitchMon(forward);
    }
    else if (NEW_RAW(A_BUTTON))
    {
        ActivateCell(taskId);
    }
    else if (NEW_RAW(START_BUTTON))
    {
        if (IsDirty() && !SaveWork())
        {
            ShowNoRoom();
            return;
        }
        PlaySE(SE_SELECT);
        StartExit(taskId);
    }
    else if (NEW_RAW(B_BUTTON))
    {
        if (IsDirty())
            OpenPrompt(PROMPT_EXIT);
        else
        {
            PlaySE(SE_SELECT);
            StartExit(taskId);
        }
    }
}

static void HandleNumberInput(void)
{
    u8 cell = sEd->cursor;
    struct NumberSpec spec;

    s16 delta = 0;
    bool8 adjust = TRUE;

    GetNumberSpec(cell, &spec);
    if (NEW_RAW(A_BUTTON))
    {
        PlaySE(SE_SELECT);
        sEd->state = EDSTATE_NAV;
        RedrawNumberCell();
        return;
    }
    else if (NEW_RAW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        if (GetCellNumber(cell) != sEd->numberOrig)
            SetCellNumber(cell, sEd->numberOrig);
        sEd->state = EDSTATE_NAV;
        RedrawNumberCell();
        return;
    }
    else if (NEW_RAW(L_BUTTON))
        delta = (s16)spec.min - (s16)GetCellNumber(cell);
    else if (NEW_RAW(R_BUTTON))
        delta = (s16)spec.rValue - (s16)GetCellNumber(cell);
    else if (JOY_REPEAT(DPAD_UP))
        delta = spec.small;
    else if (JOY_REPEAT(DPAD_DOWN))
        delta = -spec.small;
    else if (JOY_REPEAT(DPAD_RIGHT))
        delta = spec.large;
    else if (JOY_REPEAT(DPAD_LEFT))
        delta = -spec.large;
    else
        adjust = FALSE;

    if (adjust && AdjustNumber(cell, delta))
        RedrawNumberCell();
}

static void HandlePickerInput(void)
{
    u16 oldCursor = sEd->pickCursor, oldTop = sEd->pickTop;

    if (NEW_RAW(A_BUTTON))
    {
        u8 kind = sEd->pickKind;
        u8 i;
        PlaySE(SE_SELECT);
        ApplyPick();
        ClosePicker();
        switch (kind)
        {
        case PICK_SPECIES:
            RefreshIcons();
            DrawAll();
            return;
        case PICK_NATURE:
            DrawCell(CELL_NATURE);
            for (i = 0; i < NUM_STATS; i++)
            {
                DrawStatName(i);
                DrawStatValue(i);
            }
            break;
        case PICK_ITEM:
            DrawCell(CELL_ITEM);
            break;
        case PICK_BALL:
            DrawCell(CELL_BALL);
            break;
        case PICK_HPTYPE:
            DrawCell(CELL_HPTYPE);
            for (i = 0; i < NUM_STATS; i++)
            {
                DrawStatValue(i);
                DrawCell(CELL_IV1 + i);
            }
            break;
        case PICK_MOVE:
            // Moves may have been swapped or packed; the cursor stays where it was
            for (i = 0; i < MAX_MON_MOVES; i++)
            {
                if (CELL_MOVE1 + i != sEd->cursor)
                    DrawCell(CELL_MOVE1 + i);
                DrawCell(CELL_PP1 + i);
            }
            DrawCell(sEd->cursor);
            break;
        }
        CopyGfx(WIN_LEFT);
        ShowRightPanel();
    }
    else if (NEW_RAW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        ClosePicker();
        ShowRightPanel();
    }
    else if (NEW_RAW(SELECT_BUTTON) && sEd->pickKind == PICK_MOVE)
    {
        u16 id = sEd->pickIds[sEd->pickCursor];
        PlaySE(SE_SELECT);
        sEd->pickShowAll ^= 1;
        BuildPickList();
        SetPickCursorTo(id);
        DrawPicker();
    }
    else if (NEW_RAW(L_BUTTON | R_BUTTON))
    {
        PlaySE(SE_SELECT);
        if (sEd->pickCount > PICKER_ROWS * 2)
            JumpPickLetter(NEW_RAW(R_BUTTON) ? TRUE : FALSE);
        else
            MovePickCursor(NEW_RAW(R_BUTTON) ? PICKER_ROWS : -PICKER_ROWS);
        RedrawPickerMove(oldCursor, oldTop);
    }
    else if (JOY_REPEAT(DPAD_UP))
    {
        PlaySE(SE_SELECT);
        MovePickCursor(-1);
        RedrawPickerMove(oldCursor, oldTop);
    }
    else if (JOY_REPEAT(DPAD_DOWN))
    {
        PlaySE(SE_SELECT);
        MovePickCursor(1);
        RedrawPickerMove(oldCursor, oldTop);
    }
    else if (JOY_REPEAT(DPAD_LEFT))
    {
        PlaySE(SE_SELECT);
        MovePickCursor(-PICKER_ROWS);
        RedrawPickerMove(oldCursor, oldTop);
    }
    else if (JOY_REPEAT(DPAD_RIGHT))
    {
        PlaySE(SE_SELECT);
        MovePickCursor(PICKER_ROWS);
        RedrawPickerMove(oldCursor, oldTop);
    }
}

static void FinishPrompt(u8 taskId)
{
    sEd->state = EDSTATE_NAV;
    switch (sEd->promptAction)
    {
    case PROMPT_EXIT:
        StartExit(taskId);
        break;
    case PROMPT_PREV:
    case PROMPT_NEXT:
        if (!SwitchMon(sEd->promptAction == PROMPT_NEXT))
            DrawAll();
        break;
    }
}

static void HandlePromptInput(u8 taskId)
{
    if (NEW_RAW(A_BUTTON))
    {
        if (!SaveWork())
        {
            // Back to editing, with the reason in the hint row
            sEd->state = EDSTATE_NAV;
            DrawCell(sEd->cursor);
            CopyGfx(sCells[sEd->cursor].win);
            ShowNoRoom();
            ShowRightPanel();
            return;
        }
        PlaySE(SE_SELECT);
        FinishPrompt(taskId);
    }
    else if (NEW_RAW(SELECT_BUTTON))
    {
        PlaySE(SE_SELECT);
        if (sEd->mode != EDMODE_NEW)
            sEd->work = sEd->orig;
        FinishPrompt(taskId);
    }
    else if (NEW_RAW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        sEd->state = EDSTATE_NAV;
        // The cursor was hidden while the prompt was up
        DrawCell(sEd->cursor);
        if (sCells[sEd->cursor].win != WIN_RIGHT)
            CopyGfx(sCells[sEd->cursor].win);
        ShowRightPanel();
    }
}

static void Task_EditorInput(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    switch (sEd->state)
    {
    case EDSTATE_NAV:
        HandleNavInput(taskId);
        break;
    case EDSTATE_NUMBER:
        HandleNumberInput();
        break;
    case EDSTATE_PICKER:
        HandlePickerInput();
        break;
    case EDSTATE_PROMPT:
        HandlePromptInput(taskId);
        break;
    }
}

static void StartExit(u8 taskId)
{
    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
    gTasks[taskId].func = Task_EditorExit;
}

static void Task_EditorExit(u8 taskId)
{
    MainCallback exitCallback;

    if (gPaletteFade.active)
        return;

    gLastViewedMonIndex = sEd->slot;
    exitCallback = sEd->exitCallback;
    if (!sEd->failed)
    {
        DestroyIcons();
        FreeMonIconPalettes();
    }
    FreeAllWindowBuffers();
    UnsetBgTilemapBuffer(0);
    SetGpuReg(REG_OFFSET_BLDCNT, 0);
    SetGpuReg(REG_OFFSET_BLDALPHA, 0);
    FreeSortedLists();
    DestroyTask(taskId);
    FREE_AND_SET_NULL(sEd);
    SetMainCallback2(exitCallback);
}

// Nickname -------------------------------------------------------------------

static void Task_EditorGoToNaming(u8 taskId)
{
    struct Pokemon *mon;

    if (gPaletteFade.active)
        return;

    mon = Work();
    TeardownScreen();
    DestroyTask(taskId);
    sEd->nameBuffer[0] = EOS;
    DoNamingScreen(NAMING_SCREEN_NICKNAME, sEd->nameBuffer, GetSpecies(), GetMonGender(mon),
                   GetMonData(mon, MON_DATA_PERSONALITY), CB2_EditorReturnFromNaming);
}

static void CB2_EditorReturnFromNaming(void)
{
    // The naming screen starts blank; confirming a blank name keeps the old one
    if (sEd->nameBuffer[0] != EOS)
        SetMonData(Work(), MON_DATA_NICKNAME, sEd->nameBuffer);
    sEd->state = EDSTATE_NAV;
    gMain.state = 0;
    SetMainCallback2(CB2_EditorSetup);
}
