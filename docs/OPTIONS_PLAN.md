# In-game Options menu — build plan

Goal: an OPTIONS menu that looks like it was always part of NES Pac-Man — same
font, colors, cursor and layout conventions as the original title screen.

## Title screen

```
1 PLAYER
2 PLAYERS      (both shifted up slightly)
OPTIONS
```

Choosing OPTIONS blanks everything below the PAC-MAN logo (player lines and
the Namco/Nintendo copyright text) and draws the options there. The logo and
score bar stay. The game is frozen underneath (no attract demo) while the
menu is open.

## Menu tree

| Section | Items |
|---|---|
| VIDEO | Stretch · Filter · Integer scale · Inverse colors |
| AUDIO | Volume · Music on/off · SFX on/off · Echo |
| CONTROLS | Menu style: Classic / Modern (default Classic) |
| EXTRAS | Pac-Man speed 1x/2x/3x · Ghost speed 1x/2x/3x |
| CHEATS | Infinite lives · Start level · Invincibility |
| RESET TO DEFAULT | YES / NO confirm |
| BACK | |

## Behavior

- **Menu style** applies to the title menu, the options screen and the pause
  exit prompt.
  - Classic: Select moves the cursor, Start picks (the original feel).
  - Modern: D-pad moves, A picks, B goes back; Left/Right changes values.
- Settings apply instantly and save automatically (config.ini, Pac-Man
  section).
- Custom glyphs drawn in the game's font style: `:` `?` `%` and arrows.
- Values in white, cursor in yellow, as on the original title screen.

## Work items

Easy (runner settings / RAM):
- Framework: title slot, freeze, navigation, drawing, save/load
- Stretch, filter, integer scale (live runner settings)
- Inverse colors (present-time palette)
- Volume, echo (audio output stage)
- Infinite lives (RAM)
- Reset to default

Needs disassembly research first:
- Title cursor logic (to add the third slot cleanly)
- Music vs SFX mute (find where the sound engine starts each)
- Pac-Man speed and ghost speed (find the movement-speed tables/routines)
- Invincibility (find the ghost collision check)
- Start level (find where the stage is set at game start)

## Order

1. Framework + title slot + empty sections + BACK
2. CONTROLS (Classic/Modern), since every menu depends on it
3. VIDEO, AUDIO (easy parts), RESET TO DEFAULT
4. Research items one by one: music/SFX, speeds, cheats
