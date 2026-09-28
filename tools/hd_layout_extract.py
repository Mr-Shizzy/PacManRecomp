#!/usr/bin/env python3
"""
hd_layout_extract.py - build tools/hd_layout.json (the beginner graphics
layout) from capture logs.

Captures come from running the game with PACMAN_HD_CAPTURE=<file> (see
src/capture.c) through the attract loop, levels 1-13 and a death. This script
names what was captured and records, for every graphic file, which 8x8 tiles
(with flips) make it up and which color sets it is drawn with.

Usage: python tools/hd_layout_extract.py tests/out/cap tools/hd_layout.json
Then:  python tools/hd_layout_gen.py  (writes src/hd_layout.h)
"""
import glob
import json
import os
import re
import sys
from collections import defaultdict

cap_dir, out = sys.argv[1], sys.argv[2]

# ---- read captures ----------------------------------------------------------
sprites = defaultdict(set)       # pieces tuple -> {palette keys}
bg_pals = defaultdict(set)       # bg tile -> {palette keys}
nametable = None
for fn in sorted(glob.glob(os.path.join(cap_dir, "*.txt"))):
    for line in open(fn):
        if line.startswith("S "):
            m = re.match(r"S pal=(\w+) n=(\d+)(.*?) \|", line)
            pieces = tuple(tuple(p.split(",")) for p in m[3].split())
            pieces = tuple((int(a), int(b), c, d) for a, b, c, d in pieces)
            sprites[pieces].add(m[1])
        elif line.startswith("B "):
            m = re.match(r"B tile=(\w+) pal=(\w+)", line)
            bg_pals[m[1]].add(m[2])
        elif line.startswith("N ") and nametable is None:
            nametable = line.split(" | ")[0].split()[1:]


def P(*spec):
    """Pieces from 'TT' or 'TTf' strings in 2x2 order (f = H, V or B=both)."""
    out = []
    for i, s in enumerate(spec):
        t, f = s[:2], s[2:]
        out.append((8 * (i % 2), 8 * (i // 2), t,
                    ("H" if f in ("H", "B") else "-") + ("V" if f in ("V", "B") else "-")))
    return tuple(out)


def pals_of(pieces, body=None):
    ps = sorted(sprites.get(pieces, ()))
    if body:
        ps = [p for p in ps if p[6:8] == body]
    if not ps:
        sys.exit(f"no capture for {pieces} (body {body})")
    return ps


graphics = []


def sprite(name, pieces, pals, mirror=None, wild=False):
    graphics.append({"name": name, "type": "sprite", "w": 16, "h": 16,
                     "pieces": [list(p) for p in pieces], "pals": pals,
                     "mirror": mirror, "wild": wild})


# ---- Pac-Man ------------------------------------------------------------------
PAC = {
    "closed":  P("00", "00H", "00V", "00B"),
    "right_1": P("02", "01", "02V", "01V"),
    "right_2": P("06", "05", "06V", "05V"),
    "left_1":  P("01H", "02H", "01B", "02B"),
    "left_2":  P("05H", "06H", "05B", "06B"),
    "down_1":  P("03", "03H", "04", "04H"),
    "down_2":  P("07", "07H", "08", "08H"),
    "up_1":    P("04V", "04B", "03V", "03B"),
    "up_2":    P("08V", "08B", "07V", "07B"),
}
MIRROR = {"left_1": ("right_1", "h"), "left_2": ("right_2", "h"),
          "up_1": ("down_1", "v"), "up_2": ("down_2", "v")}
for n, p in PAC.items():
    m = MIRROR.get(n)
    sprite("pacman/" + n, p, pals_of(p), {"of": "pacman/" + m[0], "axis": m[1]} if m else None, wild=True)

DEATH = [P("09", "09H", "0A", "0AH"), P("0B", "0BH", "0C", "0CH")] + \
        [P("4C", "4C", t, t + "H") for t in ("0D", "0E", "0F", "10", "11", "12")] + \
        [P("14", "15", "16", "17")]
for i, p in enumerate(DEATH):
    sprite(f"pacman/death_{i + 1}", p, pals_of(p), wild=True)

# ---- ghosts -------------------------------------------------------------------
BODY = {
    "down_1":  P("18", "18H", "19", "19H"),
    "down_2":  P("18", "18H", "1A", "1AH"),
    "right_1": P("1B", "1C", "1D", "1F"),
    "right_2": P("1B", "1C", "1E", "20"),
    "left_1":  P("1CH", "1BH", "1FH", "1DH"),
    "left_2":  P("1CH", "1BH", "20H", "1EH"),
    "up_1":    P("21", "21H", "22", "22H"),
    "up_2":    P("21", "21H", "23", "23H"),
}
GMIRROR = {"left_1": "right_1", "left_2": "right_2"}
GHOSTS = {"blinky": "06", "pinky": "33", "inky": "21", "clyde": "17"}   # body color
for g, body in GHOSTS.items():
    for n, p in BODY.items():
        m = GMIRROR.get(n)
        sprite(f"ghosts/{g}/{n}", p, pals_of(p, body),
               {"of": f"ghosts/{g}/{m}", "axis": "h"} if m else None)

FRIGHT = {"1": P("24", "24H", "25", "25H"), "2": P("24", "24H", "26", "26H")}
for n, p in FRIGHT.items():
    ps = pals_of(p)
    sprite(f"ghosts/frightened/blue_{n}", p, [x for x in ps if x[2:4] == "11"])
    sprite(f"ghosts/frightened/white_{n}", p, [x for x in ps if x[2:4] == "20"])

EYES = {"down": P("27", "27H", "4C", "4C"), "right": P("28", "29", "2A", "2B"),
        "left": P("29H", "28H", "2BH", "2AH"), "up": P("2C", "2CH", "2D", "2DH")}
for n, p in EYES.items():
    sprite(f"ghosts/eyes/{n}", p, pals_of(p),
           {"of": "ghosts/eyes/right", "axis": "h"} if n == "left" else None, wild=True)

# ---- fruit and scores ---------------------------------------------------------------
def shape_with(tile0):
    for pieces in sprites:
        if len(pieces) == 4 and pieces[0][2] == tile0 and pieces[0][0] == 0 and pieces[0][1] == 0:
            return pieces
    sys.exit(f"no captured shape starting with tile {tile0}")


FRUIT_TILE = {"cherry": None, "strawberry": None, "orange": None, "apple": None,
              "melon": None, "galaxian": None, "bell": None, "key": None}
# Fruit sprites: the eight 4-tile shapes drawn with the fruit/Clyde slot that
# are not ghost bodies, in their order in the sprite table.
ghost_tiles = {p[2] for b in BODY.values() for p in b}
fruit = sorted((pc for pc in sprites if len(pc) == 4 and all(p[3] == "--" for p in pc)
                and pc[0][2] not in ghost_tiles and int(pc[0][2], 16) >= 0x30
                and all(pk[2:4] not in ("27", "11", "20") for pk in sprites[pc])
                and any(pk[4:8] in ("2017",) for pk in sprites[pc])),
               key=lambda pc: pc[0][2])
for name, pc in zip(FRUIT_TILE, fruit):
    sprite("fruit/" + name, pc, sorted(sprites[pc]), wild=True)

# Score pop-ups (100 = cherry, 200-1600 = ghosts). They share their right
# half ("00" tiles 2F/31) except 1600.
SCORES = {"100": P("2E", "2F", "30", "31"), "200": P("32", "2F", "33", "31"),
          "400": P("36", "2F", "37", "31"), "800": P("3C", "2F", "3D", "31"),
          "1600": P("42", "3F", "43", "41")}
for n, p in SCORES.items():
    sprite("scores/" + n, p, pals_of(p), wild=True)

# ---- background --------------------------------------------------------------------
def bg(name, w, h, tiles, pals, tint=False, wild=False):
    pieces = [[8 * (i % (w // 8)), 8 * (i // (w // 8)), t, "--"] for i, t in enumerate(tiles)]
    seen = sorted(bg_pals.get(tiles[0], ()))
    graphics.append({"name": name, "type": "bg", "w": w, "h": h, "pieces": pieces,
                     "pals": pals, "tint": tint, "wild": wild,
                     "preview": seen[0] if seen else "0F162606"})


for i, name in enumerate(["cherry", "strawberry", "orange", "apple", "melon", "galaxian", "bell", "key"]):
    base = 0x60 + i * 4
    bg("hud/fruit_" + name, 16, 16, [f"{base + k:02X}" for k in range(4)], [], wild=True)
bg("hud/lives", 16, 16, ["3C", "3D", "3E", "3F"], [], wild=True)
bg("dot", 8, 8, ["03"], [], wild=True)
bg("dot_alt", 8, 8, ["09"], [], wild=True)
graphics[-1]["mirror"] = {"of": "dot", "axis": "n"}     # falls back to dot.png as-is
bg("power_pellet", 8, 8, ["01"], [], wild=True)

FONT = {chr(c): f"{c:02X}" for c in list(range(0x41, 0x5B)) + list(range(0x30, 0x3A))}
FONT.update({"dash": "3A", "period": "5B", "cursor": "5C", "copyright": "5D"})
# All text uses the same few color sets; give every character all of them
# (some letters never appeared on screen during capture).
font_pals = sorted(set().union(*(bg_pals.get(t, set()) for t in FONT.values())))
for name, t in FONT.items():
    bg("font/" + name, 8, 8, [t], font_pals, tint=True)

NES_PALETTE = [int(x, 16) & 0xFFFFFF for x in re.findall(
    r"0x[0-9A-Fa-f]{8}", open(os.path.join(os.path.dirname(__file__), "..", "..", "nesrecomp",
                                           "runner", "src", "ppu_renderer.c")).read().split(
    "g_nes_palette[64] = {")[1].split("};")[0])]

layout = {
    "scale": 4,
    "nes_palette": ["%06X" % c for c in NES_PALETTE],
    "maze": {
        "x": 8, "y": 16, "cols": 21, "rows": 27,
        "cond_tile": "1F", "pal_normal": "0F110F27", "pal_flash": "0F200F27",
        "walls": [f"{t:02X}" for t in list(range(0x10, 0x20)) + [0x21, 0x22, 0x2C]],
        "tiles": [[nametable[r * 32 + c] for c in range(1, 22)] for r in range(2, 29)],
    },
    "graphics": graphics,
}
json.dump(layout, open(out, "w"), indent=1)
print(f"{len(graphics)} graphics written to {out}")
