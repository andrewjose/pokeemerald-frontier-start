# Headless testing

`gbarun` drives a ROM through libmgba (0.10.x) with a script of button presses, screenshots and memory checks. No window, no emulator UI, so it runs in CI.

```
sudo apt-get install libmgba-dev libpng-dev
tools/headless/smoke_newgame.sh pokeemerald_modern.gba pokeemerald_modern.elf build/smoke
```

`smoke_newgame.sh` builds `gbarun`, looks up `gMain` and `CB2_Overworld` in the ELF, plays `newgame.txt` (intro, NEW GAME, default name, guide pages, VANILLA+, RECOMMENDED) and checks that `gMain.callback2` ends up on the overworld. It exits non-zero if not. Screenshots of each stage go to the output folder.

## Script commands

One per line; `#` starts a comment.

| Command | Effect |
|---|---|
| `wait N` | run N frames with no keys |
| `hold KEYS N` | hold keys (`A`, `R+START`, `DOWN`...) for N frames |
| `tap KEYS` / `taps KEYS N` | 4 frames down, 8 up (N times) |
| `shot FILE.ppm` | save the current frame |
| `state_save FILE` / `state_load FILE` | savestates |
| `peek8/16/32 ADDR`, `poke8/16/32 ADDR VAL` | read/write memory |
| `expect32 ADDR VAL` | fail the run if the word at ADDR isn't VAL |
| `waitfor32 ADDR VAL MAX` | run until the word equals VAL (at most MAX frames) |
| `tapuntil32 ADDR VAL KEYS MAX` | tap KEYS every 24 frames until the word equals VAL |
| `dump ADDR LEN FILE`, `sav_out FILE` | raw memory / battery save to a file |

## Notes

- Addresses change with every build. Get them from the ELF (`arm-none-eabi-nm pokeemerald_modern.elf`) and add 1 for THUMB functions.
- Presses during text printing are dropped. When text or menus change, `newgame.txt` may need its A-press count adjusted: screenshot each step and recount.
- The first press after `state_load` can be dropped; replaying from boot is more reliable for debug-menu flows.
