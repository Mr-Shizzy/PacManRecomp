# Persistent high scores + top-10 leaderboard — plan

Status: approved 2026-09-28; in progress.

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

## Research needed before coding

1. Score RAM: where each player's score and the HI-SCORE live (likely BCD
   digits), and how the game updates and draws them, so HI-SCORE can be
   seeded from the board and kept in sync.
2. Game-over flow per player in 1P and 2P games (script 0A path), to know
   when a player's final score is final and to hold the game there while
   initials are entered.
3. The attract sequence (title loop script 04 sub-steps) to insert the
   leaderboard between the character intro and the demo game.
4. Which controller player 2 uses in a 2-player game.

- **Reset:** `EXTRAS > RESET HIGH SCORES` with a YES/NO confirm (NO
  first), shown when high scores are on.
