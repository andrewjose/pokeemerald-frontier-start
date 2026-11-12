#!/usr/bin/env python3
# Draws graphics/title_screen/emerald_version.png for Frontier Start (v1.41; larger BATTLE FRONTIER in v1.43;
# centered under EMERALD with a soft drop shadow in v1.44):
# the original "EMERALD" lettering, unchanged, with "VERSION" removed, and
# "BATTLE FRONTIER" in classic blue outlined in navy underneath.
#
# The result is 128x64 (two 64x64 8bpp sprites) with a 32-color palette:
# colors 0-15 are the original banner's, 16-31 are for BATTLE FRONTIER.
#
#   python3 tools/title_screen/make_banner.py tools/title_screen/vanilla_emerald_version.png \
#       graphics/title_screen/emerald_version.png
#
# Needs Pillow and the Poppins Bold font (SIL OFL, https://fonts.google.com/specimen/Poppins).
# Put Poppins-Bold.ttf next to this script, install it system-wide, or set BANNER_FONT=/path/to/Poppins-Bold.ttf.
# Only needed to redraw the banner: the generated PNG is committed, so `make` doesn't run this.
import os, sys
from PIL import Image, ImageDraw, ImageFont

def find_font():
    if os.environ.get('BANNER_FONT'):
        return os.environ['BANNER_FONT']
    here = os.path.dirname(os.path.abspath(__file__))
    for d in (here, '/usr/share/fonts', '/usr/local/share/fonts', os.path.expanduser('~/.local/share/fonts'),
              os.path.expanduser('~/Library/Fonts'), '/Library/Fonts', r'C:\Windows\Fonts'):
        for root, _, files in os.walk(d):
            if 'Poppins-Bold.ttf' in files:
                return os.path.join(root, 'Poppins-Bold.ttf')
    sys.exit('Poppins-Bold.ttf not found: set BANNER_FONT or put it next to make_banner.py '
             '(free download: https://fonts.google.com/specimen/Poppins)')

FONT = find_font()
W, H = 128, 64
FRONTIER_TOP = 27
# EMERALD moves EMERALD_SHIFT pixels right inside the banner, and the sprites
# move the same distance left (VERSION_BANNER_*_X in src/title_screen.c), so it
# stays where it was on screen and BATTLE FRONTIER has room to be centered under it.
EMERALD_SHIFT = 8
FRONTIER_CENTER_X = 55 + EMERALD_SHIFT  # EMERALD spans x 3-106 before the shift
SHADOW_OFFSET = (2, 2)
KERNING = {'AT': -1, 'LE': -1}

# Added colors, palette indices 16-21
OUTLINE, B0, B1, B2, B3, SHADOW = 16, 17, 18, 19, 20, 21
NEW_COLORS = [
    (16, 32, 72),     # 16 outline: navy
    (230, 238, 255),  # 17 classic blue, top highlight
    (115, 164, 230),  # 18
    (49, 106, 189),   # 19
    (16, 76, 131),    # 20 classic blue, bottom (Pantone Classic Blue, #0F4C81)
    (36, 72, 96),     # 21 drop shadow: soft slate blue, a little darker than the sky
]

def text_mask(text, size, spacing):
    font = ImageFont.truetype(FONT, size)
    widths = [font.getbbox(c)[2] - font.getbbox(c)[0] if c != ' ' else size // 3 for c in text]
    total = sum(widths) + spacing * (len(text) - 1)
    img = Image.new('1', (total + 4, size * 2), 0)
    d = ImageDraw.Draw(img)
    d.fontmode = '1'
    x = 2
    for i, (c, w) in enumerate(zip(text, widths)):
        if c != ' ':
            d.text((x - font.getbbox(c)[0], 0), c, font=font, fill=1)
        x += w + spacing + KERNING.get(text[i:i + 2], 0)
    return img.crop(img.getbbox())

def main(src, dst):
    vanilla = Image.open(src)
    vp = vanilla.getpalette()[:48]
    canvas = Image.new('P', (W, H), 0)
    canvas.putpalette(vp + sum((list(c) for c in NEW_COLORS), []) + [0] * (3 * (16 - len(NEW_COLORS))))

    # EMERALD: every pixel of the original, minus VERSION (the middle of rows 19+;
    # the tails of the E and D that reach below row 18 stay)
    for y in range(vanilla.height):
        for x in range(vanilla.width):
            c = vanilla.getpixel((x, y))
            if c and (y <= 18 or x < 18 or x >= 88):
                canvas.putpixel((x + EMERALD_SHIFT, y), c)

    # BATTLE FRONTIER
    mask = text_mask('BATTLE FRONTIER', 12, 1)  # 9 px capitals (v1.42: 7)
    w, h = mask.size
    x0, top = FRONTIER_CENTER_X - (w + 1) // 2, FRONTIER_TOP
    fill = set((x0 + x, top + y) for y in range(h) for x in range(w) if mask.getpixel((x, y)))
    out = set()
    for (x, y) in fill:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1, 2):
                p = (x + dx, y + dy)
                if p not in fill:
                    out.add(p)
    sx, sy = SHADOW_OFFSET
    shadow = set((x + sx, y + sy) for (x, y) in fill | out) - fill - out
    grad = [B0, B1, B2, B3]
    for (x, y) in shadow:
        if canvas.getpixel((x, y)) == 0:
            canvas.putpixel((x, y), SHADOW)
    for p in out:
        canvas.putpixel(p, OUTLINE)
    for (x, y) in fill:
        t = (y - top) / max(1, h - 1)
        canvas.putpixel((x, y), grad[min(3, int(t * 4))])
    canvas.save(dst)

main(sys.argv[1], sys.argv[2])
