#!/usr/bin/env python3
"""
make_mod_template.py - make a ready-to-paint mod for Pac-Man.

It reads the game's graphics from your own ROM and writes a complete mod
folder: every graphic as its own PNG, enlarged, in the game's real colors,
plus a mod.txt, a preview.png and a sounds/ folder with a list of names.

Usage (from the PacManRecomp folder):

  python tools/make_mod_template.py pacman.nes "build/mods/My Mod"

Options:  --scale N   how much bigger than the original (default 4; 2-8)

Then start the game, open OPTIONS > MODS, pick "My Mod", and start painting
the files in "build/mods/My Mod/graphics". Changes show the next time you
pick the mod (or restart the game).

Running it again on an existing mod only adds missing pictures (e.g. ones
added by a game update); it never overwrites yours.

Right-facing (and Pac-Man's up-facing) pictures are made automatically by
mirroring; add e.g. pacman/right_1.png yourself only if you want it different.
"""
import argparse
import json
import struct
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent


def write_png(path, w, h, rgba):
    def chunk(typ, data):
        body = typ + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


class Canvas:
    def __init__(self, w, h, fill=(0, 0, 0, 0)):
        self.w, self.h = w, h
        self.px = bytearray(bytes(fill) * (w * h))

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[(y * self.w + x) * 4:(y * self.w + x) * 4 + 4] = bytes(c)

    def scaled(self, s):
        out = Canvas(self.w * s, self.h * s)
        for y in range(out.h):
            row = (y // s) * self.w
            for x in range(out.w):
                i = (row + x // s) * 4
                out.px[(y * out.w + x) * 4:(y * out.w + x) * 4 + 4] = self.px[i:i + 4]
        return out

    def save(self, path, scale):
        if path.exists():
            return                      # never overwrite the modder's pictures
        c = self.scaled(scale) if scale > 1 else self
        write_png(path, c.w, c.h, c.px)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("mod_dir")
    ap.add_argument("--scale", type=int, default=4, choices=range(2, 9))
    a = ap.parse_args()

    rom = Path(a.rom).read_bytes()
    if rom[:4] != b"NES\x1a":
        sys.exit("That file is not an NES ROM (.nes).")
    chr_ = rom[16 + rom[4] * 16384:]
    L = json.loads((HERE / "hd_layout.json").read_text())
    nes = [tuple(int(h[i:i + 2], 16) for i in (0, 2, 4)) + (255,) for h in L["nes_palette"]]
    s = a.scale
    out = Path(a.mod_dir)
    gfx = out / "graphics"

    def color(pal, v):
        return nes[(int(pal, 16) >> ((3 - v) * 8)) & 0x3F]

    def draw_tile(cv, x0, y0, tile, sprite, flags, colors):
        t = tile + (256 if sprite else 0)
        for y in range(8):
            lo, hi = chr_[t * 16 + y], chr_[t * 16 + 8 + y]
            for x in range(8):
                v = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1)
                if v and colors[v]:
                    px = 7 - x if "H" in flags else x
                    py = 7 - y if "V" in flags else y
                    cv.put(x0 + px, y0 + py, colors[v])

    written = 0
    for g in L["graphics"]:
        if g.get("mirror") or g.get("also") or g.get("optional"):
            continue                    # made from its partner / another entry's file
        sprite = g["type"] == "sprite"
        if g.get("tint"):
            white = (255, 255, 255, 255)
            colors = [None, white, white, white]
        else:
            pal = g["pals"][0] if g["pals"] else g.get("preview", "0F162606")
            colors = [None] + [color(pal, v) for v in (1, 2, 3)]
        cv = Canvas(g["w"], g["h"])
        for dx, dy, t, fl in g["pieces"]:
            draw_tile(cv, dx, dy, int(t, 16), sprite, fl, colors)
        cv.save(gfx / (g["name"] + ".png"), s)
        written += 1

    # The maze: walls only (dots and pellets are separate: they disappear).
    m = L["maze"]
    walls = set(int(t, 16) for t in m["walls"])
    for name, pal in (("maze", m["pal_normal"]), ("maze_flash", m["pal_flash"])):
        cv = Canvas(m["cols"] * 8, m["rows"] * 8, (0, 0, 0, 255))
        colors = [None] + [color(pal, v) for v in (1, 2, 3)]
        for r, row in enumerate(m["tiles"]):
            for c, t in enumerate(row):
                if int(t, 16) in walls:
                    draw_tile(cv, c * 8, r * 8, int(t, 16), False, "--", colors)
        cv.save(gfx / (name + ".png"), s)
        written += 1
        if name == "maze":
            cv.save(out / "preview.png", 1)

    # The title logo (the PAC-MAN box and TM) as graphics/logo.png.
    lg = L.get("logo")
    if lg:
        cv = Canvas(len(lg["tiles"][0]) * 8, len(lg["tiles"]) * 8)
        for r, row in enumerate(lg["tiles"]):
            for c, t in enumerate(row):
                colors = [None] + [color(lg["pals"][r][c], v) for v in (1, 2, 3)]
                draw_tile(cv, c * 8, r * 8, int(t, 16), False, "--", colors)
        cv.save(gfx / "logo.png", s)
        written += 1

    (out / "sounds").mkdir(parents=True, exist_ok=True)
    (out / "sounds" / "README.txt").write_text(
        "Put replacement sounds here as WAV files named:\n"
        "start extra_life death dot fruit eat_ghost eyes fright siren\n"
        "intermission pause music (e.g. start.wav). See docs/MODDING.md.\n")
    if not (out / "mod.txt").exists():
        (out / "mod.txt").write_text(
            f"name = {out.name}\nauthor = \ndescription = My Pac-Man mod.\n")
    print(f"Wrote {written} pictures to {gfx} ({s}x).")


if __name__ == "__main__":
    main()
