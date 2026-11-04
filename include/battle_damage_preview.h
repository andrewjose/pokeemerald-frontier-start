#ifndef GUARD_BATTLE_DAMAGE_PREVIEW_H
#define GUARD_BATTLE_DAMAGE_PREVIEW_H

// Damage preview on the HP bars while the player picks a move.
//
// The HP bar of every Pokemon the highlighted move can hit alternates between
// its current HP and the forecast after the hit: the HP that is lost even on
// the lowest roll is shown empty, and the HP that the 85-100% roll decides is
// shown pale. The numbers come from the info screen's damage calculator.
//
// DamagePreview_Update is called every frame from the move and target menus;
// DamagePreview_Hide must be called on every way out of them, which puts the
// bars back exactly as they were.

// onlyTarget: a battler, or MAX_BATTLERS_COUNT for every Pokemon the move can hit.
void DamagePreview_Update(u8 attacker, u16 move, u8 onlyTarget);
void DamagePreview_Hide(void);
// Forgets any preview without touching VRAM (a new move menu).
void DamagePreview_Reset(void);

#endif // GUARD_BATTLE_DAMAGE_PREVIEW_H
