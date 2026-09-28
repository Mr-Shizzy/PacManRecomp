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

Standard **Mesen HD Pack** format: a folder with `hires.txt` and its PNG
sheets. Put it in `hdpack/`, or pick any folder in the launcher under
*Settings > Display > HD texture pack*. Packs made for Mesen with this ROM
work as-is. `nesrecomp/tools/hdpack_gen.py` generates a starter pack from
the ROM's own tiles (every tile, upscaled, ready to repaint).

The in-game menus, level readout, pause prompt and leaderboard are drawn by
the port itself and are never covered by pack tiles.

## Sounds and music

Drop WAV files named after the sound into `sounds/`. Any sound without a
file keeps the original. The original keeps running silently, so the game's
timing never changes: keep replacements about as long as the originals
(especially `start`, which the game waits on before play begins).

| File | Replaces | Plays |
|---|---|---|
| `start.wav` | start jingle | once |
| `extra_life.wav` | extra life | once |
| `death.wav` | Pac-Man dying | once |
| `dot.wav` | eating a dot ("wakka") | once per dot |
| `fruit.wav` | eating a fruit | once |
| `eat_ghost.wav` | eating a ghost | once |
| `eyes.wav` | eyes returning home | loop |
| `fright.wav` | blue ghosts | loop |
| `siren.wav` | background siren (all three speeds) | loop |
| `intermission.wav` | intermission tune | once |
| `pause.wav` | pause | once |
| `music.wav` | *(new)* music during play | loop |

Format: uncompressed PCM WAV, 8- or 16-bit, mono or stereo, any sample rate.
`start`, `intermission` and `music` follow *Options > Audio > Music*; the rest
follow *Sound FX*.

## Title logo

A `logo.png` (any size, transparency allowed) replaces the PAC-MAN logo on
the title and options screens. It is fitted into the logo band (about 26:6)
keeping its proportions, and drawn at its own resolution, so larger images
stay sharp.
