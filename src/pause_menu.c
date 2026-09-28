/*
 * src/pause_menu.c — "exit to launcher" prompt injected into Pac-Man's pause
 *
 * While the game is paused (ram_flag_pause odd) the stock pause loop only
 * watches Start, so Select/A/B/d-pad are free. We draw a hint under the game's
 * own PAUSE text and, on Select, a YES/NO confirm. Escape reaches the same
 * prompt by pausing through the game's own Start handling. Everything is a
 * host-side overlay: game RAM and VRAM are never written, only read.
 *
 * Text is drawn with the game's own background tiles, which are ASCII-indexed
 * (the pause routine writes "PAUSE" as 50 41 55 53 45). The font has A-Z, 0-9,
 * '-', '.' and the title-screen cursor, but no '?' or ':'.
 */
#include "pause_menu.h"
#include "nes_runtime.h"
#include <stdint.h>

#define RAM_SCRIPT      0x3F    /* game-loop state; 04 = in play (pausable) */
#define RAM_FLAG_DEMO   0x48    /* 00 = in a game; FF title, else attract demo */
#define RAM_FLAG_PAUSE  0x4A    /* even = running, odd = paused */

#define SCRIPT_PLAY 0x04

#define BTN_A       0x80
#define BTN_B       0x40
#define BTN_SELECT  0x20
#define BTN_START   0x10
#define BTN_DPAD    0x0F

#define TILE_CURSOR 0x5C        /* title-screen cursor */

/* Right-hand HUD column: PAUSE sits at nametable $2237 = row 17, col 23.
 * Rows 18-19 hold the fruit, rows 24+ the lives, so we use rows 20-22. */
#define TEXT_COL    23
#define TEXT_ROW    20

#define COLOR_TEXT   0x30       /* white */
#define COLOR_CURSOR 0x28       /* yellow */

static int     s_confirm;       /* confirm prompt open */
static int     s_yes;           /* cursor on YES */
static int     s_hold_start;    /* hide Start from the game until released */
static int     s_escape;        /* Escape pressed since the last frame */
static int     s_escape_pause;  /* Escape wants the game paused + prompt open */
static int     s_injected;      /* we synthesized Start last frame */
static uint8_t s_prev;

int pause_menu_escape(void) {
    s_escape = 1;
    return 1;
}

static void open_confirm(void) {
    s_confirm = 1;
    s_yes = 0;                  /* default NO: hard to leave by accident */
}

/* Escape: in a game, pause (via a synthetic Start once play is pausable) and
 * open the prompt; while paused, toggle the prompt. Outside a game (title,
 * attract demo) there is nothing to lose, so go straight to the launcher. */
static void handle_escape(int paused) {
    if (g_ram[RAM_FLAG_DEMO] != 0x00) nesrecomp_return_to_launcher();
    if (paused) {
        if (s_confirm) s_confirm = 0;
        else open_confirm();
    } else {
        s_escape_pause = 1;
    }
}

void pause_menu_on_frame(void) {
    /* A synthesized Start must not linger into the next frame (where it would
     * read as a real press and confirm/close the prompt): drop it once. */
    if (s_injected) {
        s_injected = 0;
        g_controller1_buttons &= (uint8_t)~BTN_START;
    }
    uint8_t btn = g_controller1_buttons;
    uint8_t pressed = (uint8_t)(btn & ~s_prev);
    int paused = g_ram[RAM_FLAG_PAUSE] & 1;
    s_prev = btn;

    if (s_escape) {
        s_escape = 0;
        handle_escape(paused);
    }
    if (s_escape_pause) {
        if (g_ram[RAM_FLAG_DEMO] != 0x00) {
            s_escape_pause = 0;         /* game ended meanwhile */
        } else if (paused) {
            s_escape_pause = 0;
            open_confirm();
            s_hold_start = 1;
        } else if (g_ram[RAM_SCRIPT] == SCRIPT_PLAY) {
            g_controller1_buttons |= BTN_START;  /* the game's own pause */
            s_injected = 1;
            return;
        }
    }

    if (!paused) {
        s_confirm = 0;
        s_hold_start = 0;
        return;
    }

    if (!s_confirm) {
        if (pressed & BTN_SELECT) open_confirm();
    } else {
        if (pressed & BTN_DPAD) s_yes ^= 1;
        if (pressed & BTN_B) s_confirm = 0;
        if (pressed & (BTN_A | BTN_START)) {
            if (s_yes) nesrecomp_return_to_launcher();
            s_confirm = 0;
        }
        s_hold_start = 1;
    }

    /* Start while the prompt is up (or still held after closing it) must not
     * reach the game, or it would unpause behind the prompt. */
    if (s_hold_start) {
        if (btn & BTN_START) g_controller1_buttons &= (uint8_t)~BTN_START;
        else if (!s_confirm) s_hold_start = 0;
    }
}

/* Map ASCII to the game's tile index; 0 = blank cell. */
static uint8_t tile_for(char c) {
    if (c >= 'A' && c <= 'Z') return (uint8_t)c;
    if (c >= '0' && c <= '9') return (uint8_t)c;
    if (c == '-') return 0x3A;
    if (c == '.') return 0x5B;
    if (c == '>') return TILE_CURSOR;
    return 0;
}

static void draw_text(uint32_t *fb, int col, int row, const char *s, uint8_t color) {
    const uint8_t *chr = g_chr_ram + ((g_ppuctrl & 0x10) ? 0x1000 : 0);
    const uint32_t fg = g_nes_palette[color & 0x3F];
    const uint32_t bg = g_nes_palette[0x0F];
    for (; *s; s++, col++) {
        if (col >= 32) break;
        const uint8_t t = tile_for(*s);
        const uint8_t *px = chr + t * 16;
        for (int y = 0; y < 8; y++) {
            uint32_t *line = fb + (row * 8 + y) * g_render_width
                                + g_widescreen_left + col * 8;
            for (int x = 0; x < 8; x++) {
                int bit = 7 - x;
                int v = t ? (((px[y] >> bit) & 1) | (((px[y + 8] >> bit) & 1) << 1)) : 0;
                line[x] = v ? fg : bg;
            }
        }
    }
}

void pause_menu_render(uint32_t *fb) {
    if (!(g_ram[RAM_FLAG_PAUSE] & 1)) return;

    if (!s_confirm) {
        draw_text(fb, TEXT_COL, TEXT_ROW,     "SELECT",  COLOR_TEXT);
        draw_text(fb, TEXT_COL, TEXT_ROW + 1, "TO EXIT", COLOR_TEXT);
        return;
    }
    draw_text(fb, TEXT_COL, TEXT_ROW,     "EXIT TO",  COLOR_TEXT);
    draw_text(fb, TEXT_COL, TEXT_ROW + 1, "LAUNCHER", COLOR_TEXT);
    draw_text(fb, TEXT_COL, TEXT_ROW + 2, " YES  NO", COLOR_TEXT);
    draw_text(fb, TEXT_COL + (s_yes ? 0 : 5), TEXT_ROW + 2, ">", COLOR_CURSOR);
}
