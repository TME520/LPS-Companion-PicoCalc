#!/usr/bin/env python3
"""LPS-114-CHG01b: Meal Planner sprites from the user's own card sheets.

Sources: assets/meal_deck_sheet1.png and assets/meal_deck_sheet2.png
("Daily Edible Deck", two columns x four rows of cards each).

For each distinct card the script:
  * crops the illustration (above the name), including the steam, hearts
    and motion lines drawn around the food;
  * turns the near-white card background into transparency by flood-filling
    from the crop edges (white cups and bowls stay opaque: their outline
    closes them);
  * trims, fits into 88 x 64 pixels (drawn x2 on the PicoCalc, 176 x 128)
    with premultiplied-alpha box filtering, and keeps pixels at alpha >= 50%;
  * quantizes the opaque pixels to 15 colours: one 16-colour palette per
    sprite, index 0 transparent, 4 bits per pixel;
  * samples the card's border and ribbon colours from the sheet.

The Blank card keeps the user's design: the grey "Blank" ribbon with its
burst lines (taller crop).

    python3 tools/meal_sprites.py            # writes src/meal_sprites.h
    python3 tools/meal_sprites.py --png DIR  # also writes a preview sheet
"""
import argparse
import pathlib
from collections import deque
from PIL import Image

W, H = 88, 64
ROOT = pathlib.Path(__file__).resolve().parent.parent
SHEETS = [ROOT / "assets" / "meal_deck_sheet1.png", ROOT / "assets" / "meal_deck_sheet2.png"]
# Card bounds on the 1055 x 1491 sheets (border included), measured once.
COLS = [(44, 515), (539, 1010)]
ROWS = [(108, 435), (459, 783), (807, 1122), (1145, 1454)]
# Sprite id -> (sheet, card index 0-7 reading order). Order = enum order.
SPRITES = [
    ("Latte", 1, 0), ("Black", 1, 1), ("Tea", 2, 0), ("MintTea", 2, 3), ("Juice", 2, 5),
    ("Wrap", 1, 3), ("Salad", 1, 5), ("Biscuit", 1, 6), ("Cheese", 1, 7),
    ("Apple", 2, 1), ("Orange", 2, 2), ("Soup", 2, 4), ("Blank", 2, 6),
]
BACKGROUND = 232  # min(R,G,B) at or above this, reachable from the edge, is background


def flat(img):
    """Pixel values in reading order (Pillow 13+ renames getdata)."""
    return img.get_flattened_data() if hasattr(img, "get_flattened_data") else img.getdata()


def card_box(index):
    (x0, x1), (y0, y1) = COLS[index % 2], ROWS[index // 2]
    return x0, y0, x1, y1


def sample(img, box):
    """Median colour of a region (per channel)."""
    px = list(flat(img.crop(box)))
    return tuple(sorted(c[i] for c in px)[len(px) // 2] for i in range(3))


def border_colour(img, box):
    x0, y0, x1, y1 = box
    y = (y0 + y1) // 2
    run = [img.getpixel((x, y)) for x in range(x0 - 2, x0 + 14)]
    return max(run, key=lambda c: max(c) - min(c))  # the most saturated pixel of the border line


def illustration(img, box, blank):
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    top, bottom = (0.12, 0.88) if blank else (0.03, 0.66)
    crop = img.crop((x0 + int(w * 0.12), y0 + int(h * top), x1 - int(w * 0.12), y0 + int(h * bottom))).convert("RGBA")
    cw, ch = crop.size
    px = crop.load()
    seen = bytearray(cw * ch)
    queue = deque((x, y) for x in range(cw) for y in (0, ch - 1))
    queue.extend((x, y) for y in range(ch) for x in (0, cw - 1))
    while queue:
        x, y = queue.popleft()
        if seen[y * cw + x]:
            continue
        seen[y * cw + x] = 1
        r, g, b, _ = px[x, y]
        if min(r, g, b) < BACKGROUND:
            continue
        px[x, y] = (255, 255, 255, 0)
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < cw and 0 <= ny < ch and not seen[ny * cw + nx]:
                queue.append((nx, ny))
    crop = crop.crop(crop.getbbox())
    scale = min(W / crop.width, H / crop.height)
    size = (max(1, round(crop.width * scale)), max(1, round(crop.height * scale)))
    small = crop.convert("RGBa").resize(size, Image.Resampling.BOX).convert("RGBA")
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    out.paste(small, ((W - size[0]) // 2, (H - size[1]) // 2))
    return out


def indexed(sprite):
    """15 colours for opaque pixels + index 0 for transparency.

    Median cut picks 14 colours; small but vivid details (the pink hearts)
    can fall between them, so the 15th colour is the mean of the pixels
    around the one the 14 represent worst. Every pixel then takes its nearest colour.
    """
    rgba = list(flat(sprite))
    opaque = [p[:3] for p in rgba if p[3] >= 128]
    strip = Image.new("RGB", (len(opaque), 1))
    strip.putdata(opaque)
    quant = strip.quantize(colors=14, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = quant.getpalette()[: 14 * 3]
    colours = [tuple(pal[i:i + 3]) for i in range(0, len(pal), 3)]
    distance = lambda a, b: sum((x - y) ** 2 for x, y in zip(a, b))
    nearest = lambda c: min(range(len(colours)), key=lambda i: distance(c, colours[i]))
    # The worst-represented pixels, then those close to the very worst one.
    errors = sorted(opaque, key=lambda c: distance(c, colours[nearest(c)]), reverse=True)
    worst = errors[0]
    if distance(worst, colours[nearest(worst)]) > 20 ** 2:
        group = [c for c in errors[:200] if distance(c, worst) < 48 ** 2]
        colours.append(tuple(sum(c[i] for c in group) // len(group) for i in range(3)))
    palette = [0] + [(r << 16) | (g << 8) | b for r, g, b in colours]
    palette += [0] * (16 - len(palette))
    cache = {}
    data = [cache.setdefault(p[:3], nearest(p[:3]) + 1) if p[3] >= 128 else 0 for p in rgba]
    return palette, data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--png", type=pathlib.Path)
    parser.add_argument("--out", type=pathlib.Path, default=ROOT / "src" / "meal_sprites.h")
    args = parser.parse_args()
    sheets = [Image.open(p).convert("RGB") for p in SHEETS]
    palettes, borders, ribbons, pixels, previews = [], [], [], [], []
    for name, sheet, index in SPRITES:
        img, box = sheets[sheet - 1], card_box(index)
        x0, y0, x1, y1 = box
        w, h = x1 - x0, y1 - y0
        blank = name == "Blank"
        border = border_colour(img, box)
        # Ribbon fill, left of the portion text (Blank: left of the word on its ribbon).
        ry = y0 + int(h * (0.47 if blank else 0.875))
        ribbon = sample(img, (x0 + int(w * 0.29), ry - 3, x0 + int(w * 0.33), ry + 3))
        palette, data = indexed(illustration(img, box, blank))
        palettes.append(palette)
        borders.append((border[0] << 16) | (border[1] << 8) | border[2])
        ribbons.append((ribbon[0] << 16) | (ribbon[1] << 8) | ribbon[2])
        pixels.append(data)
        if args.png:
            rgb = Image.new("RGB", (W, H), (0xFF, 0xFD, 0xF8))
            rgb.putdata([(palette[v] >> 16, (palette[v] >> 8) & 255, palette[v] & 255) if v else (0xFF, 0xFD, 0xF8) for v in data])
            previews.append(rgb)
    hexes = lambda values: ",".join(f"0x{v:06x}" for v in values)
    lines = ["// LPS-114-CHG01b: Meal Planner sprites from the user's card sheets",
             "// (assets/meal_deck_sheet1.png, assets/meal_deck_sheet2.png).",
             "// Generated by tools/meal_sprites.py; do not edit.",
             f"// {W}x{H}, 4 bits per pixel (left pixel in the high nibble), one 16-colour",
             "// palette per sprite, index 0 transparent. Border/Ribbon: the card's colours.",
             "#pragma once", "#include <cstdint>", "namespace lps { namespace meal_sprites {",
             f"constexpr int Width={W},Height={H};",
             "enum Id : uint8_t { " + ", ".join(n for n, _, _ in SPRITES) + ", Count };",
             "constexpr uint32_t Border[Count]={" + hexes(borders) + "};",
             "constexpr uint32_t Ribbon[Count]={" + hexes(ribbons) + "};",
             "constexpr uint32_t Palettes[Count][16]={"]
    lines += ["    {" + hexes(p) + "}," for p in palettes]
    lines += ["};", f"constexpr uint8_t Pixels[Count][{W * H // 2}]={{"]
    for (name, _, _), data in zip(SPRITES, pixels):
        packed = [(data[i] << 4) | data[i + 1] for i in range(0, len(data), 2)]
        rows = [",".join(f"0x{b:02x}" for b in packed[i:i + W // 2]) for i in range(0, len(packed), W // 2)]
        lines.append(f"    {{ // {name}\n    " + ",\n    ".join(rows) + "},")
    lines += ["};", "}} // namespace lps::meal_sprites", ""]
    args.out.write_text("\n".join(lines))
    if args.png:
        args.png.mkdir(parents=True, exist_ok=True)
        cell = (W * 3 + 12, H * 3 + 12)
        sheet = Image.new("RGB", (cell[0] * 5, cell[1] * 3), (40, 40, 40))
        for i, img in enumerate(previews):
            sheet.paste(img.resize((W * 3, H * 3), Image.Resampling.NEAREST), ((i % 5) * cell[0] + 6, (i // 5) * cell[1] + 6))
        sheet.save(args.png / "meal_sprites_sheet.png")


if __name__ == "__main__":
    main()
