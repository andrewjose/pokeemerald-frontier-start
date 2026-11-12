# Battle Frontier Start: technical reference

For ROM hackers and decomp contributors who want to study, port or extend the hack. Everything here refers to the `frontier-start` branch at v1.47.

## Base and build

- Base (v1.47+): pret/pokeemerald `731ad5bfd` (2026-10-01, #2364), with real upstream ancestry. TheXaman's `tx_debug_system` is squashed into one commit (`Add TX debug system`), followed by the hack's per-version commits. v1.46 and earlier were based on pret `e178c18bf` via a rewritten copy of pret's history (first parent `bafe1b27e`, identical tree): a whole-history email rewrite stripped `gpgsig` headers from pret's GitHub merge commits starting with `4cb8528d9` (PR #94, 2017), so every later hash diverged and the merge base fell back to 2017.
  - Rebase: 34 first-parent commits replayed onto upstream, authors normalised to the ID-prefixed noreply address. One conflict: `GetCollisionAtCoords` in `src/event_object_movement.c`; upstream changed `u32 dir` to `u8 direction` and dropped the local copy, so the TX `FLAG_SYS_NO_COLLISION` early return was kept with no `direction` local. The rebased tree is identical to `git merge-tree --merge-base=e178c18bf` of old `frontier-start` with `upstream/master` plus that resolution.
  - Upstream symbol changes checked against all hack code: only `maxPp` collides by name, and that is the hack's own struct field in `battle_info_screen.c`, not `ChooseMoveStruct::maxPP`. Also checked: `goto_if_*`/`call_if_*` now use named comparison constants, items are now an enum (there is no `#if ITEM_*` in the hack), and `ld_script.ld` uses `INCLUDE "memory.ld"` and keeps the hack's added `.o` lines.
  - Save usage at v1.47 (`make modern` link report): SaveBlock1 15752/15872 B (99.24%), SaveBlock2 3884/3968 B (97.88%), PokemonStorage 33744/35712 B (94.49%). New option bits continue to go into existing padding.
- Original base (v1.46 and earlier) details: pret/pokeemerald `e178c18bf` (the branch carries its own copy of that history: its first parent `bafe1b27e` has the identical tree), merged with TheXaman's `tx_debug_system` (`Merge xaman/tx_debug_system`), then a fix-up commit for bit-rot between the two (`02-xaman-bitrot-fixes`: e.g. `InBattlePyramid_()` in `battle_setup.c`).
- Compiler (v1.47+): `make modern` with Ubuntu 24.04 `gcc-arm-none-eabi` 13.2.1 / binutils 2.42 / newlib; links with `ld_script_modern.ld` (wildcard object placement, so the explicit `.o` lines added to `ld_script.ld` only matter for agbcc). Output `pokeemerald_modern.gba`. `MODERN=1` implies `UBFIX` (`include/config.h`). Through v1.46 the compiler was `agbcc` (`make`, `pokeemerald.gba`; v1.46 release MD5 `6b0559abe18d52d705dc6445aa337b74`); agbcc still builds but is untested. `make compare` fails by design.
- CI: `.github/workflows/frontier-start.yml` on pushes/PRs to `frontier-start`: `make modern`, save/memory report, MD5, then `tools/headless/smoke_newgame.sh` (libmgba 0.10.2; script `tools/headless/newgame.txt`; asserts `*(u32*)(gMain+4) == CB2_Overworld|1` after the NEW GAME chain; symbols resolved from the ELF with `arm-none-eabi-nm`). Artifacts: ROM, `.md5`, `.map`, `memory-usage.txt`, screenshots, log.
- Save compatibility agbcc → modern: not yet verified. Load a v1.46 (agbcc) `.sav` in the modern build and check party, PC boxes, OPTION bits and streaks.
- New translation units are placed explicitly in `ld_script.ld` (`.text` and `.rodata` of `battle_info_screen.o`, `battle_info_damage.o`, `battle_damage_preview.o`, `pokemon_editor.o`).
- `include/constants/map_groups.h` shows a large diff against the merge base. It's generated from `data/maps/map_groups.json` by the build, not hand-edited.
- Release assets: a BPS against the US ROM (MD5 `605b89b67018abcea91e693a4dd25be3`) and `source-patches-vX.zip`, the same changes as numbered `git format-patch` files against the base above.

## Files

| File | Role |
|---|---|
| `src/new_game.c` | `NewGameInitData`: options, story flags, badges, items, warp to the Frontier |
| `src/main_menu.c` | Birch speech skipped (straight to gender/name, no send-off) |
| `src/overworld.c` | First field callback runs `ArchetypeSelect_Script` instead of `ExecuteTruckSequence` |
| `data/scripts/ArchetypeSelect.inc` | New-game script: guide, settings preset, fighting style, party/box setup |
| `src/preset_parties.c`, `include/preset_parties.h` | Archetype parties, box fillers (unbaked path), baked-box loader |
| `src/data/preset_boxes.h` | Generated: 225 encrypted `BoxPokemon` + box names (`tools/preset_boxes/bake.py`) |
| `src/battle_info_screen.c` | L+R info screen (battle and list modes) |
| `src/battle_info_damage.c` | Side-effect-free damage calculator |
| `src/battle_damage_preview.c` | HP-bar damage preview |
| `src/battle_controller_player.c` | Hooks: L+R combo, preview update/hide |
| `src/battle_factory_screen.c` | L+R on the rental (select) and swap screens |
| `src/pokemon_editor.c` | Pokémon editor |
| `src/party_menu.c`, `src/pokemon_storage_system.c`, `src/debug.c` | EDIT entry points |
| `src/pokemon.c` | `SetBoxMonPersonalityAndOtId` (re-key in place) |
| `src/option_menu.c` | New OPTION rows, scrolling list, GUIDE panel, `ApplySettingsPreset`, `gText_FrontierGuide` |
| `src/text.c`, `src/menu.c` | INSTANT text speed |
| `src/battle_tower.c` | Battle Factory IV fix (`FLAG_OPT_FACTORY_IV_VANILLA` restores the bug) |
| `data/maps/BattleFrontier_*Lobby/scripts.inc` | KEEP STREAK branch in each `QuitWithoutSaving` script |
| `data/maps/BattleFrontier_Lounge7/scripts.inc` | Free tutors (BP when `FLAG_OPT_TUTOR_COSTS_BP`), move relearner, move deleter |
| `src/title_screen.c`, `src/graphics.c`, `graphics/title_screen/emerald_version.png` | 128×64 8bpp banner, 32-color palette |

## New game

`NewGameInitData` (`src/new_game.c`), after the vanilla reset:

- Options: frame 1, STEREO, BUTTON MODE NORMAL, FAST text. They're written again at the end of the function because later zero-fills in the init chain clobbered the packed bitfield.
- Flags: `FLAG_SYS_GAME_CLEAR`, `FLAG_SYS_FRONTIER_PASS`, `FLAG_SYS_POKEMON_GET`, `FLAG_SYS_POKEDEX_GET`, `FLAG_SYS_POKENAV_GET`, `FLAG_SYS_B_DASH`, `FLAG_RECEIVED_RUNNING_SHOES`, `FLAG_LANDMARK_BATTLE_FRONTIER`, `FLAG_RESCUED_BIRCH`, `FLAG_RECEIVED_POKEDEX_FROM_BIRCH`, `FLAG_HIDE_BATTLE_FRONTIER_RECEPTION_GATE_SCOTT`, `FLAG_HIDE_BATTLE_FRONTIER_SUDOWOODO`, `FLAG_ENABLE_PROF_BIRCH_MATCH_CALL`, `FLAG_BADGE01_GET`..`FLAG_BADGE08_GET` (v1.46). `EnableNationalPokedex()`; `VAR_HAS_ENTERED_BATTLE_FRONTIER = 1` skips Scott's gate cutscene.
- Items: 900 of each berry (overflow to the PC), 99 × TM01–TM50, 1 × HM01–HM08, 99 each of Master Ball, Max Repel, Rare Candy, HP Up, Protein, Iron, Carbos, Calcium, Zinc.
- Warp: `MAP_BATTLE_FRONTIER_OUTSIDE_WEST`, x 26, y 40.

`main_menu.c`: the intro task's timer drops from `0xD8` to 30 frames; the Birch sprite branch is replaced by setting up the dialogue window and jumping to `Task_NewGameBirchSpeech_WaitForPlayerFadeIn` with Brendan at x 180 (the existing gender/name flow). After the name is confirmed, the send-off speech is skipped and the player shrinks into the game.

`overworld.c`: the new-game field callback becomes `FieldCB_ContinueScriptHandleMusic` with `ScriptContext_SetupScript(ArchetypeSelect_Script)`.

`ArchetypeSelect.inc`:

1. Waits for the location pop-up to finish, then `special HideMapNamePopUpWindow` (the pop-up's slide breaks the multichoice scroll box otherwise).
2. Shows `gText_FrontierGuide`.
3. `MULTI_SETTINGS_PRESET` (116) → `VAR_0x8004` → `special ApplySettingsPreset`.
4. `MULTI_ARCHETYPE_SELECT` (114) → `VAR_0x8004`; `VAR_0x8005 = 50`.
5. `special GivePresetPartyAndFillBoxes`.

`MULTI_ARCHETYPE_LEVEL` (115) and the per-archetype `GivePresetParty_*` specials are older entry points kept for compatibility.

## Preset parties and boxes

Menu index → archetype (`sMenuToArchetype`): 0 RECOMMENDED, 1 OFFENSE, 2 STALL, 3 SPEED, 4 TRICKROOM, 5 RAIN, 6 SUN, 7 SAND. Out-of-range indices fall back to 0. BALANCED (2) has a box but no menu entry since v1.24. RECOMMENDED is built from the L50 half of the Tower sample box (Latios, Metagross, Swampert, Salamence, Snorlax, Suicune with the guide's EV spreads).

Box layout:

| Box | Contents |
|---|---|
| 0–7 | Archetype teams (OFFENSE, STALL, BALANCED, SPEED, TRICKROOM, RAIN, SUN, SAND): L50 in slots 0–5, L100 in 6–11 |
| 8, 9 | Legendaries L50, L100 |
| 10 | Tower individual fighters L50 (30 sets, "Conquer the Tower" pp. 5–8) |
| 11 | Tower fighters overflow L50 |
| 12 | Tower sample teams, both levels (L50 in slots 0–9) |
| 13 | Tower individual fighters L100 |

### Baking (v1.38)

`PRESET_BOXES_BAKED` (`preset_parties.c`) selects the path. With it `FALSE`, `GivePresetPartyAndFillBoxes` builds every box through the fillers (`CreateMon` plus a shiny-personality search, about 20 s). To regenerate the baked data:

1. Build with `PRESET_BOXES_BAKED FALSE`, start a new game, and stop right after `GivePresetPartyAndFillBoxes` returns.
2. Dump `*gPokemonStoragePtr` (at least `4 + 14×30×80 + 14×9` bytes: current box, the 14 used boxes of `BoxPokemon`, box names) and the first 16 bytes of `*gSaveBlock2Ptr` (the OT ID is read from offset `0x0A`).
3. `python3 tools/preset_boxes/bake.py storage.bin saveblock2.bin > src/data/preset_boxes.h`
4. Set `PRESET_BOXES_BAKED TRUE` again.

The header holds `BAKED_OT_ID`, `sBakedBoxNames`, `sBakedBoxSlots` (box, slot per entry) and `sBakedBoxMons` (raw encrypted 80-byte `BoxPokemon`).

`LoadBakedBoxes` for each entry: `CpuCopy32` into the box slot; if the personality is shiny under `BAKED_OT_ID`, `GetShinyPersonalityForOtId` finds a personality that is shiny for the player's OT ID with the same nature, gender and ability; then `SetBoxMonPersonalityAndOtId` and the player's OT name and gender.

`SetBoxMonPersonalityAndOtId` (`pokemon.c`): `DecryptBoxMon`, copy the four substructs out in the order set by the old personality, write the new personality and OT ID, write the substructs back in the new order, recompute the checksum, `EncryptBoxMon`. `SetBoxMonData` can't do this because personality and OT ID key both the encryption and the substruct order.

## L+R info screen

Hooks in `battle_controller_player.c`:

- `HandleInputChooseAction` and `HandleInputChooseMove` call `BattleInfoScreen_CheckCombo()` **before** handling A. It returns `NONE`, `OPEN`, or `WAIT` (L=A button mode: L alone is held briefly to see if R follows, so L doesn't select). It's disabled for `BATTLE_TYPE_LINK | BATTLE_TYPE_RECORDED` and when `FLAG_OPT_INFO_SCREEN_OFF` is set.
- `OPEN`: `SE_SELECT`, fade to black, then `OpenBattleInfoFromActionMenu` / `OpenBattleInfoFromMoveMenu` call `ShowBattleInfoScreen(exitCallback)`. The exit path rebuilds with `ReshowBattleScreenAfterMenu` and resumes via `WaitForFadeThenChooseAction` / `WaitForFadeThenChooseMove` and `HandleChoose*AfterDma3`.

`battle_info_screen.c`:

- State in `AllocZeroed` (`sInfo`). On failure it returns straight to `exitCallback`. `DISPCNT`, `BLDCNT` and `BLDALPHA` are saved and restored.
- Opening side: foe by default (v1.37); `optionsInfoStartYours` opens on your side, on the battler being commanded.
- Header: player name vs. trainer class + name as the battle prints them (two opponents: "A & B"; wild: "WILD POKEMON"). Long names fall back to the small font, then to the bare name. Party icons sit right of the header.
- Controls: D-pad = Pokémon, L/R = side, A = stats ↔ damage view, SELECT = next target (damage) or side (stats), B/START = exit.
- List mode: `ShowMonListInfoScreen(sides, numSides, startSide, startSlot, exitCallback)` with `struct InfoScreenSideArgs` (party pointer, count, title, short name, backdrop color, optional pick order, `asHealed`).
- Battle Factory (`battle_factory_screen.c`): `Select_Task_OpenInfoScreen` shows all six rentals with pick markers; `CB2_InitSelectScreen` rebuilds the screen from `sFactorySelectScreen` on return. `Swap_Task_OpenInfoScreen` shows the swap candidates (the Pokémon of the trainer you just beat) "as healed" plus yours, and `Swap_RestoreAfterInfoScreen` restores sub-screen, cursor and button highlight (`fromInfoScreen`, `infoReturnCursor`). Their side is titled with that trainer's class and name, read from `frontier.trainerIds[curChallengeBattleNum - 1]`: by the time the swap screen runs, `gTrainerBattleOpponent_A` already holds the next opponent. In the Battle Tent the title is FOE'S.

## Damage calculator

`DamageCalc_Move(attacker, defender, move, result)` with `struct DamageCalcMon` (a `BattlePokemon`, a battler slot for side/field lookups, an `active` flag, and the party for Beat Up). `DamageCalc_InitMon` fills one from the live battle copy, or from party data with stages reset as on switch-in.

Pipeline:

1. Script-level power and type changes are mirrored: Hidden Power, Return/Frustration, Flail/Reversal (copy of `sFlailHpScaleToPowerTable`), Low Kick (copy of `sWeightToDamageTable`), Weather Ball, Facade, and so on.
2. Every global that `CalculateBaseDamage` and `AbilityBattleEffects` read or write is saved, the calculation runs through `CalculateBaseDamage`, crit and Charge as `Cmd_damagecalc` does, STAB and the type chart as `Cmd_typecalc` does, then the globals are restored.
3. `Cmd_adjustnormaldamage`'s 85–100% roll isn't rolled; min and max are both reported. Crit min/max are reported separately. `Random()` is never called.

Result kinds: `HIT`, `STATUS`, `IMMUNE`, `FAILS` (no Stockpile, Damp vs. Explosion, Dream Eater on an awake target, …), `VARIES` (Counter, Mirror Coat, Bide), `OHKO`. Also `effectiveness`, `cantKO` (False Swipe) and min/max hits for multi-hit moves (damage is the total). `DamageCalc_GetActiveWeather` returns 0 under Cloud Nine / Air Lock.

## HP-bar preview

`DamagePreview_Update(attacker, move, onlyTarget)` runs each frame from `HandleInputChooseMove` (all targets, `MAX_BATTLERS_COUNT`) and `HandleInputChooseTarget` (the selected target). `DamagePreview_Hide` is called on every exit from those menus, and `DamagePreview_Reset` in `PlayerHandleChooseMove` (new menu, no VRAM writes).

It works on the healthbox's bar sprite (`hMain_HealthBarSpriteId`, `data[5]`). The bar is tiles 2.. of that sprite, 48 px wide (6 tiles), and the fill lives in pixel rows 3 and 4. The original tiles are copied before the first change. The forecast alternates with the current HP: `[remaining | min..max roll (pale) | lost on any roll (empty)]`. Disabled by `optionsDamagePreviewOff`.

## Pokémon editor

API: `PokemonEditor_IsAllowed()` (false in the Battle Pyramid and Battle Pike, or with `FLAG_OPT_EDIT_OFF`), `PokemonEditor_OpenParty(slot, cb)`, `PokemonEditor_OpenBox(box, pos, allBoxes, cb)`, `PokemonEditor_OpenNew(cb)`. On exit `gLastViewedMonIndex` holds the slot or box position last shown (L/R can change it).

Entry points: `MENU_EDIT` in the field party menu (`actions[]` grew to 9) and the PC's Pokémon menu (`SCREEN_CHANGE_EDIT`, `Task_EditMon`); the debug menu's Party add, Party edit, PC edit and Give > Pokémon (complex). The old complex-give flow stays in `debug.c` as `DebugAction_Give_PokemonComplex_Old` (unused).

Personality-derived fields (nature, gender, shiny) are changed in place. `FindPersonality` builds a personality with the wanted gender (low byte) and shininess (high half from the OT ID), retrying random values until `personality % 25` gives the nature; `RebuildWithPersonality` then re-keys the Pokémon with `SetBoxMonPersonalityAndOtId` (the same helper the baked boxes use) and recalculates its stats. Every other field is untouched.

## Options and save layout

`SaveBlock2` options word (`include/global.h`), from the old 4-bit padding:

| Bits | Field |
|---|---|
| `optionsTextSpeed:3` | adds `OPTIONS_TEXT_SPEED_INSTANT` (3) |
| `optionsDamagePreviewOff:1` | v1.37 |
| `optionsInfoStartYours:1` | v1.37 |
| 2 bits | still padding |

Event flags (`include/constants/flags.h`), clear = Frontier Start behavior, so pre-v1.39 saves need no migration:

| Flag | Alias | Set means |
|---|---|---|
| `FLAG_UNUSED_0x027` | `FLAG_OPT_INFO_SCREEN_OFF` | L+R info screen off |
| `FLAG_UNUSED_0x028` | `FLAG_OPT_EDIT_OFF` | no EDIT |
| `FLAG_UNUSED_0x029` | `FLAG_OPT_TUTOR_COSTS_BP` | Lounge 7 tutors charge BP |
| `FLAG_UNUSED_0x02A` | `FLAG_OPT_FACTORY_IV_VANILLA` | vanilla Factory IV bug |
| `FLAG_UNUSED_0x02B` | `FLAG_OPT_KEEP_STREAK` | keep streaks on quit-without-save |

`option_menu.c`: rows `TEXTSPEED, BATTLESCENE, BATTLESTYLE, SOUND, BUTTONMODE, FRAMETYPE, INFOSCREEN, INFOSTART, DMGPREVIEW, EDITMENU, FREETUTORS, FACTORYIVS, KEEPSTREAK, GUIDE, CANCEL`. The list is taller than the screen: 7 visible rows with scroll arrows, the window starts at the top of BG0 and BG0 is scrolled. GUIDE draws `gText_FrontierGuide` in a panel over the bottom of the list.

`ApplySettingsPreset` (`VAR_0x8004`, out of range → VANILLA+) writes from `sSettingsPresets[]`:

| | VANILLA | VANILLA+ | MODERN |
|---|---|---|---|
| Text | MID | FAST | INSTANT |
| Frame / sound | 0 / MONO | 1 / STEREO | 1 / STEREO |
| Battle style | SHIFT | SHIFT | SET |
| Battle scene | on | on | off |
| Damage preview, info screen, EDIT | off | on | on |
| Tutors | cost BP | free | free |
| Factory IVs | vanilla bug | fixed | fixed |
| Keep streak | off | off | on |

INSTANT text (`text.c`): in `RunTextPrinters`, a printer at the player's speed (`textSpeed == 0`) with `OPTIONS_TEXT_SPEED_INSTANT` loops `RenderFont` (and the printer callback) up to `0x400` times while it returns `RENDER_PRINT`, then does a single `CopyWindowToVram(..., COPYWIN_GFX)`. `menu.c` maps INSTANT to delay 1 and accepts the new maximum in `GetPlayerTextSpeedDelay`.

## Battle Factory IV fix

`battle_tower.c`: vanilla (without `BUGFIX`) takes the Factory opponents' IV tier from `towerWinStreaks[battleMode][FRONTIER_LVL_50]`. The hack always takes the `BUGFIX` path (`factoryWinStreaks`) unless `FLAG_OPT_FACTORY_IV_VANILLA` is set.

## KEEP STREAK

In each of the seven lobbies' `EventScript_QuitWithoutSaving`, after the "didn't save" message: `goto_if_set FLAG_OPT_KEEP_STREAK, …_KeepStreak`, which skips the facility's streak reset (e.g. `tower_set TOWER_DATA_WIN_STREAK, 0` and `…_ACTIVE, FALSE`) and only does `frontier_set FRONTIER_DATA_CHALLENGE_STATUS, 0` plus `VAR_TEMP_CHALLENGE_STATUS = 255`.

## Title banner

`graphics/title_screen/emerald_version.png` is now 128×64 at 8bpp (`-mwidth 8 -mheight 8`, 0x2000 bytes) with 32 colors: 0–15 are the original banner's, 16–21 the outline, the BATTLE FRONTIER gradient and the shadow. The sprites are 64×64 (`VERSION_BANNER_RIGHT_TILEOFFSET` 128); X moved 8 px left and Y 16 px down so EMERALD stays where it was on screen. Drawn by `tools/title_screen/make_banner.py` from `tools/title_screen/vanilla_emerald_version.png` (Poppins Bold).

## Known issues and notes

- `MULTI_ARCHETYPE_LEVEL` and the `GivePresetParty_*` specials are unused leftovers.
- The baked boxes depend on `struct BoxPokemon` and the substruct layout. Any change to the box fillers needs a re-bake (above).
- The hack uses vanilla flags `0x27`–`0x2B`. Other hacks that use the same "unused" flags will collide.
