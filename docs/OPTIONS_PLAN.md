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
| CONTROLS | Menu style: Classic / Modern (default Classic) � Rumble (default off) |
| EXTRAS | Pac-Man speed 1x/2x/3x · Ghost speed 1x/2x/3x |
| CHEATS | Infinite lives · Start level · Invincibility |
| RESET TO DEFAULT | YES / NO confirm |
| BACK | |

## Behavior

- **Menu style** applies to the title menu, the options screen and the pause
  exit prompt.
  - Classic: Select moves the cursor, Start picks (the original feel).
  - Modern: D-pad moves, A picks, B goes back; Left/Right changes values.
- Settings apply instantly and save automatically: display and volume in
  the runner's config.ini, the rest in pacman_options.ini next to the exe.
- Custom glyphs drawn in the game's font style: `:` `?` `%` and arrows.
- Labels and cursor in white as on the original title screen, values in
  the title palette's orange, section headers in the logo's salmon.

## Status

All items are implemented (2026-09-28):

| Item | How |
|---|---|
| Framework, title slot, freeze, navigation, save/load | `src/options.c`, title menu state `$3F=02` / `$48=FF`, idle timer `$87/$88` held |
| Stretch, filter, integer scale | runner `g_nes_config` + `nesrecomp_apply_video_settings()` |
| Inverse colors | whole-frame invert in `game_post_render` |
| Volume | runner `g_nes_config.volume` (live) |
| Echo | `nesrecomp_set_audio_filter()` 200 ms feedback delay |
| Music / SFX | `apu_set_mute_mask()` on channels owned by muted sound slots (music = slots 0-1, 13-14) |
| Pac-Man / ghost speed | scale the stage speed table `$9F-$B4` by 1.5 (pairs 0-3 Pac-Man, 6-10 ghosts); 2x/3x were unplayable |
| Show level | LEVEL + stage `$68`+1 in the HUD column, rows 11/13 |
| Pause menu | MAIN MENU (game-over script 0A with both players out of lives; keeps the high score) / LAUNCHER / BACK |
| Infinite lives | keep `$67`/`$77` at least 3 |
| Start level | set stage `$68`/`$78` while it reads FF at game start |
| Invincible | undo the collision's switch to the death script (`$3F` 04 -> 08) |
| Rumble | light while ghosts are blue (`$88` bits), hard 0.3 s burst on eating a ghost (script 06), medium through the death melt (script 08 with `$87` != 0); `nesrecomp_rumble()` |
| Title detection | flag `$48`=FF is shared with the attract-demo game, so also require the title's "PLAY" text in nametable 0 |
| Reset to default | restores both runner and Pac-Man settings |

## Order

1. Framework + title slot + empty sections + BACK
2. CONTROLS (Classic/Modern), since every menu depends on it
3. VIDEO, AUDIO (easy parts), RESET TO DEFAULT
4. Research items one by one: music/SFX, speeds, cheats
