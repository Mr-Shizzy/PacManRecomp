# PacManRecomp

Native Windows static recompilation of **Pac-Man (USA, Namco 1993)** using
[NESRecomp](https://github.com/mstan/nesrecomp). No ROM or ROM-derived code is
included; supply your own.

Expected ROM: 24,592 bytes, CRC32 `9E4E9CC2` (excluding the 16-byte iNES header).

## Layout

```
C:\Recomp\PacMan\
  nesrecomp\      NESRecomp checkout (branch pacman-local carries local fixes)
  recomp-ui\      launcher / settings UI
  PacManRecomp\   this repo
```

## Build

```bash
# 1. Build the recompiler (once, or after recompiler changes)
cmake -S ../nesrecomp/recompiler -B ../nesrecomp/build/recompiler -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Release
cmake --build ../nesrecomp/build/recompiler

# 2. Regenerate C from your ROM (place it here as pacman.nes)
../nesrecomp/build/recompiler/NESRecomp.exe pacman.nes --game game.toml

# 3. Build the game
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Modding

HD texture packs, replacement sounds/music and a custom title logo: see
[docs/MODDING.md](docs/MODDING.md).

## Testing

`tests/*.txt` are NESRecomp input scripts (headless). Run them with
`tests/run.sh`, which copies the fresh build into `tests/run/` so tests never
touch your own settings next to `build/PacManRecomp.exe`:

```bash
sh tests/run.sh options_classic.txt
sh tests/run.sh options_modern.txt "ModernMenus = 1"
```

Extra arguments become lines of the test's `pacman_options.ini`.
Screenshots land in `tests/out/`.
