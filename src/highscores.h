/*
 * src/highscores.h — persistent top-10 leaderboard (Extras > HIGH SCORES).
 */
#pragma once
#include <stdint.h>

/* Load the saved board (game_on_init). */
void hs_init(void);

/* Clear the board to ten "---  0" rows and save it. */
void hs_reset(void);

/* Per-frame logic, first thing in game_on_frame(). Returns 1 while the
 * leaderboard owns the frame (game frozen, pad in use): the caller should
 * skip the other menus' input that frame. */
int hs_on_frame(void);

/* Keep the game frozen while a board / initials screen is up (game_post_nmi). */
void hs_post_nmi(void);

/* Draw the title HI-SCORE, the leaderboard and initials screens, and the
 * "score not saved" note (game_post_render, after the other overlays). */
void hs_render(uint32_t *fb);
