# Modding Pac-Man

Everything here is optional and off unless the files are present. Put them
next to `PacManRecomp.exe`.

```
PacManRecomp.exe
hdpack/          HD texture pack (hires.txt + PNGs)
sounds/          replacement sound effects / music (*.wav)
logo.png         replacement title logo
```

## HD texture packs

An HD texture pack swaps the game's blocky pixel art for your own
higher-resolution drawings, while the game plays exactly the same.

### How it works (the 30-second version)

Everything on the NES screen is built from tiny **8x8-pixel squares called
tiles**: letters, bits of maze, pieces of the logo, frames of Pac-Man and the
ghosts. Pac-Man has **512 tiles**. An HD pack is:

- **`tiles.png`**: one big picture holding a bigger version of every tile,
  in a fixed grid.
- **`hires.txt`**: a "map" telling the game which square of `tiles.png`
  replaces which tile. You normally never touch it.

### Quick start

1. Install Python (free, from python.org) if you don't have it.
2. In the `PacManRecomp` folder, run:
   ```bash
   python tools/make_hd_template.py pacman.nes build/hdpack --scale 4
   ```
   This makes a **starter pack** in `build/hdpack/` (next to
   `PacManRecomp.exe`): every tile, 4x bigger, in gray shades.
3. Start the game. It now looks gray: the starter pack is working.
4. Open `build/hdpack/tiles.png` in any image editor that supports
   transparency (Krita, GIMP, Paint.NET, Aseprite, Photoshop...).
5. Paint over the tiles, save as PNG with the **same name**, restart the game.

The pack is on by default. To turn it off, or to use a pack stored somewhere
else, open the launcher: *Settings > Display > HD texture pack*.

### The tile sheet

| Scale | Each tile | Whole `tiles.png` |
|---|---|---|
| `--scale 2` | 16 x 16 px | 256 x 512 px |
| `--scale 4` (recommended) | 32 x 32 px | 512 x 1024 px |
| `--scale 8` | 64 x 64 px | 1024 x 2048 px |

- Tiles sit **16 per row**, in order. Tile number *N* is in column
  *N mod 16*, row *N / 16* (counting from 0 at the top left).
- **Top half (tiles 0-255): the background.** Letters, numbers, the PAC-MAN
  logo, the fruit icons, the maze walls, dots and power pellets.
- **Bottom half (tiles 256-511): the moving things.** Pac-Man's animation
  frames, the ghosts, their eyes, the fruit and the score pop-ups.
- Big pictures are made of several tiles side by side (the logo is dozens).
  Paint them as one picture across the squares, but keep every piece inside
  its own square.

### Rules

- **Don't move, resize or reorder anything** in `tiles.png`, and keep the
  whole image the same size. The map points at exact positions.
- **Transparent pixels show the game's background.** Keep see-through areas
  transparent (not white) so sprites don't get boxes around them.
- **Starter-pack gray isn't always gray in the game.** Some tiles that look
  black in the game use a color the starter pack shows as gray; paint those
  black (or transparent).
- **One drawing per tile, used everywhere.** The four ghosts share the same
  tiles and only differ in color in the original, so one ghost drawing is
  used for all four. (Advanced: `hires.txt` can give a tile different
  pictures per color set; see the Mesen HD pack documentation.)
- Keep the file names `tiles.png` and `hires.txt`, in a folder of their own.

### Existing packs

Packs made for the **Mesen** emulator for this game drop straight in: copy
the pack's folder (the one with `hires.txt`) to `build/hdpack/`.

The port's own menus, level readout, pause menu and leaderboard are drawn on
top of the pack and never replaced by it.

## Sounds and music

Drop WAV files named after the sound into `sounds/`. Any sound without a
file keeps the original. The original keeps running silently, so the game's
timing never changes: keep replacements about as long as the originals
(especially `start`, which the game waits on before play begins).

| File | Replaces | Plays | Original length |
|---|---|---|---|
| `start.wav` | start jingle | once | 4.15 s |
| `extra_life.wav` | extra life | once | 1.6 s |
| `death.wav` | Pac-Man dying | once | 1.9 s |
| `dot.wav` | eating a dot ("wakka") | once per dot | 0.08 s |
| `fruit.wav` | eating a fruit | once | 0.43 s |
| `eat_ghost.wav` | eating a ghost | once | 0.48 s |
| `eyes.wav` | eyes returning home | loop | any (original cycle 0.22 s) |
| `fright.wav` | blue ghosts | loop | any (original cycle 0.13 s) |
| `siren.wav` | background siren (all three speeds) | loop | any (original cycle 0.33 s) |
| `intermission.wav` | intermission tune | once | 5.3 s |
| `pause.wav` | pause | once | 0.58 s |
| `music.wav` | *(new)* music during play | loop | any |

Lengths were measured from the game running at 60 frames per second.
Matching them matters most for `start` (play begins when the original ends),
`death` (the melt animation is about 2.2 s) and `intermission` (the cutscene
runs on the original's timing). Dots are eaten up to ~7 times a second, so
keep `dot` very short or it will overlap itself. Loops can be any length;
make them loop cleanly.

Format: uncompressed PCM WAV, 8- or 16-bit, mono or stereo, any sample rate.
`start`, `intermission` and `music` follow *Options > Audio > Music*; the rest
follow *Sound FX*.

## Title logo

A `logo.png` (any size, transparency allowed) replaces the PAC-MAN logo on
the title and options screens. It is fitted into the logo band (about 26:6)
keeping its proportions, and drawn at its own resolution, so larger images
stay sharp.
