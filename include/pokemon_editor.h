#ifndef GUARD_POKEMON_EDITOR_H
#define GUARD_POKEMON_EDITOR_H

#include "main.h"

// Pokemon editor (v1.33): a full-screen editor laid out like the L+R info
// screen. Opened from the party menu (EDIT), the PC (EDIT) and the debug
// menu's Pokemon Creator.
//
// On exit gLastViewedMonIndex holds the party slot / box position of the
// Pokemon that was showing last (the editor can switch Pokemon with L/R).

// FALSE while a Frontier challenge is in progress (Battle Pyramid, Battle Pike).
bool8 PokemonEditor_IsAllowed(void);

void PokemonEditor_OpenParty(u8 slot, MainCallback exitCallback);
// allBoxes: L/R walk every box, not just this one (debug menu "PC edit").
void PokemonEditor_OpenBox(u8 boxId, u8 boxPos, bool8 allBoxes, MainCallback exitCallback);
// Creates a new Pokemon; saving gives it to the player (party, or PC if full).
void PokemonEditor_OpenNew(MainCallback exitCallback);

#endif // GUARD_POKEMON_EDITOR_H
