#ifndef GUARD_PRESET_PARTIES_H
#define GUARD_PRESET_PARTIES_H

// Called from script: VAR_0x8004 = archetype (0-7), VAR_0x8005 = level (50 or 100)
// Gives party + fills all 8 PC boxes with alternate-level teams.
void GivePresetPartyAndFillBoxes(void);

// Legacy single-archetype specials (lv50 only)
void GivePresetParty_Offense(void);
void GivePresetParty_Stall(void);
void GivePresetParty_Balanced(void);
void GivePresetParty_SpeedControl(void);
void GivePresetParty_TrickRoom(void);
void GivePresetParty_Rain(void);
void GivePresetParty_Sun(void);
void GivePresetParty_Sand(void);

#endif  // GUARD_PRESET_PARTIES_H
