# Modding Pac-Man

Make your own version of Pac-Man with new pictures, sounds and a new title
logo. No programming and no special tools, just an image editor and (for
sounds) any program that saves WAV files.

The original game is never changed: mods are switched on and off in the
launcher (**Mods**) or the game (**OPTIONS > MODS**), and **None** is always
the original.

---

## 1. Make your first mod (5 minutes)

1. In the launcher, click **Mods**, then **Dump textures**.
   This saves every picture of the game as its own PNG file, enlarged 4
   times, in the original colors, into a new mod folder (**My Mod 1**, then
   **My Mod 2**...) in the `mods` folder next to the game. It picks that mod
   and opens its folder.
2. Open any file in its `graphics` folder in your image editor (Krita, GIMP,
   Paint.NET, Aseprite, Photoshop...), paint it, save it (same name, PNG).
3. Play. (Already in the game? In **OPTIONS > MODS** pick **NONE** and then
   your mod again to see your changes.)

For power users there is also a script that does the same, with a choice of
size: `python tools/make_mod_template.py pacman.nes "build/mods/My Mod" --scale 8`.

That's it. Everything below is reference.

---

## 2. What's in a mod

A mod is a folder inside `mods/` next to `PacManRecomp.exe`. Everything in it
is optional; anything missing stays original.

```
mods/
  My Mod/
    mod.txt        name, author and description shown in the MODS menu
    preview.png    small picture shown in the MODS menu
    graphics/      the pictures (section 3), plus logo.png (section 4)
    sounds/        the sounds (section 5)
```

`mod.txt` looks like this (all three lines are optional):

```
name = My Mod
author = Your Name
description = Pac-Man in space.
```

The launcher's Mods page shows the whole description. The game's own MODS
menu has room for two lines of 28 letters (about 50 letters with the line
break); anything longer is cut off with "...". The name shows up to 13
letters and the author up to 11 there (also cut off with "...").

You can have as many mods as you like; the MODS menu lists them all. To share
a mod, zip its folder; to install one, unzip it into `mods/`.

---

## 3. Pictures (`graphics/`)

### Sizes

Every picture replaces one thing in the game. Draw it **any size you like**,
as long as it keeps the original's **shape** (most are square). The game
scales everything to fit. Bigger = more detail. The starter pictures are 4x
the original:

| Original | Starter (4x) | What |
|---|---|---|
| 32 x 32 | 128 x 128 | the giant Pac-Man |
| 16 x 16 | 64 x 64 | Pac-Man, ghosts, fruit, scores, score-column icons, cutscene pictures |
| 8 x 8 | 32 x 32 | letters, numbers, dots |
| 168 x 216 | 672 x 864 | the maze |

Use **transparent** pixels (not white or black) for "nothing here", so the
maze and backgrounds show through.

### The files

| Folder / file | What it is |
|---|---|
| `pacman/closed.png` | Pac-Man with his mouth shut (all directions) |
| `pacman/left+right_1.png`, `left+right_2.png` | moving left **and** right (drawn facing left; flipped for right): mouth half open, wide open |
| `pacman/down+up_1.png`, `down+up_2.png` | moving down **and** up (drawn facing down; flipped for up) |
| `pacman/right_*.png`, `up_*.png` | *optional*, see "Mirrored pictures" below |
| `pacman/closed_left.png`, `closed_right.png`, `closed_up.png`, `closed_down.png` | *optional*: the closed mouth when heading that way (otherwise `closed.png` is used for all four) |
| `pacman/death_1.png` ... `death_10.png` | the death animation, in order (10 is the final pop) |
| `ghosts/blinky/`, `pinky/`, `inky/`, `clyde/` | each ghost: `left+right_1/2` (drawn looking left; flipped for right), `down_1/2`, `up_1/2` (the eyes show where it's going; 1 and 2 are the wiggling feet) |
| `ghosts/frightened/blue_1.png`, `blue_2.png` | a blue (edible) ghost |
| `ghosts/frightened/white_1.png`, `white_2.png` | the white flash when it's about to recover |
| `ghosts/eyes/left+right.png`, `down.png`, `up.png` | eaten ghost's eyes going home |
| `fruit/cherry.png` ... `fruit/key.png` | the bonus fruit in the maze (cherry, strawberry, orange, apple, melon, galaxian, bell, key) |
| `scores/100.png` ... `scores/5000.png` | the points that pop up (200-1600 for ghosts; 100, 300, 500, 700, 1000, 2000, 3000, 5000 for fruit) |
| `hud/fruit_*.png` | the small fruit icons in the score column |
| `hud/lives.png` | the lives icon (little Pac-Man) |
| `font/A.png` ... `font/Z.png`, `font/0.png` ... `font/9.png` | letters and numbers (plus `dash`, `period`, `cursor`, `copyright`, `exclamation`), also used for READY!, PLAYER ONE/TWO and GAME OVER |
| `intermission/big_pacman_closed.png`, `big_pacman_open.png` | the giant Pac-Man in the first cutscene (32 x 32) |
| `intermission/blinky_torn_*.png`, `snag_*.png`, `tear_*.png` | Blinky snagging and tearing his cloak on a nail (second cutscene) |
| `intermission/blinky_patched_*.png`, `cloth.png` | Blinky's patched body and the dragged cloth (third cutscene) |
| `namco_logo.png` | the red "namco" on the title screen (72 x 8) |
| `dot.png`, `power_pellet.png` | the dots and the big flashing power pellets |
| `maze.png` | the maze walls |
| `maze_flash.png` | the maze when it flashes after a level (optional: made from `maze.png`) |
| `logo.png` | the title logo (section 4) |

### Mirrored pictures

Pictures named `left+right` or `down+up` are used for **both** directions: the
game flips them for the other one. So a hat painted once shows whichever way
Pac-Man faces. Want a side to look different (say, an eye patch that stays on
one eye)? Add your own `right_1.png` / `right_2.png` (or `up_1.png` /
`up_2.png`, drawn facing up) next to them and those are used for that
direction instead.

### Things to know

- **Letters and numbers: paint them white.** The game colors them itself
  (white, red, blue...) wherever they appear.
- **Paint every picture as a whole.** Each file shows exactly as painted, and
  you can paint anywhere in its square, even outside the original's outline
  (a hat, a cape...), including parts that are empty in the original (like
  the top of the later death pictures).
- **The maze picture is just the walls.** Dots and power pellets are
  separate (`dot.png`, `power_pellet.png`) because they disappear when eaten.
  Keep your walls where the original walls are: the maze's paths can't move.

### Existing HD packs

Packs made for the **Mesen** emulator (a folder with `hires.txt`) work as a
mod too: put the folder in `mods/` and pick it in the MODS menu.

---

## 4. Title logo

`graphics/logo.png` (any size, transparency allowed) replaces the PAC-MAN
logo on the title and options screens. It is fitted into the logo's space
(about 26 wide by 6 tall) keeping its proportions, and shown at its own
resolution, so bigger pictures stay sharp.

---

## 5. Sounds (`sounds/`)

Put WAV files named after the sound into `sounds/`. Any sound without a file
keeps the original. The original keeps running silently, so the game's timing
never changes: keep your sounds about as long as the originals.

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

Matching the length matters most for `start` (play begins when the original
ends), `death` (the melt animation is about 2.2 s) and `intermission` (the
cutscene runs on the original's timing). Dots can be eaten about 7 times a
second, so keep `dot` very short. Loops can be any length; make them loop
cleanly.

Format: uncompressed PCM WAV, 8- or 16-bit, mono or stereo, any sample rate.
`start`, `intermission` and `music` follow *OPTIONS > AUDIO > MUSIC*; the rest
follow *SOUND FX*.

---

## 6. Troubleshooting

- **My change doesn't show:** pick NONE and then your mod again in the MODS
  menu, or restart the game. The game rebuilds the mod each time it's picked.
- **A picture looks squashed:** it doesn't have the original's shape (see
  Sizes). Pac-Man, ghosts and fruit are square; the maze is 7 wide by 9 tall.
- **Something has a box around it:** that area should be transparent.
- **The `.hdcache` folder** inside `graphics/` is made by the game; you can
  delete it any time.

The older way (loose `sounds/`, `logo.png` or an `hdpack/` folder next to the
exe) still works when the MODS menu is set to NONE.
