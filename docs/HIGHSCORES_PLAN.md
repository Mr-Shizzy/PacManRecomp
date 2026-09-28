# Persistent high scores + top-10 leaderboard — plan

Status: implemented 2026-09-28 (`src/highscores.c`).

## Decisions (agreed 2026-09-28)

- **Opt-in:** `EXTRAS > HIGH SCORES  ON/OFF`, default **OFF**. Off = exactly
  the original game (HI-SCORE starts at 10000, nothing saved, no initials,
  no board).
- **Empty board:** 10 rows of `---` with score `0`.
- **What counts:** a game is recorded only if **no cheat and no speed option**
  was on at any point during it (infinite lives, invincible, start level > 1,
  Pac-Man 1.5x, ghosts 1.5x). A disqualified game shows a short note at game
  over instead of the initials entry.
- **HI-SCORE (in-game and title):** shows leaderboard #1 (0 on an empty
  board), updating live as you pass it, like the arcade.
- **Initials entry (arcade style):** after a qualifying GAME OVER. Three
  letters, the current one blinking.
  - Modern: Up/Down cycle A-Z, space, '.'; Right or A = next letter; Left or
    B = back; confirming the third letter saves.
  - Classic: Select cycles the letter, Start = next letter / save.
- **2 players:** both enter initials if both qualify (higher score first),
  each on their own controller.
- **Leaderboard screen:** the 10 rows scroll up the screen in the game's font
  and colors (rank, initials, score).
  - In the attract loop: title menu -> character intro -> **HIGH SCORES** ->
    computer-played demo -> back to the title.
  - Right after entering initials, with the new entry highlighted.
  - Start / A skips it.

## Storage

- `pacman_scores.ini` next to the exe (same place as the other settings), one
  line per rank: initials and score. Written only when the board changes.
- Never touched by `RESET TO DEFAULT` (that resets settings, not scores).

## How it works (research results)

- **Scores:** HI-SCORE `$61-$66`, current player `$70-$75`, other player
  `$80-$85`; one decimal digit per byte, least significant first, displayed
  with a trailing 0. The game raises HI-SCORE live when the current score
  passes it. With the option on, HI-SCORE is seeded from board #1 on the
  title/attract and drawn over the title's score bar (row 4, cols 12-18).
- **Final game over:** lives run out -> script `0A`. It is the last one when
  it is a 1-player game or the other player's lives (`$77`) are 0.
- **Freeze:** the main loop waits for the NMI to clear `$40`; re-arming it
  after every NMI holds the game still while sound and input keep running.
- **Attract:** the title loop never draws the maze, so the maze appearing
  with the demo flag FF marks the start of the computer-played demo; the
  board shows then (after the character intro and chase).
- **Player 2** reads controller 2 in 2-player games; their initials accept
  either controller so one pad is enough.
- **Reset:** `EXTRAS > RESET HIGH SCORES` (YES/NO, NO first), shown only
  while high scores are on.

## Tests

`tests/hs_attract.txt`, `hs_entry.txt`, `hs_cheat.txt`, `hs_2p.txt` (run
with `sh tests/run.sh <script> "HighScores = 1"`).
