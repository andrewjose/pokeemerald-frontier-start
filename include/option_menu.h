#ifndef GUARD_OPTION_MENU_H
#define GUARD_OPTION_MENU_H

void CB2_InitOptionMenu(void);

// v1.45: the settings presets offered on a new game (gSpecialVar_0x8004).
#define SETTINGS_PRESET_VANILLA      0 // Emerald's own settings; Frontier Start's features off
#define SETTINGS_PRESET_VANILLA_PLUS 1 // Frontier Start's defaults (as before v1.45)
#define SETTINGS_PRESET_MODERN       2 // every convenience and fix on
void ApplySettingsPreset(void);

#endif // GUARD_OPTION_MENU_H
