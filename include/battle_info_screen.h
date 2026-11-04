#ifndef GUARD_BATTLE_INFO_SCREEN_H
#define GUARD_BATTLE_INFO_SCREEN_H

#include "main.h"

// Info screen: opened with L+R from the battle action or move menu, and from
// the Battle Factory rental/swap screens. Shows IVs, EVs, nature, stats, stat
// stages, ability, item and moves for every Pokemon on both sides.

enum {
    BATTLE_INFO_COMBO_NONE, // carry on with normal menu input
    BATTLE_INFO_COMBO_WAIT, // L=A: L pressed alone, waiting briefly for R
    BATTLE_INFO_COMBO_OPEN, // L+R pressed: open the info screen
};

// One side of a list-mode info screen (outside battle).
struct InfoScreenSideArgs
{
    struct Pokemon *party;  // must stay valid until the screen exits
    u8 count;               // number of party slots to show (max PARTY_SIZE)
    const u8 *title;        // header, e.g. "RENTALS" (copied; at most 31 characters)
    const u8 *shortName;    // for the other side's L/R hint, e.g. "JIM"; NULL = title
    u16 color;              // backdrop color
    const u8 *picks;        // optional per-slot pick order (1-3, 0 = not picked)
    bool8 asHealed;         // show full HP/PP and no status (e.g. Factory swap candidates)
};

u8 BattleInfoScreen_CheckCombo(void);
u8 InfoScreen_CheckComboOutsideBattle(void);
void ShowBattleInfoScreen(MainCallback exitCallback);
void ShowMonListInfoScreen(const struct InfoScreenSideArgs *sides, u8 numSides, u8 startSide, u8 startSlot, MainCallback exitCallback);

#endif // GUARD_BATTLE_INFO_SCREEN_H
