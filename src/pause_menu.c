/*
 * src/pause_menu.c — "exit to launcher" prompt injected into Pac-Man's pause
 *
 * While the game is paused (ram_flag_pause odd) the stock pause loop only
 * watches Start, so Select/A/B/d-pad are free. We draw a hint under the game's
 * own PAUSE text and, on Select, a MAIN MENU / LAUNCHER / BACK menu. Escape reaches the same
 * prompt by pausing through the game's own Start handling. Everything is a
 * host-side overlay: game RAM and VRAM are never written, only read.
 *
 * Controls follow the OPTIONS menu style: classic (Select moves the cursor,
 * Start picks) or modern (d-pad moves, A picks, B backs out).
 */
#include "pause_menu.h"
#include "nes_text.h"
#include "options.h"
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
#define BTN_UP      0x08
#define BTN_DOWN    0x04
#define BTN_LEFT    0x02
#define BTN_RIGHT   0x01

/* Right-hand HUD column: PAUSE sits at nametable $2237 = row 17, col 23.
 * Rows 18-19 hold the fruit, rows 24+ the lives, so we use rows 20-22. */
#define TEXT_COL    23
#define TEXT_ROW    20

#define COLOR_TEXT   TEXT_WHITE
#define COLOR_CURSOR TEXT_YELLOW

enum { PICK_MAIN_MENU, PICK_LAUNCHER, PICK_BACK, PICK_COUNT };
static const char *const k_picks[PICK_COUNT] = { "MAIN MENU", "LAUNCHER", "BACK" };

static int     s_confirm;       /* menu open */
static int     s_sel;           /* cursor: PICK_* */
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
    s_sel = PICK_BACK;          /* default BACK: hard to leave by accident */
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
        int pick;
        if (!g_opt.modern) {
            if (pressed & BTN_SELECT) s_sel = (s_sel + 1) % PICK_COUNT;
            pick = pressed & BTN_START;
        } else {
            if (pressed & (BTN_UP | BTN_LEFT))    s_sel = (s_sel + PICK_COUNT - 1) % PICK_COUNT;
            if (pressed & (BTN_DOWN | BTN_RIGHT)) s_sel = (s_sel + 1) % PICK_COUNT;
            if (pressed & BTN_B) s_confirm = 0;
            pick = pressed & (BTN_A | BTN_START);
        }
        if (pick) {
            if (s_sel == PICK_LAUNCHER) nesrecomp_return_to_launcher();
            s_confirm = 0;
            if (s_sel == PICK_MAIN_MENU) {
                options_quit_to_title();
                return;
            }
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

void pause_menu_render(uint32_t *fb) {
    if (!(g_ram[RAM_FLAG_PAUSE] & 1)) return;

    if (!s_confirm) {
        text_draw(fb, TEXT_COL, TEXT_ROW,     "SELECT",   COLOR_TEXT);
        text_draw(fb, TEXT_COL, TEXT_ROW + 1, "FOR MENU", COLOR_TEXT);
        return;
    }
    for (int i = 0; i < PICK_COUNT; i++) {
        text_draw(fb, TEXT_COL, TEXT_ROW + i, k_picks[i], COLOR_TEXT);
        if (i == s_sel) text_draw(fb, TEXT_COL - 1, TEXT_ROW + i, "@", COLOR_CURSOR);
    }
}
