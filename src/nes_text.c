/*
 * src/nes_text.c — text in Pac-Man's own font (see nes_text.h).
 */
#include "nes_text.h"
#include "nes_runtime.h"

#define TILE_CURSOR 0x5C        /* title-screen cursor */
#define COLOR_BG    0x0F        /* the title/maze background black */

/* Extra glyphs in the game's style: 7 rows tall, 2-pixel vertical strokes. */
static const struct { char c; uint8_t rows[8]; } s_glyphs[] = {
    { ':', { 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00 } },
    { '?', { 0x3E, 0x63, 0x06, 0x0C, 0x18, 0x00, 0x18, 0x00 } },
    { '%', { 0x63, 0x66, 0x0C, 0x18, 0x30, 0x66, 0xC6, 0x00 } },
    { '<', { 0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00 } },
    { '>', { 0x30, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x30, 0x00 } },
    { '!', { 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00 } },
};

/* Fill `rows` (8 bytes, 1 bit per pixel, MSB left) for character c.
 * Returns 0 for a blank cell. */
static int glyph_rows(char c, uint8_t rows[8]) {
    int tile = -1;
    if (c >= 'A' && c <= 'Z') tile = c;
    else if (c >= '0' && c <= '9') tile = c;
    else if (c == '-') tile = 0x3A;
    else if (c == '.') tile = 0x5B;
    else if (c == '@') tile = TILE_CURSOR;
    if (tile >= 0) {
        const uint8_t *chr = g_chr_ram + ((g_ppuctrl & 0x10) ? 0x1000 : 0) + tile * 16;
        for (int y = 0; y < 8; y++) rows[y] = (uint8_t)(chr[y] | chr[y + 8]);
        return 1;
    }
    for (unsigned i = 0; i < sizeof(s_glyphs) / sizeof(s_glyphs[0]); i++) {
        if (s_glyphs[i].c != c) continue;
        for (int y = 0; y < 8; y++) rows[y] = s_glyphs[i].rows[y];
        return 1;
    }
    return 0;
}

void text_draw_px(uint32_t *fb, int x, int y, const char *s, uint8_t color) {
    const uint32_t fg = g_nes_palette[color & 0x3F];
    const uint32_t bg = g_nes_palette[COLOR_BG];
    for (; *s; s++, x += 8) {
        if (x < 0 || x > 248) continue;
        uint8_t rows[8] = { 0 };
        glyph_rows(*s, rows);
        for (int r = 0; r < 8; r++) {
            int py = y + r;
            if (py < 0 || py >= 240) continue;
            uint32_t *line = fb + py * g_render_width + g_widescreen_left + x;
            for (int b = 0; b < 8; b++)
                line[b] = (rows[r] >> (7 - b)) & 1 ? fg : bg;
        }
    }
}

void text_draw(uint32_t *fb, int col, int row, const char *s, uint8_t color) {
    text_draw_px(fb, col * 8, row * 8, s, color);
}

void text_clear_rows(uint32_t *fb, int row0, int row1, int y_px) {
    const uint32_t bg = g_nes_palette[COLOR_BG];
    for (int py = row0 * 8 + y_px; py < (row1 + 1) * 8 + y_px; py++) {
        if (py < 0 || py >= 240) continue;
        uint32_t *line = fb + py * g_render_width + g_widescreen_left;
        for (int x = 0; x < 256; x++) line[x] = bg;
    }
}
