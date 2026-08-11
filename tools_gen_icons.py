#!/usr/bin/env python3
"""Generate 1-bit 10x10 Flipper icons for Echo from ASCII bitmaps.

'#' = foreground (black / on), anything else = background (white / off).
fbt thresholds PNGs to 1-bit where dark pixels become 'on'.

The mark is a ripple leaving a point: a phone calling out, and the room
hearing it. It is the same shape the chamber screen draws forty times a
second, which is the whole idea.
"""
from PIL import Image
import os

OUT = os.path.join(os.path.dirname(__file__), "icons")
os.makedirs(OUT, exist_ok=True)

GLYPHS = {
    # App mark: a source on the left, two wavefronts leaving it
    "echo_10px": [
        "..........",
        "......#...",
        "...#...#..",
        "..#.....#.",
        "###.#....#",
        "###.#....#",
        "..#.....#.",
        "...#...#..",
        "......#...",
        "..........",
    ],
    # Same shape, used by the About screen
    "ripple_10px": [
        "..........",
        "......#...",
        "...#...#..",
        "..#.....#.",
        "###.#....#",
        "###.#....#",
        "..#.....#.",
        "...#...#..",
        "......#...",
        "..........",
    ],
    # A handset, for anywhere a device needs a face
    "phone_10px": [
        "..#####...",
        "..#...#...",
        "..#...#...",
        "..#...#...",
        "..#...#...",
        "..#...#...",
        "..#...#...",
        "..#.#.#...",
        "..#####...",
        "..........",
    ],
    # A pin: a name that resolves to one address
    "pin_10px": [
        "...###....",
        "..#...#...",
        ".#.....#..",
        ".#..#..#..",
        ".#.###.#..",
        ".#..#..#..",
        "..#...#...",
        "...#.#....",
        "....#.....",
        "..........",
    ],
}


def write_glyph(name, rows):
    img = Image.new("1", (10, 10), 1)  # 1 = white background
    px = img.load()
    for y, row in enumerate(rows):
        for x, ch in enumerate(row[:10]):
            if ch == "#":
                px[x, y] = 0  # 0 = black / on
    path = os.path.join(OUT, f"{name}.png")
    img.save(path)
    return path


def main():
    for name, rows in GLYPHS.items():
        assert len(rows) == 10, f"{name}: expected 10 rows, got {len(rows)}"
        for row in rows:
            assert len(row) == 10, f"{name}: row is {len(row)} wide, expected 10"
        print("wrote", write_glyph(name, rows))


if __name__ == "__main__":
    main()
