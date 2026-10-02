# Tests

Headless regression scripts for the recompiled game. Each `*.txt` is a
NESRecomp input script (button presses, waits, RAM asserts, screenshots).

## Running

```bash
sh tests/run.sh boot_smoke.txt
sh tests/run.sh options_modern.txt "ModernMenus = 1"
```

Needs `build/PacManRecomp.exe` (built) and `pacman.nes` in the repo root.
Extra arguments become lines of the test's `pacman_options.ini`
(prefix with `config:` to write to `config.ini` instead).

Tests never touch your settings: `run.sh` runs an isolated copy of the game in
`tests/run/` with fresh config/score files, so `build/config.ini` and friends
are never read or written. Screenshots land in `tests/out/` (created
automatically). Both folders are gitignored. Scripts write
`SCREENSHOT tests/out/x.png`; `run.sh` turns that into an absolute path for the
current checkout, so they work from anywhere.

## Scripts

- `boot_smoke.txt` - boot, title screen, start a game, screenshots.
- `options_classic.txt` - classic (Select-button) options menu.
- `options_modern.txt` - modern options menu (run with `"ModernMenus = 1"`).
- `title_quit.txt` - quitting from the title screen.
- `pause_menu_yes.txt` - pause menu, confirming quit.
- `cheats_probe.txt` - cheat codes (level skip and friends).
- `hs_entry.txt` - high score name entry.
- `hs_2p.txt` - two-player high scores.
- `cap_death.txt` - losing a life.
- `cap_gameover.txt` - game over.
- `inter_probe.txt` - intermission cutscenes (mod screenshots).
- `startlevel_menu.txt` - start-level select, up to level 256.
- `hud_1p.txt` / `hud_2p.txt` - level HUD in 1- and 2-player games.
- `mods_menu_probe.txt` - mods menu.
- `starter_probe.txt` - starter mod loads and runs.
- `sounddump.txt` - dumps the game's own sounds (for modders).
- `rumble_death.txt` - gamepad rumble on death.
- `sound_pack_probe.txt` - replacement sound pack loading.
