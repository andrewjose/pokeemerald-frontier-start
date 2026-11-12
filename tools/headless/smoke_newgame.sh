#!/usr/bin/env bash
# Headless smoke test: boot the ROM, play through NEW GAME (naming, guide,
# settings preset, fighting style) and check that the game reaches the
# overworld at the Battle Frontier. Screenshots go to $OUT.
#
# Usage: tools/headless/smoke_newgame.sh [ROM] [ELF] [OUT]
# Needs: libmgba-dev, libpng-dev, arm-none-eabi-binutils (for nm), python3-pil (optional, for PNGs)
set -euo pipefail
ROM=${1:-pokeemerald_modern.gba}
ELF=${2:-pokeemerald_modern.elf}
OUT=${3:-build/smoke}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"

cc -O2 -o "$OUT/gbarun" "$HERE/gbarun.c" -lmgba -lpng -lm

sym() { arm-none-eabi-nm "$ELF" | awk -v s="$1" '$3==s {print "0x"$1}' | head -n1; }
GMAIN=$(sym gMain)
CB2_OVERWORLD=$(sym CB2_Overworld)
CALLBACK2=$(printf '0x%08x' $((GMAIN + 4)))
WANT=$(printf '0x%08x' $((CB2_OVERWORLD + 1)))   # THUMB bit

sed -e "s#@OUT@#$OUT#g" -e "s#@CALLBACK2@#$CALLBACK2#g" -e "s#@WANT@#$WANT#g" \
    "$HERE/newgame.txt" > "$OUT/newgame.run.txt"

set +e
"$OUT/gbarun" "$ROM" "$OUT/newgame.run.txt" | tee "$OUT/log.txt"
rc=${PIPESTATUS[0]}
set -e

if python3 -c 'import PIL' 2>/dev/null; then
    python3 - "$OUT" <<'EOF'
import glob, sys
from PIL import Image
for p in glob.glob(sys.argv[1] + '/*.ppm'):
    Image.open(p).save(p[:-4] + '.png')
EOF
fi
exit $rc
