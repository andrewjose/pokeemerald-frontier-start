#include "global.h"
#include "new_game.h"
#include "random.h"
#include "pokemon.h"
#include "roamer.h"
#include "pokemon_size_record.h"
#include "script.h"
#include "lottery_corner.h"
#include "play_time.h"
#include "mauville_old_man.h"
#include "match_call.h"
#include "lilycove_lady.h"
#include "load_save.h"
#include "pokeblock.h"
#include "dewford_trend.h"
#include "berry.h"
#include "rtc.h"
#include "easy_chat.h"
#include "event_data.h"
#include "money.h"
#include "trainer_hill.h"
#include "tv.h"
#include "coins.h"
#include "text.h"
#include "overworld.h"
#include "mail.h"
#include "battle_records.h"
#include "item.h"
#include "pokedex.h"
#include "apprentice.h"
#include "frontier_util.h"
#include "pokedex.h"
#include "save.h"
#include "link_rfu.h"
#include "main.h"
#include "contest.h"
#include "item_menu.h"
#include "pokemon_storage_system.h"
#include "pokemon_jump.h"
#include "decoration_inventory.h"
#include "secret_base.h"
#include "player_pc.h"
#include "field_specials.h"
#include "berry_powder.h"
#include "mystery_gift.h"
#include "union_room_chat.h"
#include "constants/items.h"
#include "constants/item.h"
#include "constants/vars.h"
#include "event_data.h"
#include "preset_parties.h"

extern const u8 EventScript_ResetAllMapFlags[];

static void ClearFrontierRecord(void);
static void WarpToTruck(void);
static void ResetMiniGamesRecords(void);

EWRAM_DATA bool8 gDifferentSaveFile = FALSE;
EWRAM_DATA bool8 gEnableContestDebugging = FALSE;

static const struct ContestWinner sContestWinnerPicDummy =
{
    .monName = _(""),
    .trainerName = _("")
};

void SetTrainerId(u32 trainerId, u8 *dst)
{
    dst[0] = trainerId;
    dst[1] = trainerId >> 8;
    dst[2] = trainerId >> 16;
    dst[3] = trainerId >> 24;
}

u32 GetTrainerId(u8 *trainerId)
{
    return (trainerId[3] << 24) | (trainerId[2] << 16) | (trainerId[1] << 8) | (trainerId[0]);
}

void CopyTrainerId(u8 *dst, u8 *src)
{
    s32 i;
    for (i = 0; i < TRAINER_ID_LENGTH; i++)
        dst[i] = src[i];
}

static void InitPlayerTrainerId(void)
{
    u32 trainerId = (Random() << 16) | GetGeneratedTrainerIdLower();
    SetTrainerId(trainerId, gSaveBlock2Ptr->playerTrainerId);
}

// L=A isnt set here for some reason.
static void SetDefaultOptions(void)
{
    gSaveBlock2Ptr->optionsTextSpeed = OPTIONS_TEXT_SPEED_FAST;
    gSaveBlock2Ptr->optionsWindowFrameType = 0;
    gSaveBlock2Ptr->optionsSound = OPTIONS_SOUND_MONO;
    gSaveBlock2Ptr->optionsBattleStyle = OPTIONS_BATTLE_STYLE_SHIFT;
    gSaveBlock2Ptr->optionsBattleSceneOff = FALSE;
    gSaveBlock2Ptr->regionMapZoom = FALSE;
}

static void ClearPokedexFlags(void)
{
    gUnusedPokedexU8 = 0;
    memset(&gSaveBlock2Ptr->pokedex.owned, 0, sizeof(gSaveBlock2Ptr->pokedex.owned));
    memset(&gSaveBlock2Ptr->pokedex.seen, 0, sizeof(gSaveBlock2Ptr->pokedex.seen));
}

void ClearAllContestWinnerPics(void)
{
    s32 i;

    ClearContestWinnerPicsInContestHall();

    // Clear Museum paintings
    for (i = MUSEUM_CONTEST_WINNERS_START; i < NUM_CONTEST_WINNERS; i++)
        gSaveBlock1Ptr->contestWinners[i] = sContestWinnerPicDummy;
}

static void ClearFrontierRecord(void)
{
    CpuFill32(0, &gSaveBlock2Ptr->frontier, sizeof(gSaveBlock2Ptr->frontier));

    gSaveBlock2Ptr->frontier.opponentNames[0][0] = EOS;
    gSaveBlock2Ptr->frontier.opponentNames[1][0] = EOS;
}

static void WarpToTruck(void)
{
    SetWarpDestination(MAP_GROUP(MAP_INSIDE_OF_TRUCK), MAP_NUM(MAP_INSIDE_OF_TRUCK), WARP_ID_NONE, -1, -1);
    WarpIntoMap();
}

void Sav2_ClearSetDefault(void)
{
    ClearSav2();
    SetDefaultOptions();
}

void ResetMenuAndMonGlobals(void)
{
    gDifferentSaveFile = FALSE;
    ResetPokedexScrollPositions();
    ZeroPlayerPartyMons();
    ZeroEnemyPartyMons();
    ResetBagScrollPositions();
    ResetPokeblockScrollPositions();
}

void NewGameInitData(void)
{
    if (gSaveFileStatus == SAVE_STATUS_EMPTY || gSaveFileStatus == SAVE_STATUS_CORRUPT)
        RtcReset();

    gDifferentSaveFile = TRUE;
    gSaveBlock2Ptr->encryptionKey = 0;
    ZeroPlayerPartyMons();
    ZeroEnemyPartyMons();
    ResetPokedex();
    ClearFrontierRecord();
    ClearSav1();
    ClearAllMail();
    gSaveBlock2Ptr->specialSaveWarpFlags = 0;
    gSaveBlock2Ptr->gcnLinkFlags = 0;
    InitPlayerTrainerId();
    PlayTimeCounter_Reset();
    ClearPokedexFlags();
    InitEventData();
    ClearTVShowData();
    ResetGabbyAndTy();
    ClearSecretBases();
    ClearBerryTrees();
    SetMoney(&gSaveBlock1Ptr->money, 3000);
    SetCoins(0);
    ResetLinkContestBoolean();
    ResetGameStats();
    ClearAllContestWinnerPics();
    ClearPlayerLinkBattleRecords();
    InitSeedotSizeRecord();
    InitLotadSizeRecord();
    gPlayerPartyCount = 0;
    ZeroPlayerPartyMons();
    ResetPokemonStorageSystem();
    ClearRoamerData();
    ClearRoamerLocationData();
    gSaveBlock1Ptr->registeredItem = ITEM_NONE;
    ClearBag();
    NewGameInitPCItems();
    ClearPokeblocks();
    ClearDecorationInventories();
    InitEasyChatPhrases();
    SetMauvilleOldMan();
    InitDewfordTrend();
    ResetFanClub();
    ResetLotteryCorner();
    // --- Battle Frontier Start Mod ---
    // Flags: bypass Birch intro, unlock Frontier, enable start menu options.
    FlagSet(FLAG_SYS_GAME_CLEAR);              // treated as post-game save
    FlagSet(FLAG_SYS_FRONTIER_PASS);           // player has the Frontier Pass
    FlagSet(FLAG_SYS_POKEMON_GET);             // enables POKEMON in the start menu
    FlagSet(FLAG_SYS_POKEDEX_GET);             // enables POKEDEX in the start menu
    FlagSet(FLAG_SYS_POKENAV_GET);             // enables POKENAV in the start menu
    FlagSet(FLAG_SYS_B_DASH);                  // enables B button running
    FlagSet(FLAG_RECEIVED_RUNNING_SHOES);      // marks running shoes as received
    FlagSet(FLAG_LANDMARK_BATTLE_FRONTIER);    // frontier registered on map
    FlagSet(FLAG_RESCUED_BIRCH);               // Birch intro done
    FlagSet(FLAG_RECEIVED_POKEDEX_FROM_BIRCH); // has Pokedex
    FlagSet(FLAG_HIDE_BATTLE_FRONTIER_RECEPTION_GATE_SCOTT); // Scott not blocking gate
    FlagSet(FLAG_HIDE_BATTLE_FRONTIER_SUDOWOODO);            // Sudowoodo not blocking
    FlagSet(FLAG_ENABLE_PROF_BIRCH_MATCH_CALL);
    EnableNationalPokedex();
    // Skip the Scott intro cutscene at the Reception Gate
    VarSet(VAR_HAS_ENTERED_BATTLE_FRONTIER, 1);

    // Curated starting items (Battle Frontier Start Mod, v1.25):
    //   - 900 of every berry
    //   - 99 of each TM (TM01..TM50)
    //   - 99 Master Balls
    //   - 99 Max Repels (Repel and Super Repel are NOT given)
    //   - 99 Rare Candy
    //   - 99 of each vitamin (HP Up, Protein, Iron, Carbos, Calcium, Zinc)
    // Everything else: nothing. PC overflow only kicks in for berries (the only
    // pocket that risks filling at 900-each).
    {
        u16 itemId;
        u16 qty;
        // Berries: 900 each
        for (itemId = 1; itemId < ITEMS_COUNT; itemId++)
        {
            if (GetItemPocket(itemId) != POCKET_BERRIES)
                continue;
            qty = 900;
            if (!AddBagItem(itemId, qty))
                AddPCItem(itemId, qty);
        }
        // TMs: 99 each (TM01..TM50). HMs are skipped.
        for (itemId = ITEM_TM01; itemId <= ITEM_TM50; itemId++)
        {
            if (!AddBagItem(itemId, 99))
                AddPCItem(itemId, 99);
        }
        // Specific items: 99 of each
        AddBagItem(ITEM_MASTER_BALL, 99);
        AddBagItem(ITEM_MAX_REPEL,   99);
        AddBagItem(ITEM_RARE_CANDY,  99);
        AddBagItem(ITEM_HP_UP,       99);
        AddBagItem(ITEM_PROTEIN,     99);
        AddBagItem(ITEM_IRON,        99);
        AddBagItem(ITEM_CARBOS,      99);
        AddBagItem(ITEM_CALCIUM,     99);
        AddBagItem(ITEM_ZINC,        99);
    }

    // Re-apply options here at the end of init so nothing in the chain overwrites them.
    // SetDefaultOptions runs early in Sav2_ClearSetDefault but agbcc's bitfield packing
    // can cause later zero-fills to clobber packed fields.
    gSaveBlock2Ptr->optionsTextSpeed        = OPTIONS_TEXT_SPEED_FAST;
    gSaveBlock2Ptr->optionsWindowFrameType  = 1;
    gSaveBlock2Ptr->optionsSound            = OPTIONS_SOUND_STEREO;
    gSaveBlock2Ptr->optionsButtonMode       = OPTIONS_BUTTON_MODE_L_EQUALS_A;
    // Warp to the Battle Frontier OutsideWest plaza.
    // x=26, y=40 places the player in the central open area between facilities.
    SetWarpDestination(MAP_GROUP(MAP_BATTLE_FRONTIER_OUTSIDE_WEST),
                       MAP_NUM(MAP_BATTLE_FRONTIER_OUTSIDE_WEST),
                       WARP_ID_NONE, 26, 40);
    WarpIntoMap();
    RunScriptImmediately(EventScript_ResetAllMapFlags);
    ResetMiniGamesRecords();
    InitUnionRoomChatRegisteredTexts();
    InitLilycoveLady();
    ResetAllApprenticeData();
    ClearRankingHallRecords();
    InitMatchCallCounters();
    ClearMysteryGift();
    WipeTrainerNameRecords();
    ResetTrainerHillResults();
    ResetContestLinkResults();
}

static void ResetMiniGamesRecords(void)
{
    CpuFill16(0, &gSaveBlock2Ptr->berryCrush, sizeof(struct BerryCrush));
    SetBerryPowder(&gSaveBlock2Ptr->berryCrush.berryPowderAmount, 0);
    ResetPokemonJumpRecords();
    CpuFill16(0, &gSaveBlock2Ptr->berryPick, sizeof(struct BerryPickingResults));
}
