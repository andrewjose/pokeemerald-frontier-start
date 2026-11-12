# Battle Frontier Start: how it works (for software engineers)

This is for engineers who haven't worked on GBA games or decompilations. Game-specific ideas are explained as they come up.

## The starting point: a decompilation

Pokémon Emerald shipped in 2005 as a 16 MB ARM binary for the Game Boy Advance. The [pret](https://github.com/pret/pokeemerald) project rewrote it as C and assembly that compiles back to the **byte-identical** original ROM, using `agbcc`, a rebuild of the GCC 2.95-era compiler Game Freak used. "Matching" is the point: if the output hashes the same as the retail ROM, the source is a faithful model of the game.

That turns ROM hacking into normal software work. Instead of patching bytes at fixed addresses, this hack edits C functions, adds source files and lets the linker lay everything out. The cost is that the output no longer matches retail, which is why `make compare` fails on this branch.

The branch history is: pret's `master`, then TheXaman's `tx_debug_system` (a debug menu and Pokémon creator) as one commit, then one commit per version of the hack.

In v1.47 the branch was rebased: its commits were replayed on top of the current pret `master`, the way you'd rebase a feature branch onto a moved `main`. Earlier, an author-email rewrite had run over the whole history, including pret's own commits. That stripped the GPG signatures from pret's merge commits, which changed their hashes, so git no longer recognised them as the same commits and a normal `git merge` of upstream produced 316 conflicting files. After the rebase the branch descends from pret's real commits again. Upstream's 88 new commits are almost all cleanups that keep the retail ROM byte-identical (renames, compiler-workaround fixes, comments), so the game doesn't change. The useful addition is a link-time report of how full each save block is.

v1.47 also switched compilers. pret builds with `agbcc` because matching retail requires Game Freak's compiler, but this hack stopped matching retail at v1.0, so there is nothing to gain from it. The hack now builds with a current `arm-none-eabi-gcc` (`make modern`), which generates better code, turns on pret's fixes for undefined behaviour in the original code (`UBFIX`), and is what pokeemerald-expansion uses. It is also the only build that has been tested since. A GitHub Actions workflow builds every push the same way and runs a headless smoke test: an emulator library (libmgba) is driven by a script of button presses through a new game, and the test checks that the game's main callback ends up on the overworld, the way you'd assert on a state machine's final state. The one open question with a compiler switch is old saves: a battery save is the save structs copied byte for byte, so it only loads if the new compiler lays the structs out identically. Both compilers follow the same ARM layout rules, but loading a v1.46 save into the new build is the check.

v1.48 only updates the CI workflow's GitHub Actions to their current major versions, which run on Node 24; the ROM is byte-identical to v1.47.

## GBA constraints that shape the code

- **No operating system and tiny RAM.** 256 KB of slow work RAM (EWRAM) and 32 KB of fast RAM (IWRAM). The game has a small heap, so new screens allocate their state when they open and free it when they close, and they check for allocation failure.
- **The ROM is read-only.** Data that never changes (like the preset PC boxes since v1.38) can live in ROM as `const` arrays at no RAM cost.
- **Tile-based graphics.** The screen is built from 8×8-pixel tiles on up to four background layers, plus sprites. Drawing a new screen means loading tiles, palettes and "tilemaps" (grids of tile indices) into video RAM. When you open the L+R info screen, the battle's tiles are thrown away and rebuilt on exit, exactly the way the game already handles opening the bag mid-battle.
- **One frame at a time.** Everything runs from a 60 Hz main loop. Long work is split into tasks that do a little each frame, and the code waits for things like palette fades to finish over several frames.
- **The save is a fixed-size struct.** It's written to 128 KB of flash. New settings can't add fields without breaking old saves, so this hack reuses spare bits and unused event flags (see below).

## How a new game starts

Vanilla Emerald starts with Professor Birch's speech, then a moving-truck cutscene in Littleroot Town. The hack changes three places:

1. **The intro** (`main_menu.c`): the Birch sprite and speech are skipped; the code jumps to the existing "boy or girl?" and naming steps.
2. **New-game setup** (`new_game.c`): after the normal reset, it sets the story flags a finished game would have (game clear, Frontier Pass, badges, Pokédex, running shoes, Scott's intro done), gives the starting items, and warps the player to the Battle Frontier instead of the truck. **Flags** are the game's global booleans, a couple of thousand bits in the save that scripts check to decide what has happened.
3. **A script** (`data/scripts/ArchetypeSelect.inc`) runs on arrival. Emerald's events are written in a small bytecode scripting language, assembled from `.inc` files. This script shows the guide, the settings-preset menu and the fighting-style menu, then calls C functions ("specials") that build the party and fill the PC.

## The preset PC boxes

A Pokémon in the save isn't a plain struct. Its data is split into four blocks whose **order depends on the Pokémon's "personality"** (a random 32-bit value that also decides nature, gender and shininess). The blocks are then **XOR-encrypted with a key made from the personality and the original trainer's ID**, and a checksum guards the result. That's the game's anti-cheat: edit a byte naively and the checksum fails, and the Pokémon turns into a "Bad Egg".

Until v1.38 the hack built all 225 box Pokémon at new game through the game's own `CreateMon`. Making the shiny ones means searching for a personality that is shiny for the player's ID, and that took about 20 seconds.

Since v1.38 the boxes are **precomputed**: the hack was run once with a fixed trainer ID, the PC was dumped from RAM, and `tools/preset_boxes/bake.py` turned the dump into a C array. At new game each baked Pokémon is copied out of ROM and **re-keyed**: decrypted under the old ID, then re-encrypted under the player's ID and name. Shiny ones get a new shiny personality for the player's ID that keeps the same nature, gender and ability. It's the same trick as reusing a precomputed fixture instead of building it at startup, with a migration step for the one field that varies.

## The L+R info screen

The battle engine has a controller per battler: a state machine that runs each frame while the player picks an action or move. The hack adds a check to the player's controller: if L and R are pressed together, it fades out, swaps in the info screen and records what to return to. On exit the battle screen is rebuilt with the game's own `ReshowBattleScreenAfterMenu`. No battle state is written, so the battle continues exactly as it would have.

The screen is generic: it takes one or two lists of Pokémon, so the Battle Factory rental and swap screens reuse it with their own lists.

## The damage calculator

The game computes damage inside battle-script commands that run when a move is used, and they write to shared globals as they go (current attacker, target, move, flags), with random rolls along the way. To predict damage without changing the battle, `battle_info_damage.c`:

- copies the bits of each move's script that adjust power (Hidden Power, Return, Flail, Weather Ball and so on);
- **saves every global** that the damage functions read or write, calls the game's own `CalculateBaseDamage` and the type-effectiveness logic, then **restores the globals**;
- never calls the random number generator. Instead of rolling the 85–100% damage factor, it reports the whole range.

Reusing the game's own functions means abilities, items, weather and type quirks behave exactly as in a real attack, with no second implementation to drift out of sync. The trade-off is that the game's functions weren't written to be called this way, so the save-and-restore list has to cover everything they touch. Not calling the random number generator matters too: each call advances it, which would change the rolls in the real battle.

The HP-bar preview (`battle_damage_preview.c`) uses the same calculator. It doesn't redraw the HP bar through the battle's code. It copies the bar's tiles, paints the forecast into them, and copies the originals back when the cursor moves.

## Options without breaking saves

The OPTION menu settings live in the save. The hack needed seven new ones:

- two went into **4 spare bits** already reserved in the options word (damage preview, info-screen side);
- five use **unused event flags** (flags 0x27–0x2B, which Emerald never touches).

Every new setting is designed so that **0 / clear means the hack's default behavior**, so a save from an older version reads the right values without migration. The settings presets just write all of these at once.

INSTANT text needed a new text-speed value. The text printer normally draws one character per frame; at INSTANT it keeps drawing until it hits a pause or a button wait, then copies the window to video RAM once.

## Keeping streaks

If you turn the GBA off mid-challenge without saving, Emerald notices on the next load and the facility's receptionist resets your streak. Each of the seven lobby scripts got one line: if the KEEP STREAK flag is set, skip the reset and just clear the "challenge in progress" status.

## Trade-offs and limits

- Everything is built for the US version (`pokeemerald`); the patch needs the US ROM.
- The ROM no longer matches retail, so pret's `make compare` can't be used as a regression test.
- Unused flags and spare bits are a finite resource. The hack documents each one it uses in `include/constants/flags.h` and `include/global.h`.

The deep technical details (exact functions, files, hooks and data layouts) are in [3-technical.md](3-technical.md).
