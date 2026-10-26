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
    // Starting items — utility
    AddBagItem(ITEM_TOWN_MAP,      1);
    AddBagItem(ITEM_POKE_BALL,    10);
    AddBagItem(ITEM_REPEL,         5);
    AddBagItem(ITEM_FULL_RESTORE,  3);
    AddBagItem(ITEM_ESCAPE_ROPE,   2);
    // Competitive held items (26 items, fills remaining ITEMS pocket slots)
    AddBagItem(ITEM_CHOICE_BAND,   1);
    AddBagItem(ITEM_LEFTOVERS,     1);
    AddBagItem(ITEM_SHELL_BELL,    1);
    AddBagItem(ITEM_SCOPE_LENS,    1);
    AddBagItem(ITEM_KINGS_ROCK,    1);
    AddBagItem(ITEM_FOCUS_BAND,    1);
    AddBagItem(ITEM_BRIGHT_POWDER, 1);
    AddBagItem(ITEM_WHITE_HERB,    1);
    AddBagItem(ITEM_QUICK_CLAW,    1);
    AddBagItem(ITEM_THICK_CLUB,    1);
    AddBagItem(ITEM_TWISTED_SPOON, 1);
    AddBagItem(ITEM_CHARCOAL,      1);
    AddBagItem(ITEM_DRAGON_FANG,   1);
    AddBagItem(ITEM_SILK_SCARF,    1);
    AddBagItem(ITEM_HARD_STONE,    1);
    AddBagItem(ITEM_MIRACLE_SEED,  1);
    AddBagItem(ITEM_BLACK_GLASSES, 1);
    AddBagItem(ITEM_BLACK_BELT,    1);
    AddBagItem(ITEM_MAGNET,        1);
    AddBagItem(ITEM_MYSTIC_WATER,  1);
    AddBagItem(ITEM_SHARP_BEAK,    1);
    AddBagItem(ITEM_POISON_BARB,   1);
    AddBagItem(ITEM_NEVER_MELT_ICE,1);
    AddBagItem(ITEM_SPELL_TAG,     1);
    AddBagItem(ITEM_SOFT_SAND,     1);
    AddBagItem(ITEM_METAL_COAT,    1);
    // Berries — all 44 (go to Berries pocket automatically, 46 slot capacity)
    AddBagItem(ITEM_CHERI_BERRY,   1);
    AddBagItem(ITEM_CHESTO_BERRY,  1);
    AddBagItem(ITEM_PECHA_BERRY,   1);
    AddBagItem(ITEM_RAWST_BERRY,   1);
    AddBagItem(ITEM_ASPEAR_BERRY,  1);
    AddBagItem(ITEM_LEPPA_BERRY,   1);
    AddBagItem(ITEM_ORAN_BERRY,    1);
    AddBagItem(ITEM_PERSIM_BERRY,  1);
    AddBagItem(ITEM_LUM_BERRY,     1);
    AddBagItem(ITEM_SITRUS_BERRY,  1);
    AddBagItem(ITEM_FIGY_BERRY,    1);
    AddBagItem(ITEM_WIKI_BERRY,    1);
    AddBagItem(ITEM_MAGO_BERRY,    1);
    AddBagItem(ITEM_AGUAV_BERRY,   1);
    AddBagItem(ITEM_IAPAPA_BERRY,  1);
    AddBagItem(ITEM_RAZZ_BERRY,    1);
    AddBagItem(ITEM_BLUK_BERRY,    1);
    AddBagItem(ITEM_NANAB_BERRY,   1);
    AddBagItem(ITEM_WEPEAR_BERRY,  1);
    AddBagItem(ITEM_PINAP_BERRY,   1);
    AddBagItem(ITEM_POMEG_BERRY,   1);
    AddBagItem(ITEM_KELPSY_BERRY,  1);
    AddBagItem(ITEM_QUALOT_BERRY,  1);
    AddBagItem(ITEM_HONDEW_BERRY,  1);
    AddBagItem(ITEM_GREPA_BERRY,   1);
    AddBagItem(ITEM_TAMATO_BERRY,  1);
    AddBagItem(ITEM_CORNN_BERRY,   1);
    AddBagItem(ITEM_MAGOST_BERRY,  1);
    AddBagItem(ITEM_RABUTA_BERRY,  1);
    AddBagItem(ITEM_NOMEL_BERRY,   1);
    AddBagItem(ITEM_SPELON_BERRY,  1);
    AddBagItem(ITEM_PAMTRE_BERRY,  1);
    AddBagItem(ITEM_WATMEL_BERRY,  1);
    AddBagItem(ITEM_DURIN_BERRY,   1);
    AddBagItem(ITEM_BELUE_BERRY,   1);
    AddBagItem(ITEM_LIECHI_BERRY,  1);
    AddBagItem(ITEM_GANLON_BERRY,  1);
    AddBagItem(ITEM_SALAC_BERRY,   1);
    AddBagItem(ITEM_PETAYA_BERRY,  1);
    AddBagItem(ITEM_APICOT_BERRY,  1);
    AddBagItem(ITEM_LANSAT_BERRY,  1);
    AddBagItem(ITEM_STARF_BERRY,   1);
    AddBagItem(ITEM_ENIGMA_BERRY,  1);
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
