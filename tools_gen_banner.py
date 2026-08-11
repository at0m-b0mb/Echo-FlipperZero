#!/usr/bin/env python3
"""Repo branding for Echo: the README banner and the GitHub social preview.

The motif is the product in one picture. A handset on the right, wavefronts
leaving it, and riding those wavefronts the actual names a phone gives away -
a hotel, a gym, a hospital, the router at home. The rings are cool because a
radio wave is nobody's fault; the names are warm because they are the part
that belongs to somebody.

The names are the same ones the demo-mode script uses, so the banner, the
screenshots and the app all tell one story.

    python3 tools_gen_banner.py
"""
from PIL import Image, ImageDraw, ImageFont
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "images")
os.makedirs(OUT, exist_ok=True)

INK = (9, 10, 13)
INK_2 = (19, 22, 30)
WAVE = (63, 216, 208)  # the ripple: cool, impersonal
WAVE_DIM = (28, 92, 92)
LEAK = (245, 166, 35)  # the name it carried: warm, and yours
PAPER = (238, 241, 245)
MUTED = (132, 142, 158)

AVENIR = "/System/Library/Fonts/Avenir Next.ttc"
MENLO = "/System/Library/Fonts/Menlo.ttc"

# Avenir Next.ttc face order, confirmed by getname(): 0 Bold, 2 Demi Bold,
# 5 Medium, 7 Regular, 8 Heavy. The odd indices are the italics, and picking
# one by accident is exactly the kind of thing a banner ships with.
AV_HEAVY, AV_BOLD, AV_MEDIUM, AV_REGULAR = 8, 0, 5, 7


def avenir(size, face=AV_BOLD):
    return ImageFont.truetype(AVENIR, size, index=face)


def mono(size, bold=False):
    return ImageFont.truetype(MENLO, size, index=1 if bold else 0)


# The names come off the demo script in helpers/demo_feed.c.
LEAKS = [
    ("Hilton_Honors", "a hotel"),
    ("CityHospital-Guest", "a hospital"),
    ("PlanetFitness", "a gym"),
    ("NETGEAR58", "home"),
]


def wash(w, h):
    """A quiet gradient, so the flat black does not read as a placeholder."""
    img = Image.new("RGB", (w, h), INK)
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(int(INK[i] + (INK_2[i] - INK[i]) * (t**0.75)) for i in range(3))
        d.line((0, y, w, y), c)
    return img


def handset(d, cx, cy, w, h, colour):
    """A phone, drawn thin - it is the source, not the subject."""
    d.rounded_rectangle((cx - w // 2, cy - h // 2, cx + w // 2, cy + h // 2),
                        radius=w // 5, outline=colour, width=3)
    d.line((cx - w // 6, cy - h // 2 + 11, cx + w // 6, cy - h // 2 + 11),
           fill=colour, width=3)
    d.ellipse((cx - 4, cy + h // 2 - 20, cx + 4, cy + h // 2 - 12), outline=colour, width=2)


def wavefronts(img, cx, cy, radii, spread=68):
    """Arcs leaving the handset, fading as they go. Drawn on their own layer so
    the alpha actually means something."""
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    for i, r in enumerate(radii):
        t = i / max(1, len(radii) - 1)
        alpha = int(215 * (1.0 - t) ** 1.25) + 18
        width = 3 if i < 2 else 2
        ld.arc((cx - r, cy - r, cx + r, cy + r), -spread, spread,
               fill=WAVE + (alpha,), width=width)
    img.alpha_composite(layer)


def leak_labels(img, entries, points, name_size, note_size):
    """The names, riding the wavefronts outward.

    Placed at explicit points rather than by angle: the long ones have to start
    far enough left that the note after them still lands on the canvas, and
    picking that by eye beats discovering it in the rendered PNG."""
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    f_name = mono(name_size, bold=True)
    f_note = avenir(note_size, AV_MEDIUM)

    for (name, note), (x, y) in zip(entries, points):
        ld.ellipse((x - 4, y - 4, x + 4, y + 4), fill=LEAK + (255,))
        ld.text((x + 14, y - name_size // 2 - 2), name, font=f_name, fill=LEAK + (255,))
        w = ld.textlength(name, font=f_name)
        ld.text((x + 18 + w, y - note_size // 2 - 1), note, font=f_note, fill=MUTED + (235,))

    img.alpha_composite(layer)


def wordmark(d, x, y, title_size, tag_size, sub_size, sub_lines):
    d.text((x, y), "ECHO", font=avenir(title_size, AV_HEAVY), fill=PAPER)
    y += int(title_size * 1.02)
    d.text((x, y), "Your phone is shouting", font=avenir(tag_size, AV_BOLD), fill=WAVE)
    y += int(tag_size * 1.18)
    d.text((x, y), "where you have been.", font=avenir(tag_size, AV_BOLD), fill=WAVE)
    y += int(tag_size * 1.6)
    for line in sub_lines:
        d.text((x, y), line, font=avenir(sub_size, AV_MEDIUM), fill=MUTED)
        y += int(sub_size * 1.45)
    return y


def build_banner():
    W, H = 1280, 400
    img = wash(W, H).convert("RGBA")
    d = ImageDraw.Draw(img)

    cx, cy = 726, H // 2
    wavefronts(img, cx, cy, [70, 100, 136, 178, 228, 290, 362, 448, 548])
    d = ImageDraw.Draw(img)
    handset(d, cx, cy, 58, 104, PAPER)

    leak_labels(
        img, LEAKS,
        points=[(986, 158), (862, 84), (1000, 268), (876, 336)],
        name_size=19, note_size=16,
    )

    d = ImageDraw.Draw(img)
    wordmark(
        d, 72, 74, 108, 30, 18,
        ["Wi-Fi probe-request privacy tracker", "Flipper Zero  +  ESP32   -   receive only"],
    )
    d.line((640, 96, 640, H - 96), fill=WAVE_DIM, width=1)

    path = os.path.join(OUT, "banner.png")
    img.convert("RGB").save(path)
    print("wrote", path)


def build_social():
    W, H = 1280, 640
    img = wash(W, H).convert("RGBA")

    cx, cy = 860, 330
    wavefronts(img, cx, cy, [78, 112, 152, 200, 258, 326, 406, 500], spread=92)
    d = ImageDraw.Draw(img)
    handset(d, cx, cy, 70, 126, PAPER)

    leak_labels(
        img, LEAKS[:3],
        points=[(940, 214), (906, 348), (954, 470)],
        name_size=20, note_size=17,
    )

    d = ImageDraw.Draw(img)
    y = wordmark(
        d, 88, 150, 132, 36, 21,
        ["Wi-Fi probe-request privacy tracker", "Flipper Zero  +  ESP32"],
    )
    d.text((88, y + 26), "at0m-b0mb/Echo-FlipperZero", font=mono(19), fill=WAVE_DIM)

    path = os.path.join(OUT, "social-preview.png")
    img.convert("RGB").save(path)
    print("wrote", path)


if __name__ == "__main__":
    build_banner()
    build_social()
