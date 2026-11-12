# Pokémon Emerald: Battle Frontier Start

A ROM hack of Pokémon Emerald, built on the [pret/pokeemerald](https://github.com/pret/pokeemerald) decompilation, that starts a new game at the **Battle Frontier** with a competitive team, so you can go straight to the facilities.

Current version: **v1.46**. Download the patch from [Releases](../../releases).

> **Help wanted: playtesting.** This needs more people playing it. If you find a bug, crash or anything that looks wrong, please [open an issue on GitHub](https://github.com/andrewjose/pokeemerald-frontier-start/issues) and include the version, your emulator or flash cart, what you were doing (facility, menu or battle) and a screenshot or save if you can.

## Documentation

- [docs/1-overview.md](docs/1-overview.md): what the hack does, for players
- [docs/2-engineering.md](docs/2-engineering.md): how it works, for software engineers new to GBA games and decomps
- [docs/3-technical.md](docs/3-technical.md): files, functions, hooks and save layout, for ROM hackers

## What's in it

- **New game start:** no Birch intro. You go straight to character creation, read a short guide, pick a settings preset, and then pick a fighting style. Each style gives you a level 50 team, and you start at the Battle Frontier with every facility open.
- **Pre-filled PC boxes:** your PC starts with 14 boxes of ready-to-use Pokémon (225 in all): teams for every fighting style at Lv50 and Lv100, the legendaries, and the Battle Tower fighters and sample teams from the "Conquer the Tower" guide.
- **Fighting styles:** Recommended (Latios, Metagross, Swampert, Salamence, Snorlax, Suicune), Offense, Stall, Speed Control, Trick Room, Rain, Sun and Sand. A Balanced team is in the PC too.
- **Settings presets:** **VANILLA** uses Emerald's own settings with the new features off. **VANILLA+** turns the new features on and uses FAST text. **MODERN** turns everything on and adds INSTANT text, battle animations off, SET battles, and keeping your streak on save-and-quit.
- **INSTANT text speed** in OPTION: each text box appears all at once, in battle too.
- **In-battle info (L+R):** see IVs, EVs, nature, stats, stat stages, ability, item, status and moves for every Pokémon on both sides, plus damage estimates for each move. It also works on the Battle Factory rental and swap screens.
- **Damage preview:** when you highlight a move, the foe's HP bar shows the range of damage it could do.
- **Pokémon editor:** EDIT in the party menu and the PC changes anything about a Pokémon (species, nature, IVs, EVs, moves, ability, shininess and more).
- **Free move services:** the move tutors' house has free tutors, a move relearner and a move deleter.
- **Starting items:** 900 of every berry, 99 of each TM, one of each HM, 99 Master Balls, Max Repels, Rare Candies and vitamins.
- **All eight badges:** HMs work in the field, and Pokémon with another trainer's OT obey you.
- **Fixes:** the Battle Factory uses the right win streak for opponent IVs (vanilla reads the Battle Tower's by mistake).
- **OPTION toggles:** each change can be switched back to vanilla behavior: L+R INFO, EDIT MENU, FREE TUTORS, FACTORY IVS, KEEP STREAK, DMG PREVIEW and which side the info screen opens on. **GUIDE** shows the overview again.
- **Title screen:** the original EMERALD lettering with BATTLE FRONTIER added in classic blue.
- **Debug menu:** R + START on the map opens TheXaman's debug menu.

## How to install and play

1. Get a clean **Pokémon Emerald (USA)** ROM. This repo doesn't provide one.
   MD5 `605b89b67018abcea91e693a4dd25be3`
2. Download `pokeemerald-frontier-start-v1.46.bps` from [Releases](../../releases).
3. Apply it with [Rom Patcher JS](https://www.marcrobledo.com/RomPatcher.js/) (in your browser) or [Flips](https://github.com/Alcaro/Flips).
4. Check the result: MD5 `6b0559abe18d52d705dc6445aa337b74`
5. Play in any GBA emulator (mGBA recommended) or on a flash cart. Start a **new game**: an old Emerald save won't get the Frontier start.

## Building from source

The hack lives on the **`frontier-start`** branch; `master` is an untouched copy of pret's pokeemerald. You need the same toolchain as pret's decomp: see [INSTALL.md](INSTALL.md) for Linux, macOS and Windows (WSL) setup, including `agbcc`.

```
git clone -b frontier-start https://github.com/andrewjose/pokeemerald-frontier-start
cd pokeemerald-frontier-start

# agbcc, the original GBA C compiler (one time; INSTALL.md has the details)
git clone https://github.com/pret/agbcc ../agbcc
(cd ../agbcc && ./build.sh && ./install.sh ../pokeemerald-frontier-start)

make -j$(nproc)
```

This builds `pokeemerald.gba`, the same ROM as the release patch (MD5 above). Don't use `make compare`: it checks against the vanilla ROM's SHA1 (`rom.sha1`), which this ROM doesn't match on purpose.

To make your own patch, create a BPS from the clean ROM to `pokeemerald.gba` with Flips (`flips --create --bps clean.gba pokeemerald.gba my-patch.bps`).

The `source-patches-vX.zip` on each release has the same changes as numbered patches against pret `e178c18bf` + TheXaman's `tx_debug_system`, if you'd rather apply them to your own pokeemerald tree.

Tools that aren't part of the build:

- `tools/title_screen/make_banner.py`: redraws the title banner (needs Pillow and the Poppins Bold font).
- `tools/preset_boxes/bake.py`: regenerates `src/data/preset_boxes.h` from a RAM dump (see [docs/3-technical.md](docs/3-technical.md)).

## Changelog

### Polish and presentation (v1.40–v1.46)
- **v1.46**: All eight badges on a new game.
- **v1.45**: Settings presets on a new game (VANILLA / VANILLA+ / MODERN) and an INSTANT text speed.
- **v1.44**: BATTLE FRONTIER is centered under EMERALD and has a soft drop shadow.
- **v1.43**: Larger BATTLE FRONTIER lettering on the title screen.
- **v1.42**: GUIDE moved from the START menu to OPTION, which restores the original START menu.
- **v1.41**: The title screen uses the original EMERALD lettering with BATTLE FRONTIER added. New games show a short overview guide before you pick a fighting style.
- **v1.40**: New title banner. The Birch intro and the dialog after character creation are skipped.

### Options and faster start (v1.37–v1.39)
- **v1.39**: OPTION toggles for each of the hack's changes to vanilla: info screen, EDIT menu, free tutors, Factory IVs, and keeping streaks on save-and-quit.
- **v1.38**: The pre-filled PC boxes are built into the ROM, so there's no wait when starting a new game.
- **v1.37**: The L+R info screen opens on the foe's side. OPTION toggles for the damage preview and which side the info screen opens on.

### Battle tools (v1.30–v1.36)
- **v1.36**: Highlighting a move shows its damage range on the target's HP bar.
- **v1.35**: The info screen header shows your name and the opponent's.
- **v1.34**: The info screen shows the damage-calc target. New saves default to normal button mode.
- **v1.33**: Pokémon editor: EDIT in the party menu and PC.
- **v1.32**: Damage calculator on the info screen.
- **v1.31**: The info screen also works on the Battle Factory rental and swap screens.
- **v1.30**: L+R battle info screen.

### Frontier tuning (v1.22–v1.29)
- **v1.29**: Keeping your streak on save-and-quit became optional.
- **v1.28**: Fixed Battle Factory IV scaling.
- **v1.27**: Save-and-quit no longer resets your streak, at all 7 facilities.
- **v1.26**: You start with one of each HM.
- **v1.25**: Curated starting items and recommended sample teams.
- **v1.24**: The RECOMMENDED team replaced BALANCED as the first choice.
- **v1.23**: Fixed a graphics glitch in the banner; clearer confirm text.
- **v1.22**: Your party is always level 50.

### Foundation (v1.0–v1.2)
- **v1.2**: Battle Tower preset boxes based on the "Conquer the Tower" guide: 30 fighter sets at L50 and L100, plus sample teams, all shiny with Hidden Power types set. Two-page fighting-style menu.
- **v1.1**: Lots of every item; move tutors and other move services became free.
- **v1.0**: First release. Start at the Battle Frontier, choose a team archetype, and get PC boxes pre-filled with every archetype's team. Includes TheXaman's debug menu.

## Credits

- [pret/pokeemerald](https://github.com/pret/pokeemerald), the decompilation this is built on. See [pret.github.io](https://pret.github.io/) for pret's other projects.
- [TheXaman's tx_debug_system](https://github.com/TheXaman/pokeemerald/tree/tx_debug_system), the debug menu and Pokémon creator.
- Battle Tower sets and sample teams: the "Conquer the Tower" guide.
- Title lettering: Poppins Bold (SIL Open Font License).

This is an unofficial fan project. Pokémon is © Nintendo, Creatures Inc. and GAME FREAK inc. No ROM data is included.
