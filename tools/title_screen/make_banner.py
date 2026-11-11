#!/usr/bin/env python3
# Draws graphics/title_screen/emerald_version.png for Frontier Start (v1.40):
# "EMERALD" (emerald green) over "BATTLE FRONTIER" (classic blue), outlined in
# navy, as a 128x32 16-color indexed image (the title screen's version banner).
#
#   python3 tools/title_screen/make_banner.py graphics/title_screen/emerald_version.png
import sys
from PIL import Image, ImageDraw, ImageFont

FONT = '/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf'
W, H = 128, 32

# index: color
PAL = [
    (255, 0, 255),    # 0 transparent
    (16, 32, 72),     # 1 outline: navy
    (222, 255, 230),  # 2 emerald, top (mint highlight)
    (115, 230, 156),  # 3
    (32, 189, 106),   # 4
    (16, 131, 82),    # 5 emerald, bottom
    (230, 238, 255),  # 6 classic blue, top (highlight)
    (115, 164, 230),  # 7
    (49, 106, 189),   # 8
    (16, 76, 131),    # 9 classic blue, bottom (Pantone Classic Blue, #0F4C81)
    (8, 20, 48),      # 10 drop shadow under EMERALD: deep navy
    (8, 20, 48),      # 11 drop shadow under BATTLE FRONTIER: deep navy
    (0, 0, 0), (0, 0, 0), (0, 0, 0), (0, 0, 0),
]

KERNING = {'AT': -1, 'LE': -1}

def text_mask(text, size, spacing):
    font = ImageFont.truetype(FONT, size)
    # draw each letter without antialiasing, with extra spacing
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

def place(canvas, mask, cx, top, grad, shadow):
    w, h = mask.size
    x0 = cx - w // 2
    px = mask.load()
    fill = set((x0 + x, top + y) for y in range(h) for x in range(w) if px[x, y])
    out = set()
    for (x, y) in fill:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1, 2):   # outline, thicker below
                p = (x + dx, y + dy)
                if p not in fill:
                    out.add(p)
    sh = set((x + 1, y + 1) for (x, y) in out) - fill - out
    cv = canvas.load()
    for (x, y) in sh:
        if 0 <= x < W and 0 <= y < H and cv[x, y] == 0:
            cv[x, y] = shadow
    for (x, y) in out:
        if 0 <= x < W and 0 <= y < H:
            cv[x, y] = 1
    for (x, y) in fill:
        if 0 <= x < W and 0 <= y < H:
            t = (y - top) / max(1, h - 1)
            cv[x, y] = grad[min(len(grad) - 1, int(t * len(grad)))]

canvas = Image.new('P', (W, H), 0)
pal = []
for c in PAL:
    pal += list(c)
canvas.putpalette(pal)

emerald = text_mask('EMERALD', 18, 1)
frontier = text_mask('BATTLE FRONTIER', 10, 1)
place(canvas, emerald, W // 2, 1, [2, 3, 4, 5], 10)
place(canvas, frontier, W // 2, 21, [6, 7, 8, 9], 11)
canvas.save(sys.argv[1], bits=4)
print('EMERALD', emerald.size, 'BATTLE FRONTIER', frontier.size)
