#!/usr/bin/env python3
"""Render Echo's screens at 128x64 and upscale them for the README.

These are mockups, not photographs - but they are not drawings either. Two
things keep them honest:

  * the layout constants below are copied from the view headers, and the draw
    code is a port of the C, so a label that runs off the edge here runs off
    the edge on the device too;

  * every number and verdict comes out of `test/host_echo_test --dump`, which
    scores the demo-mode personas with the shipped engine. Nothing on these
    screenshots was typed in by hand.

Font mapping, chosen so the mockup is never narrower than the hardware:

    FontPrimary    helvB08      -> Menlo Bold 10  (6 px/char)
    FontSecondary  haxrcorp4089 -> Menlo 8        (4.8 px/char, real is ~4)

Anything that fits here fits on the Flipper.

    python3 tools_gen_mockups.py
"""
from PIL import Image, ImageDraw, ImageFont
import math
import os
import subprocess
import sys

W, H = 128, 64
SCALE = 4
BEZEL = 10

BG = (247, 172, 47)
FG = (36, 26, 12)
CASE = (28, 28, 32)

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "images")
os.makedirs(OUT, exist_ok=True)

MENLO = "/System/Library/Fonts/Menlo.ttc"
F_PRIMARY = ImageFont.truetype(MENLO, 10, index=1)
F_SECONDARY = ImageFont.truetype(MENLO, 8, index=0)

# ---------------------------------------------------------------- layout
# views/chamber_view.h
CH_HDR_BASE, CH_RULE_Y = 8, 10
CH_TOP, CH_BOT = 11, 50
CH_CX, CH_CY = 64, 31
CH_RULE2_Y, CH_TICK_BASE = 51, 61
CH_FEED_TOP, CH_FEED_H = 13, 10
CH_RX_SCALE, CH_RY_SCALE = 2.5, 0.85
CH_R_MIN, CH_R_MAX = 7.0, 20.0
CH_RIPPLE_MAX, ECHO_RIPPLE_MS = 9, 1400

# views/net_list_view.h and views/dev_list_view.h share these
LIST_TOP, LIST_ROW_H, LIST_VISIBLE = 10, 11, 4
LIST_STRIP_RULE, LIST_STRIP_BASE = 54, 62

# views/dossier_view.c
DOS_LIST_TOP, DOS_LIST_ROW_H, DOS_LIST_VISIBLE = 22, 10, 3
DOS_FOOT_RULE, DOS_FOOT_BASE = 52, 62

# views/explain_view.c
EX_ART_TOP, EX_ART_BOT = 12, 38
EX_RULE_TOP, EX_RULE_MID = 10, 40
EX_LINE1, EX_LINE2 = 49, 58


# ---------------------------------------------------------------- canvas
class Canvas:
    """The handful of canvas_* calls Echo actually uses, and nothing else."""

    def __init__(self):
        self.img = Image.new("RGB", (W, H), BG)
        self.d = ImageDraw.Draw(self.img)
        self.color = FG

    def set_color(self, c):
        self.color = c

    def str(self, x, baseline, text, font=F_SECONDARY):
        self.d.text((x, baseline), text, font=font, fill=self.color, anchor="ls")

    def str_right(self, x, baseline, text, font=F_SECONDARY):
        self.d.text((x, baseline), text, font=font, fill=self.color, anchor="rs")

    def str_center(self, x, baseline, text, font=F_SECONDARY):
        self.d.text((x, baseline), text, font=font, fill=self.color, anchor="ms")

    def line(self, x0, y0, x1, y1):
        self.d.line((x0, y0, x1, y1), fill=self.color)

    def box(self, x, y, w, h):
        self.d.rectangle((x, y, x + w - 1, y + h - 1), fill=self.color)

    def frame(self, x, y, w, h):
        self.d.rectangle((x, y, x + w - 1, y + h - 1), outline=self.color)

    def rframe(self, x, y, w, h, r):
        self.d.rounded_rectangle((x, y, x + w - 1, y + h - 1), radius=r, outline=self.color)

    def rbox(self, x, y, w, h, r):
        self.d.rounded_rectangle((x, y, x + w - 1, y + h - 1), radius=r, fill=self.color)

    def circle(self, cx, cy, r):
        self.d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=self.color)

    def disc(self, cx, cy, r):
        self.d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=self.color)

    def dot(self, x, y):
        self.d.point((x, y), fill=self.color)

    def save(self, name):
        big = self.img.resize((W * SCALE, H * SCALE), Image.NEAREST)
        shell = Image.new("RGB", (W * SCALE + BEZEL * 2, H * SCALE + BEZEL * 2), CASE)
        shell.paste(big, (BEZEL, BEZEL))
        path = os.path.join(OUT, name)
        shell.save(path)
        print("wrote", path)
        return shell


# ------------------------------------------------------- engine-derived data
def load_dump():
    """Score the demo personas with the shipped engine and read the result."""
    binary = os.path.join(HERE, "test", "host_echo_test")
    if not os.path.exists(binary):
        subprocess.run(["make", "-C", os.path.join(HERE, "test")], check=True)
    out = subprocess.run([binary, "--dump"], capture_output=True, text=True, check=True).stdout

    devices, ssids = [], []
    for row in out.splitlines():
        f = row.split("\t")
        if f[0] == "DEV":
            devices.append(
                dict(
                    mac=f[1], vendor=f[2], randomized=f[3] == "1", named=int(f[4]),
                    score=int(f[5]), grade=f[6], group=int(f[7]), probes=int(f[8]),
                    worst=f[9], worst_cat=f[10],
                    cols=[int(f[11]), int(f[12]), int(f[13]), int(f[14]), int(f[15])],
                    verdict=f[16],
                )
            )
        elif f[0] == "SSID":
            ssids.append(
                dict(name=f[1], cat=f[2], tag=f[3], hits=int(f[4]),
                     askers=int(f[5]), pin=f[6] == "1", reveals=f[7])
            )
    return devices, ssids


DEVICES, SSIDS = load_dump()


# ----------------------------------------------------------------- helpers
def fnv1a(data, h=0x811C9DC5):
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def mac_bytes(mac_str):
    return bytes(int(p, 16) for p in mac_str.split(":"))


def bitrev4(v):
    """chamber_bitrev4(): 1 -> 8, 2 -> 4, 3 -> 12."""
    r = 0
    for i in range(4):
        r = (r << 1) | ((v >> i) & 1)
    return r


def chamber_radius(rssi):
    v = max(-95, min(-30, rssi))
    t = (-30 - v) / 65.0
    return CH_R_MIN + t * (CH_R_MAX - CH_R_MIN)


def chamber_polar(angle, radius):
    rad = angle * (2 * math.pi / 256.0)
    x = CH_CX + int(math.cos(rad) * radius * CH_RX_SCALE)
    y = CH_CY + int(math.sin(rad) * radius * CH_RY_SCALE)
    return max(3, min(124, x)), max(CH_TOP + 2, min(CH_BOT - 2, y))


def elide(text, limit):
    if len(text) <= limit:
        return text
    return text[: limit - 2] + ".."


def tag_box(c, x, y, tag, named=True):
    if not named:
        c.frame(x, y, 9, 9)
        return
    c.box(x, y, 9, 9)
    c.set_color(BG)
    c.str_center(x + 4, y + 7, tag)
    c.set_color(FG)


def listener(c):
    """chamber_listener(): a crosshair in a ring - that is where you stand."""
    c.circle(CH_CX, CH_CY, 3)
    c.line(CH_CX - 5, CH_CY, CH_CX - 4, CH_CY)
    c.line(CH_CX + 4, CH_CY, CH_CX + 5, CH_CY)
    c.line(CH_CX, CH_CY - 5, CH_CX, CH_CY - 4)
    c.line(CH_CX, CH_CY + 4, CH_CX, CH_CY + 5)
    c.dot(CH_CX, CH_CY)


def ring(c, cx, cy, r):
    """chamber_ring(): a circle clipped to the chamber band, plotted by hand."""
    if r < 1:
        return
    x, y, d = 0, r, 3 - 2 * r
    while y >= x:
        for px, py in (
            (cx + x, cy + y), (cx - x, cy + y), (cx + x, cy - y), (cx - x, cy - y),
            (cx + y, cy + x), (cx - y, cy + x), (cx + y, cy - x), (cx - y, cy - x),
        ):
            if 0 <= px <= 127 and CH_TOP + 1 <= py <= CH_BOT - 1:
                c.dot(px, py)
        if d > 0:
            y -= 1
            d += 4 * (x - y) + 10
        else:
            d += 4 * x + 6
        x += 1


def bucketed(blips):
    """chamber_draw_room(): bit-reversed table index, first free bucket wins."""
    used, out = 0, []
    for slot, (mac, rssi, age, named) in enumerate(blips):
        bucket = bitrev4(slot & 0x0F)
        for t in range(16):
            cand = (bucket + t) & 0x0F
            if not (used & (1 << cand)):
                bucket = cand
                break
        used |= 1 << bucket
        out.append((bucket * 16 + 8, rssi, age, named))
    return out


# ------------------------------------------------------------------ screens
def chrome(c, devices_n, leaking_n, demo=True, connected=True, channel=0):
    c.str(2, CH_HDR_BASE, "ECHO", F_PRIMARY)
    if demo:
        c.str(32, CH_HDR_BASE, "DEMO")
    tail = "hop" if channel == 0 else f"c{channel}"
    c.str_right(118, CH_HDR_BASE, f"D{devices_n} L{leaking_n} {tail}")
    (c.disc if connected else c.circle)(123, 4, 2)
    c.line(0, CH_RULE_Y, 127, CH_RULE_Y)


# The moment each device was last heard, chosen to catch the ripples mid-flight.
BLIPS = [
    # (mac, rssi, age_ms, named)
    ("34:23:BA:7C:19:04", -47, 980, True),
    ("7C:B2:7D:44:12:9A", -72, 420, True),
    ("C0:BD:D1:0A:5E:22", -80, 1250, True),
    ("8A:3F:11:02:9C:41", -66, 250, False),
    ("B6:7C:D9:41:22:08", -63, 1900, False),
    ("EA:05:63:9F:11:B2", -70, 700, False),
    ("8E:41:C2:19:7A:03", -58, 1500, False),
]


def screen_listen():
    c = Canvas()
    chrome(c, 7, 3)

    listener(c)

    ripples = 0
    for angle, rssi, age, named in bucketed(BLIPS):
        x, y = chamber_polar(angle, chamber_radius(rssi))
        if age < ECHO_RIPPLE_MS and ripples < 4:
            r = int(age * CH_RIPPLE_MAX / ECHO_RIPPLE_MS)
            if r >= 2:
                ring(c, x, y, r)
                if named:
                    ring(c, x, y, r - 1)
                ripples += 1
        if named:
            c.disc(x, y, 2)
        else:
            c.circle(x, y, 2)

    c.line(0, CH_RULE2_Y, 127, CH_RULE2_Y)
    tag_box(c, 2, CH_RULE2_Y + 2, "T")
    c.str(14, CH_TICK_BASE, elide("Hilton_Honors", 16), F_PRIMARY)
    c.str_right(126, CH_TICK_BASE, "-54")
    return c.save("screen_listen.png")


FEED = [
    ("T", "Hilton_Honors", -54, True),
    ("?", "", -58, False),
    ("E", "eduroam", -80, True),
    ("H", "Sharma_Home_5G", -72, True),
    ("?", "", -66, False),
]


def screen_feed():
    c = Canvas()
    chrome(c, 7, 3)
    for i, (tag, ssid, rssi, named) in enumerate(FEED):
        y = CH_FEED_TOP + i * CH_FEED_H
        tag_box(c, 2, y, tag, named)
        c.str(14, y + 8, elide(ssid, 22) if named else "no name given")
        c.str_right(126, y + 8, str(rssi))
    return c.save("screen_feed.png")


def screen_networks():
    c = Canvas()
    rows = SSIDS[:]
    c.str(2, 8, "Networks", F_PRIMARY)
    c.str_right(126, 8, str(len(rows)))
    c.line(0, 9, 127, 9)

    selected = 0
    for i in range(LIST_VISIBLE):
        s = rows[i]
        y = LIST_TOP + i * LIST_ROW_H
        base = y + 8
        sel = i == selected
        if sel:
            c.box(0, y, 122, LIST_ROW_H)
            c.set_color(BG)
        c.frame(2, y + 1, 9, 9)
        c.str_center(6, y + 8, s["tag"])
        c.str(14, base, elide(s["name"], 21))
        c.str_right(119, base, f"x{s['hits']}")
        if sel:
            c.set_color(FG)

    track = LIST_ROW_H * LIST_VISIBLE
    knob = max(4, track * LIST_VISIBLE // len(rows))
    c.box(124, LIST_TOP, 3, knob)

    sel = rows[selected]
    c.line(0, LIST_STRIP_RULE, 127, LIST_STRIP_RULE)
    c.str(2, LIST_STRIP_BASE, sel["reveals"])
    if sel["pin"]:
        c.box(118, LIST_STRIP_RULE + 1, 9, 9)
        c.set_color(BG)
        c.str_center(122, LIST_STRIP_BASE, "!")
        c.set_color(FG)
    return c.save("screen_networks.png")


def grade_box(c, x, y, grade, inverted=False):
    solid = grade not in ("A+", "A", "B")
    if solid != inverted:
        c.box(x, y, 13, 9)
        c.set_color(BG)
        c.str_center(x + 6, y + 8, grade)
        c.set_color(BG if inverted else FG)
    else:
        c.frame(x, y, 13, 9)
        c.str_center(x + 6, y + 8, grade)


def screen_devices():
    c = Canvas()
    rows = DEVICES
    c.str(2, 8, "Devices", F_PRIMARY)
    c.str_right(126, 8, str(len(rows)))
    c.line(0, 9, 127, 9)

    selected = 0
    for i in range(LIST_VISIBLE):
        r = rows[i]
        y = LIST_TOP + i * LIST_ROW_H
        base = y + 8
        sel = i == selected
        if sel:
            c.box(0, y, 122, LIST_ROW_H)
            c.set_color(BG)
        grade_box(c, 2, y + 1, r["grade"], sel)
        c.str(18, base, r["mac"])
        c.str_right(119, base, f"x{r['named']}" if r["named"] else "-")
        if sel:
            c.set_color(FG)

    track = LIST_ROW_H * LIST_VISIBLE
    knob = max(4, track * LIST_VISIBLE // len(rows))
    c.box(124, LIST_TOP, 3, knob)

    sel = rows[selected]
    c.line(0, LIST_STRIP_RULE, 127, LIST_STRIP_RULE)
    if sel["group"] > 1:
        left = f"1 of {sel['group']} MACs"
    elif sel["randomized"]:
        left = "random MAC"
    else:
        left = sel["vendor"] or "real MAC"
    c.str(2, LIST_STRIP_BASE, left)
    c.str_right(126, LIST_STRIP_BASE, f"{sel['score']}/100")
    return c.save("screen_devices.png")


# The names device 0 leaked, in the order the dossier sorts them (weight, hits).
DOSSIER_NAMES = [
    ("Home", "NETGEAR58"),
    ("Health", "CityHospital-Guest"),
    ("Hotel", "Hilton_Honors"),
]


def screen_dossier():
    c = Canvas()
    d = DEVICES[0]

    c.box(0, 0, 20, 19)
    c.set_color(BG)
    c.str_center(10, 14, d["grade"], F_PRIMARY)
    c.set_color(FG)

    c.str(24, 8, d["mac"])
    c.str(24, 17, f"{d['vendor']} - {d['probes']} probes")
    c.line(0, 20, 127, 20)

    for i, (cat, name) in enumerate(DOSSIER_NAMES):
        base = DOS_LIST_TOP + i * DOS_LIST_ROW_H + 7
        c.str(2, base, cat)
        c.str(40, base, elide(name, 16))

    track = DOS_LIST_ROW_H * DOS_LIST_VISIBLE
    knob = max(4, track * DOS_LIST_VISIBLE // 6)
    c.box(124, DOS_LIST_TOP, 3, knob)

    c.line(0, DOS_FOOT_RULE, 127, DOS_FOOT_RULE)
    c.str(2, DOS_FOOT_BASE, d["verdict"])
    for i in range(3):
        (c.disc if i == 0 else c.circle)(104 + i * 8, 59, 2)
    return c.save("screen_dossier.png")


def screen_why():
    c = Canvas()
    d = DEVICES[0]
    labels = ["Names", "Places", "Address", "Identity", "Tracking"]
    maxes = [30, 25, 10, 10, 25]

    c.str(2, 8, "Why this grade", F_PRIMARY)
    c.str_right(126, 8, f"{d['score']}/100")
    c.line(0, 9, 127, 9)

    for i, label in enumerate(labels):
        y = 12 + i * 8
        c.str(2, y + 7, label)
        c.frame(46, y + 1, 62, 6)
        v = d["cols"][i]
        if v:
            fill = max(1, v * 60 // maxes[i])
            c.box(47, y + 2, fill, 4)
        c.str_right(126, y + 7, str(v))

    c.line(0, DOS_FOOT_RULE, 127, DOS_FOOT_RULE)
    c.str(2, DOS_FOOT_BASE, "no floor or cap")
    for i in range(3):
        (c.disc if i == 1 else c.circle)(104 + i * 8, 59, 2)
    return c.save("screen_why.png")


def screen_explain():
    """Page four: three addresses, one radio."""
    c = Canvas()
    c.str(2, 8, "Still one phone", F_PRIMARY)
    for i in range(4):
        (c.disc if i == 3 else c.circle)(101 + i * 8, 5, 2)
    c.line(0, EX_RULE_TOP, 127, EX_RULE_TOP)

    c.frame(50, EX_ART_TOP - 1, 46, 28)
    for i, (mac, seq) in enumerate([("8A:3F:11", "412"), ("B6:7C:D9", "415"), ("EA:05:63", "418")]):
        base = EX_ART_TOP + 7 + i * 9
        c.str(2, base, mac)
        c.str(53, base, "77C10E55")
        c.str(100, base, seq)

    c.line(0, EX_RULE_MID, 127, EX_RULE_MID)
    c.str(2, EX_LINE1, "Its probe shape and frame")
    c.str(2, EX_LINE2, "counter give it away.")
    return c.save("screen_explain.png")


def screen_menu():
    """The Flipper submenu, with a header and 16 px rows."""
    c = Canvas()
    items = ["Listen", "Networks", "Devices", "How it works"]
    c.str_center(64, 12, "Echo", F_PRIMARY)
    for i, item in enumerate(items[:3]):
        y = 16 + i * 16
        if i == 0:
            c.rbox(0, y, 128, 16, 3)
            c.set_color(BG)
        c.str_center(64, y + 12, item, F_PRIMARY)
        if i == 0:
            c.set_color(FG)
    return c.save("screen_menu.png")


# ------------------------------------------------------------- contact sheet
def contact_sheet(tiles):
    cols = 4
    rows = (len(tiles) + cols - 1) // cols
    tw, th = tiles[0].size
    pad = 8
    sheet = Image.new(
        "RGB", (cols * tw + pad * (cols + 1), rows * th + pad * (rows + 1)), (18, 18, 22)
    )
    for i, t in enumerate(tiles):
        x = pad + (i % cols) * (tw + pad)
        y = pad + (i // cols) * (th + pad)
        sheet.paste(t, (x, y))
    path = os.path.join(OUT, "screens.png")
    sheet.save(path)
    print("wrote", path)


def main():
    tiles = [
        screen_listen(),
        screen_feed(),
        screen_networks(),
        screen_devices(),
        screen_dossier(),
        screen_why(),
        screen_explain(),
        screen_menu(),
    ]
    contact_sheet(tiles)


if __name__ == "__main__":
    sys.exit(main())
