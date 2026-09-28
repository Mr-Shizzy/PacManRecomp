/*
 * src/nes_text.h — draw text over the frame in Pac-Man's own font.
 *
 * Letters, digits, '-' and '.' come from the game's ASCII-indexed background
 * tiles; ':' '?' '%' '<' '>' are extra glyphs drawn in the same style.
 * '@' is the title-screen cursor. Space and unknown characters are blank.
 */
#pragma once
#include <stdint.h>

/* NES palette indices used by the original title screen. */
#define TEXT_WHITE   0x20
#define TEXT_ORANGE  0x27
#define TEXT_SALMON  0x26
#define TEXT_YELLOW  0x28
#define TEXT_RED     0x06   /* HUD labels: HI-SCORE, 1UP */

/* Draw at a tile cell (col, row), painting each cell's background black. */
void text_draw(uint32_t *fb, int col, int row, const char *s, uint8_t color);

/* Same, at a pixel position; rows outside the 240-line frame are clipped. */
void text_draw_px(uint32_t *fb, int x, int y, const char *s, uint8_t color);

/* Paint tile rows [row0, row1] black (full width), offset by y_px pixels. */
void text_clear_rows(uint32_t *fb, int row0, int row1, int y_px);
