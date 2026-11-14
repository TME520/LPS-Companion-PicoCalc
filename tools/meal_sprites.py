#!/usr/bin/env python3
"""LPS-114-CHG01: generate the Meal Planner card sprites.

Draws each food as 32x32 pixel art with Pillow (no anti-aliasing), adds a
one-pixel dark outline, and writes src/meal_sprites.h: one shared 16-colour
palette plus 4 bits per pixel (512 bytes per sprite, in flash). Index 0 is
transparent and takes the card colour when drawn. The app scales sprites x4.

    python3 tools/meal_sprites.py            # writes src/meal_sprites.h
    python3 tools/meal_sprites.py --png DIR  # also writes x8 PNG previews
"""
import argparse
import pathlib
from PIL import Image, ImageDraw

SIZE = 32
# Index: 0 transparent, then the shared palette (0xRRGGBB).
PALETTE = [
    0x000000,  # 0 transparent (card body)
    0x3B2A25,  # 1 outline
    0xFFFFFF,  # 2 white
    0xF2E2C4,  # 3 foam / cream
    0xB07A4F,  # 4 latte brown
    0x4A2C1D,  # 5 black coffee
    0xD9433B,  # 6 red
    0xF29B30,  # 7 orange
    0xF5D76E,  # 8 yellow / cheese
    0x6CBF4F,  # 9 green
    0x2F7D3A,  # 10 dark green
    0xD9A55B,  # 11 biscuit / tortilla
    0xCFD8E0,  # 12 cup grey
    0xFBE9A8,  # 13 light juice
    0xE0703A,  # 14 soup
    0x7F8FA6,  # 15 bowl blue-grey
]
T, OUT, WHITE, FOAM, LATTE, BLACK, RED, ORANGE, YELLOW, GREEN, DGREEN, TAN, CUP, JUICE, SOUP, BOWL = range(16)


def canvas():
    img = Image.new("P", (SIZE, SIZE), T)
    return img, ImageDraw.Draw(img)


def mug(d, liquid, top):
    d.rectangle([7, 9, 22, 27], fill=CUP)          # body
    d.ellipse([19, 12, 27, 22], outline=CUP, width=2)  # handle
    d.rectangle([9, 11, 20, 13], fill=liquid)       # coffee surface
    if top is not None:
        d.rectangle([9, 9, 20, 11], fill=top)
    d.line([8, 15, 8, 25], fill=WHITE)               # shine


def steam(d, x):
    for i, (dx, y) in enumerate([(0, 2), (1, 3), (0, 4), (-1, 5), (0, 6)]):
        d.point((x + dx, y), fill=CUP)
        d.point((x + 5 + dx, y + 1), fill=CUP)


def latte():
    img, d = canvas()
    d.rectangle([8, 6, 21, 27], fill=CUP)            # tall mug
    d.ellipse([18, 10, 27, 21], outline=CUP, width=2)
    d.rectangle([10, 6, 19, 9], fill=FOAM)
    d.rectangle([10, 10, 19, 12], fill=LATTE)
    d.point((14, 7), fill=WHITE); d.point((15, 8), fill=WHITE)
    d.line([9, 13, 9, 25], fill=WHITE)
    return img


def black():
    img, d = canvas()
    d.ellipse([3, 24, 28, 29], fill=CUP)             # saucer
    d.rectangle([8, 13, 22, 25], fill=CUP)
    d.ellipse([20, 15, 27, 22], outline=CUP, width=2)
    d.rectangle([10, 13, 20, 15], fill=BLACK)
    d.line([9, 17, 9, 23], fill=WHITE)
    steam(d, 13)
    return img


def tea():
    img, d = canvas()
    d.ellipse([3, 24, 28, 29], fill=CUP)
    d.rectangle([8, 13, 22, 25], fill=WHITE)
    d.ellipse([20, 15, 27, 22], outline=WHITE, width=2)
    d.rectangle([10, 13, 20, 15], fill=LATTE)
    d.line([12, 13, 9, 19], fill=OUT)                # bag string
    d.rectangle([6, 19, 9, 22], fill=YELLOW)          # tag
    steam(d, 14)
    return img


def mint_tea():
    img, d = canvas()
    d.polygon([(8, 9), (23, 9), (21, 28), (10, 28)], fill=CUP)   # glass
    d.polygon([(9, 13), (22, 13), (21, 27), (10, 27)], fill=JUICE)
    d.ellipse([11, 4, 17, 11], fill=GREEN)            # mint leaves
    d.ellipse([15, 3, 21, 10], fill=DGREEN)
    d.ellipse([12, 15, 16, 19], fill=GREEN)
    d.ellipse([16, 19, 20, 23], fill=DGREEN)
    d.line([10, 14, 11, 26], fill=WHITE)
    return img


def juice():
    img, d = canvas()
    d.polygon([(8, 8), (23, 8), (21, 28), (10, 28)], fill=CUP)
    d.polygon([(9, 12), (22, 12), (21, 27), (10, 27)], fill=JUICE)
    d.line([18, 2, 16, 20], fill=RED, width=2)        # straw
    d.line([20, 2, 23, 2], fill=RED, width=2)
    d.ellipse([20, 6, 27, 13], fill=ORANGE)           # slice
    d.line([10, 13, 11, 26], fill=WHITE)
    return img


def wrap():
    img, d = canvas()
    d.polygon([(4, 22), (20, 4), (28, 12), (12, 28)], fill=TAN)   # rolled tortilla
    d.ellipse([18, 3, 29, 14], fill=TAN)
    d.ellipse([20, 5, 27, 12], fill=YELLOW)           # cheese filling
    d.point((22, 7), fill=GREEN); d.point((24, 9), fill=GREEN); d.point((23, 10), fill=RED)
    d.line([8, 21, 18, 11], fill=FOAM)
    d.line([11, 24, 21, 14], fill=FOAM)
    return img


def bowl(d, fill):
    d.chord([3, 6, 28, 28], 0, 180, fill=BOWL)
    d.rectangle([3, 16, 28, 17], fill=fill)
    d.line([7, 20, 10, 24], fill=WHITE)


def salad():
    img, d = canvas()
    for x, y in [(5, 9), (12, 7), (19, 9)]:
        d.ellipse([x, y, x + 8, y + 8], fill=RED)
        d.point((x + 4, y + 4), fill=YELLOW)
        d.point((x + 3, y + 5), fill=YELLOW)
    d.ellipse([9, 12, 14, 16], fill=GREEN)
    d.ellipse([18, 12, 23, 16], fill=DGREEN)
    bowl(d, GREEN)
    return img


def biscuit():
    img, d = canvas()
    d.rounded_rectangle([4, 6, 27, 25], radius=4, fill=TAN)
    d.rounded_rectangle([6, 8, 25, 23], radius=3, outline=LATTE)
    for x in (10, 15, 20):
        for y in (12, 18):
            d.point((x, y), fill=LATTE)
    return img


def cheese():
    img, d = canvas()
    d.polygon([(3, 24), (28, 24), (28, 12), (3, 18)], fill=YELLOW)    # side
    d.polygon([(3, 18), (28, 12), (22, 7)], fill=JUICE)               # top
    for x, y, r in [(9, 20, 2), (17, 17, 2), (23, 20, 1), (14, 22, 1)]:
        d.ellipse([x - r, y - r, x + r, y + r], fill=ORANGE)
    return img


def apple():
    img, d = canvas()
    d.ellipse([5, 8, 17, 28], fill=RED)
    d.ellipse([14, 8, 27, 28], fill=RED)
    d.line([16, 9, 15, 3], fill=LATTE, width=2)
    d.ellipse([17, 2, 24, 7], fill=GREEN)
    d.ellipse([8, 12, 11, 17], fill=WHITE)
    return img


def orange():
    img, d = canvas()
    d.ellipse([4, 6, 27, 29], fill=ORANGE)
    d.ellipse([14, 2, 22, 8], fill=GREEN)
    d.point((15, 7), fill=DGREEN)
    for x, y in [(10, 14), (19, 20), (13, 23), (21, 13)]:
        d.point((x, y), fill=YELLOW)
    d.ellipse([8, 10, 11, 13], fill=YELLOW)
    return img


def soup():
    img, d = canvas()
    bowl(d, SOUP)
    d.line([20, 3, 27, 16], fill=CUP, width=2)        # spoon
    d.ellipse([18, 1, 23, 6], fill=CUP)
    for x in (8, 13):
        for i, (dx, y) in enumerate([(0, 6), (1, 8), (0, 10), (-1, 12)]):
            d.point((x + dx, y), fill=CUP)
    return img


QUESTION = """
..XXXXXX..
.XXXXXXXX.
XXX....XXX
XXX....XXX
.......XXX
.....XXXX.
....XXXX..
....XXX...
....XXX...
..........
....XXX...
....XXX...
"""


def blank():
    img, d = canvas()
    # A bold question mark: the mystery food.
    for y, row in enumerate(QUESTION.strip().splitlines()):
        for x, c in enumerate(row):
            if c == "X":
                d.rectangle([6 + 2 * x, 4 + 2 * y, 7 + 2 * x, 5 + 2 * y], fill=BOWL)
    return img


SPRITES = [("Latte", latte), ("Black", black), ("Tea", tea), ("MintTea", mint_tea), ("Juice", juice),
           ("Wrap", wrap), ("Salad", salad), ("Biscuit", biscuit), ("Cheese", cheese),
           ("Apple", apple), ("Orange", orange), ("Soup", soup), ("Blank", blank)]


def outline(img):
    px = img.load()
    edge = []
    for y in range(SIZE):
        for x in range(SIZE):
            if px[x, y] != T:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < SIZE and 0 <= ny < SIZE and px[nx, ny] not in (T, OUT):
                    edge.append((x, y))
                    break
    for p in edge:
        px[p] = OUT
    return img


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--png", type=pathlib.Path)
    parser.add_argument("--out", type=pathlib.Path, default=root / "src" / "meal_sprites.h")
    args = parser.parse_args()
    lines = ["// LPS-114-CHG01: Meal Planner sprites. Generated by tools/meal_sprites.py; do not edit.",
             "// 32x32, 4 bits per pixel (left pixel in the high nibble), index 0 transparent.",
             "#pragma once", "#include <cstdint>", "namespace lps { namespace meal_sprites {",
             f"constexpr int Size={SIZE};",
             "constexpr uint32_t Palette[16]={" + ",".join(f"0x{c:06x}" for c in PALETTE) + "};",
             "enum Id : uint8_t { " + ", ".join(n for n, _ in SPRITES) + ", Count };",
             f"constexpr uint8_t Pixels[Count][{SIZE * SIZE // 2}]={{"]
    flat = []
    for name, draw in SPRITES:
        img = outline(draw())
        data = list(img.tobytes())
        assert max(data) < 16
        packed = [(data[i] << 4) | data[i + 1] for i in range(0, len(data), 2)]
        rows = [",".join(f"0x{b:02x}" for b in packed[i:i + 16]) for i in range(0, len(packed), 16)]
        lines.append(f"    {{ // {name}\n    " + ",\n    ".join(rows) + "},")
        if args.png:
            args.png.mkdir(parents=True, exist_ok=True)
            rgb = Image.new("RGB", (SIZE, SIZE), (0xFB, 0xF4, 0xE6))
            rp = rgb.load()
            for i, v in enumerate(data):
                if v:
                    c = PALETTE[v]
                    rp[i % SIZE, i // SIZE] = (c >> 16, (c >> 8) & 255, c & 255)
            flat.append(rgb)
    lines += ["};", "}} // namespace lps::meal_sprites", ""]
    args.out.write_text("\n".join(lines))
    if args.png:
        sheet = Image.new("RGB", (len(flat) * (SIZE + 4) * 4, (SIZE + 4) * 4), (40, 40, 40))
        for i, img in enumerate(flat):
            sheet.paste(img.resize((SIZE * 4, SIZE * 4), Image.NEAREST), (i * (SIZE + 4) * 4 + 8, 8))
        sheet.save(args.png / "meal_sprites_sheet.png")


if __name__ == "__main__":
    main()
