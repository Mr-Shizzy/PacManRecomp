#!/usr/bin/env python3
"""
make_hd_template.py - make a starter HD texture pack for Pac-Man that you can
paint over.

It reads the game's graphics from your own ROM and writes:

  <out>/hires.txt   the "map" that tells the game which picture replaces
                    which tile (you normally never edit this)
  <out>/tiles.png   every graphic tile of the game, enlarged, in gray shades,
                    ready to repaint in any image editor

Usage (from the PacManRecomp folder):

  python tools/make_hd_template.py pacman.nes hdpack --scale 4

--scale 2 makes each 8x8 tile 16x16 pixels, 4 makes it 32x32 (the default),
8 makes it 64x64. No extra Python packages are needed.
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

# Gray shades for the NES's 4 tile colors; color 0 is transparent (the
# game's background shows through).
SHADES = {
    0: (0, 0, 0, 0),
    1: (85, 85, 85, 255),
    2: (170, 170, 170, 255),
    3: (255, 255, 255, 255),
}
TILES_PER_ROW = 16


def write_png(path, w, h, rgba):
    def chunk(typ, data):
        body = typ + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("out")
    ap.add_argument("--scale", type=int, default=4, choices=(2, 3, 4, 5, 6, 7, 8))
    a = ap.parse_args()

    rom = Path(a.rom).read_bytes()
    if rom[:4] != b"NES\x1a":
        sys.exit("That file is not an NES ROM (.nes).")
    prg = rom[4] * 16384
    chr_ = rom[16 + prg:16 + prg + rom[5] * 8192]
    tiles = len(chr_) // 16
    if not tiles:
        sys.exit("This ROM has no graphics tiles.")

    s = a.scale
    cell = 8 * s
    w = TILES_PER_ROW * cell
    h = (tiles + TILES_PER_ROW - 1) // TILES_PER_ROW * cell
    px = bytearray(w * h * 4)
    lines = [
        "# Pac-Man HD pack (made by make_hd_template.py)",
        "<ver>103",
        f"<scale>{s}",
        "<img>tiles.png",
    ]
    for t in range(tiles):
        ox, oy = (t % TILES_PER_ROW) * cell, (t // TILES_PER_ROW) * cell
        for y in range(8):
            lo, hi = chr_[t * 16 + y], chr_[t * 16 + 8 + y]
            for x in range(8):
                c = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1)
                for dy in range(s):
                    for dx in range(s):
                        i = ((oy + y * s + dy) * w + ox + x * s + dx) * 4
                        px[i:i + 4] = bytes(SHADES[c])
        # tile index (hex), any palette, position in the sheet, brightness 1,
        # default tile = used whatever colors the game gives it
        lines.append(f"<tile>0,{t:X},FFFFFFFF,{ox},{oy},1,Y")

    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    write_png(out / "tiles.png", w, h, px)
    (out / "hires.txt").write_text("\n".join(lines) + "\n")
    print(f"Wrote {out}/hires.txt and {out}/tiles.png "
          f"({tiles} tiles, {cell}x{cell} px each, sheet {w}x{h}).")


if __name__ == "__main__":
    main()
