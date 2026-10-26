#ifndef GUARD_BATTLE_INFO_SCREEN_H
#define GUARD_BATTLE_INFO_SCREEN_H

#include "main.h"

// In-battle info screen: opened with L+R from the battle action or move menu.
// Shows IVs, EVs, nature, stats, stat stages, ability, item and moves for
// every Pokemon on both sides of the battle.

enum {
    BATTLE_INFO_COMBO_NONE, // carry on with normal menu input
    BATTLE_INFO_COMBO_WAIT, // L=A: L pressed alone, waiting briefly for R
    BATTLE_INFO_COMBO_OPEN, // L+R pressed: open the info screen
};

u8 BattleInfoScreen_CheckCombo(void);
void ShowBattleInfoScreen(MainCallback exitCallback);

#endif // GUARD_BATTLE_INFO_SCREEN_H
