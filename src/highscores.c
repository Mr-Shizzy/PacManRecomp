/*
 * src/highscores.c — persistent top-10 leaderboard (Extras > HIGH SCORES).
 *
 * Off by default: then nothing here touches the game. When on:
 *  - HI-SCORE is leaderboard #1. The game keeps it in RAM ($61-$66, one BCD
 *    digit per byte, least significant first, shown with a trailing 0) and
 *    raises it live during play; on the title / attract we seed it from the
 *    board, and draw it over the title in case the game drew it first.
 *  - At the final GAME OVER (script 0A with no player left), each player
 *    whose score makes the top 10 enters initials, then the board scrolls
 *    up with the new rows highlighted. Games with any cheat or speed option
 *    on are not recorded (a note says so).
 *  - In the attract loop the board scrolls up between the character intro
 *    and the computer-played demo.
 * The game is held still for these screens by re-arming its "wait for
 * NMI" flag ($40) after every NMI, so its main loop just keeps waiting.
 * Everything is drawn over the frame in the game's own font.
 */
#include "highscores.h"
#include "options.h"
#include "nes_text.h"
#include "nes_runtime.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

/* ---- game RAM ---------------------------------------------------------- */
#define RAM_SCRIPT      0x3F
#define RAM_NMI_WAIT    0x40    /* main loop spins until the NMI clears it */
#define RAM_CUR_PLAYER  0x46
#define RAM_GAME_MODE   0x47    /* 0 = 1 player, 1 = 2 players */
#define RAM_FLAG_DEMO   0x48    /* 00 in a game; FF title and attract demo */
#define RAM_HISCORE     0x61    /* 6 digits, least significant first */
#define RAM_LIVES_OTHER 0x77
#define RAM_SCORE_CUR   0x70    /* current player's score */
#define RAM_SCORE_OTHER 0x80    /* the other player's score */
#define RAM_TIMER_LO    0x87    /* game-over display: wraps to end it */

#define SCRIPT_GAME_OVER 0x0A
#define SCRIPT_ATTRACT   0x04   /* title loop: character intro / chase */

/* The maze is on screen (its left wall on nametable row 16). The title loop
 * never draws it, so with the demo flag FF this means the attract demo game. */
#define NT_MAZE_ROW16    0x201
static int maze_showing(void) {
    return g_ppu_nt[NT_MAZE_ROW16] == 0x22 && g_ppu_nt[NT_MAZE_ROW16 + 1] == 0x10;
}

#define BTN_A       0x80
#define BTN_B       0x40
#define BTN_SELECT  0x20
#define BTN_START   0x10
#define BTN_UP      0x08
#define BTN_DOWN    0x04
#define BTN_LEFT    0x02
#define BTN_RIGHT   0x01

#define RANKS 10

typedef struct { char ini[4]; uint32_t score; } Entry;

static Entry s_board[RANKS];
static int   s_highlight[RANKS];

/* ---- scores ------------------------------------------------------------ */
static uint32_t read_score(int addr) {
    uint32_t v = 0;
    for (int i = 5; i >= 0; i--) v = v * 10 + (g_ram[addr + i] & 0x0F);
    return v * 10;                          /* the display's trailing 0 */
}

static void write_hiscore(uint32_t points) {
    uint32_t v = points / 10;
    for (int i = 0; i < 6; i++) { g_ram[RAM_HISCORE + i] = (uint8_t)(v % 10); v /= 10; }
}

/* Right-aligned in 7 columns like the game: blanks for leading zeros, at
 * least "00". */
static void score_text(uint32_t points, char out[8]) {
    char digits[16];
    snprintf(digits, sizeof(digits), "%lu", (unsigned long)points);
    if (points < 10) snprintf(digits, sizeof(digits), "00");
    snprintf(out, 8, "%7s", digits);
}

/* ---- persistence ------------------------------------------------------- */
static const char *scores_path(void) {
    static char path[1100];
    char dir[1024];
    nesrecomp_exe_dir(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%spacman_scores.ini", dir);
    return path;
}

static void clear_board(void) {
    for (int i = 0; i < RANKS; i++) {
        memcpy(s_board[i].ini, "---", 4);
        s_board[i].score = 0;
    }
}

static void save_board(void) {
    FILE *f = fopen(scores_path(), "w");
    if (!f) return;
    fprintf(f, "# Pac-Man high scores: rank = initials score\n");
    for (int i = 0; i < RANKS; i++)
        fprintf(f, "%d = %.3s %lu\n", i + 1, s_board[i].ini, (unsigned long)s_board[i].score);
    fclose(f);
}

void hs_init(void) {
    clear_board();
    FILE *f = fopen(scores_path(), "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        int rank, n = 0;
        if (sscanf(line, " %d = %n", &rank, &n) != 1 || rank < 1 || rank > RANKS) continue;
        const char *p = line + n;
        unsigned long score;
        if (strlen(p) < 4 || sscanf(p + 3, " %lu", &score) != 1) continue;
        memcpy(s_board[rank - 1].ini, p, 3);
        s_board[rank - 1].ini[3] = '\0';
        s_board[rank - 1].score = (uint32_t)score;
    }
    fclose(f);
}

void hs_reset(void) {
    clear_board();
    save_board();
}

static int qualifies(uint32_t points) {
    return points > 0 && points > s_board[RANKS - 1].score;
}

/* Insert, returning the rank index it landed on. */
static int insert(const char ini[4], uint32_t points) {
    int at = RANKS - 1;
    while (at > 0 && points > s_board[at - 1].score) at--;
    memmove(&s_board[at + 1], &s_board[at], (RANKS - 1 - at) * sizeof(Entry));
    memmove(&s_highlight[at + 1], &s_highlight[at], (RANKS - 1 - at) * sizeof(int));
    memcpy(s_board[at].ini, ini, 4);
    s_board[at].score = points;
    s_highlight[at] = 1;
    return at;
}

/* ---- state ------------------------------------------------------------- */
typedef enum { HS_IDLE, HS_ATTRACT, HS_ENTRY, HS_RESULTS } HsState;

static HsState s_state;
static int     s_frames;            /* frames in the current state */
static int     s_dirty;             /* a cheat/speed option was on this game */
static int     s_note;              /* show "score not saved" at game over */

/* Initials entry queue: up to two players, best score first. */
static struct { int player; uint32_t score; } s_queue[2];
static int     s_queue_n, s_queue_i;
static char    s_ini[4];
static int     s_pos;
static uint8_t s_prev1, s_prev2;

static const char k_letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.- ";

#define SCROLL_SPEED  2             /* px per frame for the board scroll */
#define BOARD_HOLD    300           /* frames the board stays up (5 s) */

static void start(HsState st) {
    s_state = st;
    s_frames = 0;
}

static void begin_entry(void) {
    memcpy(s_ini, "AAA", 4);
    s_pos = 0;
    start(HS_ENTRY);
}

static int letter_index(char c) {
    const char *p = strchr(k_letters, c);
    return p ? (int)(p - k_letters) : 0;
}

static void cycle_letter(int dir) {
    int n = (int)strlen(k_letters);
    s_ini[s_pos] = k_letters[(letter_index(s_ini[s_pos]) + dir + n) % n];
}

static void entry_input(uint8_t pressed) {
    int next = 0;
    if (!g_opt.modern) {
        if (pressed & BTN_SELECT) cycle_letter(+1);
        if (pressed & BTN_START)  next = 1;
    } else {
        if (pressed & BTN_UP)   cycle_letter(+1);
        if (pressed & BTN_DOWN) cycle_letter(-1);
        if (pressed & (BTN_RIGHT | BTN_A | BTN_START)) next = 1;
        if ((pressed & (BTN_LEFT | BTN_B)) && s_pos > 0) s_pos--;
    }
    if (!next) return;
    if (++s_pos < 3) return;
    insert(s_ini, s_queue[s_queue_i].score);
    if (++s_queue_i < s_queue_n) {
        begin_entry();
    } else {
        save_board();
        start(HS_RESULTS);
    }
}

/* The final GAME OVER: queue every player whose score makes the board. */
static void on_final_game_over(void) {
    s_queue_n = s_queue_i = 0;
    memset(s_highlight, 0, sizeof(s_highlight));
    if (s_dirty) { s_note = 1; return; }

    int cur = g_ram[RAM_CUR_PLAYER] & 1;
    uint32_t a = read_score(RAM_SCORE_CUR);
    if (qualifies(a)) { s_queue[s_queue_n].player = cur + 1; s_queue[s_queue_n++].score = a; }
    if (g_ram[RAM_GAME_MODE] & 1) {
        uint32_t b = read_score(RAM_SCORE_OTHER);
        if (qualifies(b)) { s_queue[s_queue_n].player = 2 - cur; s_queue[s_queue_n++].score = b; }
    }
    if (s_queue_n == 2 && s_queue[1].score > s_queue[0].score) {
        int p = s_queue[0].player; uint32_t sc = s_queue[0].score;
        s_queue[0] = s_queue[1];
        s_queue[1].player = p; s_queue[1].score = sc;
    }
    /* Two players can only both fit if the second still beats rank 10
     * after the first is in; insert() handles that ordering. */
    if (s_queue_n) begin_entry();
}

int hs_on_frame(void) {
    static uint8_t prev_script = 0xFF, prev_demo = 0xFF;
    static int prev_maze = 1;
    uint8_t demo = g_ram[RAM_FLAG_DEMO], script = g_ram[RAM_SCRIPT];
    int maze = maze_showing();
    uint8_t b1 = g_controller1_buttons, b2 = g_controller2_buttons;
    uint8_t p1 = (uint8_t)(b1 & ~s_prev1), p2 = (uint8_t)(b2 & ~s_prev2);
    s_prev1 = b1;
    s_prev2 = b2;

    if (!g_opt.highscores) {
        if (s_state != HS_IDLE) start(HS_IDLE);
        prev_script = script; prev_demo = demo; prev_maze = maze;
        return 0;
    }

    /* Track cheats for the game in progress. */
    if (demo == 0x00) {
        if (prev_demo != 0x00) { s_dirty = 0; s_note = 0; }
        if (options_cheats_active()) s_dirty = 1;
    } else {
        s_note = 0;
        write_hiscore(s_board[0].score);    /* title / attract: board #1 */
    }

    switch (s_state) {
    case HS_IDLE:
        if (demo == 0x00 && script == SCRIPT_GAME_OVER && prev_script != SCRIPT_GAME_OVER &&
            (!(g_ram[RAM_GAME_MODE] & 1) || g_ram[RAM_LIVES_OTHER] == 0))
            on_final_game_over();
        else if (demo == 0xFF && !prev_maze && maze)
            start(HS_ATTRACT);              /* attract demo game begins */
        break;
    case HS_ATTRACT:
        s_frames++;
        if ((p1 & (BTN_A | BTN_START)) || s_frames > 240 / SCROLL_SPEED + BOARD_HOLD) {
            start(HS_IDLE);
            break;                          /* Start also reaches the game */
        }
        g_controller1_buttons = 0;
        break;
    case HS_ENTRY: {
        s_frames++;
        int player = s_queue[s_queue_i].player;
        entry_input(player == 1 ? p1 : (uint8_t)(p1 | p2));
        g_controller1_buttons = g_controller2_buttons = 0;
        break;
    }
    case HS_RESULTS:
        s_frames++;
        if ((p1 & (BTN_A | BTN_START)) || s_frames > 240 / SCROLL_SPEED + BOARD_HOLD) {
            memset(s_highlight, 0, sizeof(s_highlight));
            g_ram[RAM_TIMER_LO] = 0xF8;     /* let GAME OVER end promptly */
            start(HS_IDLE);
        }
        g_controller1_buttons = g_controller2_buttons = 0;
        break;
    }
    prev_script = script;
    prev_demo = demo;
    prev_maze = maze;
    return s_state != HS_IDLE;
}

void hs_post_nmi(void) {
    if (s_state != HS_IDLE) g_ram[RAM_NMI_WAIT] = 1;
}

/* ---- drawing ----------------------------------------------------------- */
static const char *const k_ranks[RANKS] = {
    "1ST", "2ND", "3RD", "4TH", "5TH", "6TH", "7TH", "8TH", "9TH", "10TH"
};

static void draw_board(uint32_t *fb, int y_off) {
    text_clear_rows(fb, 0, 29, 0);
    text_draw_px(fb, 10 * 8, 4 * 8 + y_off, "HIGH SCORES", TEXT_SALMON);
    text_draw_px(fb, 6 * 8, 7 * 8 + y_off, "RANK  NAME    SCORE", TEXT_RED);
    int blink = (s_frames / 16) & 1;
    for (int i = 0; i < RANKS; i++) {
        int y = (9 + i * 2) * 8 + y_off;
        uint8_t col = s_highlight[i] ? (blink ? TEXT_YELLOW : TEXT_WHITE) : TEXT_WHITE;
        char sc[8];
        score_text(s_board[i].score, sc);
        text_draw_px(fb, 6 * 8, y, k_ranks[i], TEXT_ORANGE);
        text_draw_px(fb, 12 * 8, y, s_board[i].ini, col);
        text_draw_px(fb, 18 * 8, y, sc, col);
    }
}

static int scroll_offset(void) {
    int y = 240 - s_frames * SCROLL_SPEED;
    return y > 0 ? y : 0;
}

static void draw_entry(uint32_t *fb) {
    char buf[32];
    text_clear_rows(fb, 0, 29, 0);
    text_draw(fb, 8, 5, "CONGRATULATIONS", TEXT_SALMON);
    snprintf(buf, sizeof(buf), "PLAYER %d", s_queue[s_queue_i].player);
    text_draw(fb, 12, 8, buf, TEXT_WHITE);
    text_draw(fb, 6, 10, "YOU MADE THE TOP 10", TEXT_WHITE);
    text_draw(fb, 6, 13, "ENTER YOUR INITIALS", TEXT_SALMON);
    int blink = (s_frames / 12) & 1;
    for (int i = 0; i < 3; i++) {
        char c[2] = { s_ini[i], 0 };
        if (i == s_pos && blink) c[0] = ' ';
        text_draw(fb, 13 + i * 2, 16, c, TEXT_WHITE);
        if (i == s_pos) text_draw(fb, 13 + i * 2, 17, "-", TEXT_YELLOW);
    }
    char sc[8];
    score_text(s_queue[s_queue_i].score, sc);
    snprintf(buf, sizeof(buf), "SCORE %s", sc);
    text_draw(fb, 9, 20, buf, TEXT_WHITE);
    if (!g_opt.modern) {
        text_draw(fb, 4, 24, "SELECT - CHANGE LETTER", TEXT_ORANGE);
        text_draw(fb, 4, 26, "START  - NEXT", TEXT_ORANGE);
    } else {
        text_draw(fb, 4, 24, "UP.DOWN - CHANGE LETTER", TEXT_ORANGE);
        text_draw(fb, 4, 26, "A - NEXT     B - BACK", TEXT_ORANGE);
    }
}

void hs_render(uint32_t *fb) {
    if (!g_opt.highscores) return;
    /* Title-loop screens (scroll-in, menu, character intro) show the score
     * bar: draw board #1 over the game's HI-SCORE, riding the scroll-in. */
    int y = 0;
    int title_bar = options_title_y(&y) ||
        (g_ram[RAM_FLAG_DEMO] == 0xFF && g_ram[RAM_SCRIPT] == SCRIPT_ATTRACT && !maze_showing());
    if (s_state == HS_IDLE && title_bar) {
        char sc[8];
        score_text(s_board[0].score, sc);
        text_draw_px(fb, 12 * 8, 4 * 8 + y, sc, TEXT_WHITE);
    }
    switch (s_state) {
    case HS_ATTRACT:
    case HS_RESULTS: draw_board(fb, scroll_offset()); break;
    case HS_ENTRY:   draw_entry(fb); break;
    default:
        if (s_note && g_ram[RAM_SCRIPT] == SCRIPT_GAME_OVER) {
            text_draw(fb, 23, 20, "CHEATS ON", TEXT_WHITE);
            text_draw(fb, 23, 21, "SCORE NOT", TEXT_WHITE);
            text_draw(fb, 23, 22, "SAVED", TEXT_WHITE);
        }
        break;
    }
}
