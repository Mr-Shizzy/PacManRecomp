/*
 * src/options.c — in-game OPTIONS menu on Pac-Man's title screen.
 *
 * The title loop's menu (RAM $3F = 02 while $48 = FF) shows 1 PLAYER /
 * 2 PLAYERS. We draw our own 1 PLAYER / 2 PLAYERS / OPTIONS in the same font
 * and place, hide the pad from the game while it is up, and start a game by
 * setting the game's own 1P/2P mode ($47) and passing it one Start press.
 * OPTIONS replaces everything below the logo with our screens; the idle timer
 * ($87/$88, attract demo at $200) is held so the demo never starts.
 *
 * Display and volume are the runner's settings (g_nes_config, config.ini);
 * the rest are Pac-Man's own (pacman_options.ini next to the exe). Every
 * change applies at once and is saved at once.
 */
#include "options.h"
#include "nes_text.h"
#include "nes_runtime.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

/* ---- game RAM ---------------------------------------------------------- */
#define RAM_SCRIPT      0x3F    /* title loop: 00 scroll-in, 02 menu, 04 attract */
#define RAM_SCROLL_Y    0x42    /* title scroll-in: counts 0 -> F0 */
#define RAM_GAME_MODE   0x47    /* 0 = 1 player, 1 = 2 players */
#define RAM_FLAG_DEMO   0x48    /* FF = title screen */
#define RAM_TIMER_LO    0x87    /* title idle timer; attract demo at $200 */
#define RAM_TIMER_HI    0x88

#define SCRIPT_SCROLL   0x00
#define SCRIPT_MENU     0x02
#define DEMO_TITLE      0xFF

/* ---- pad bits (g_controller1_buttons) ---------------------------------- */
#define BTN_A       0x80
#define BTN_B       0x40
#define BTN_SELECT  0x20
#define BTN_START   0x10
#define BTN_UP      0x08
#define BTN_DOWN    0x04
#define BTN_LEFT    0x02
#define BTN_RIGHT   0x01

#define START_LEVEL_MAX 20

PacOptions g_opt;

static const PacOptions k_defaults = {
    .inverse = 0, .music = 1, .sfx = 1, .echo = 0, .modern = 0,
    .pac_speed = 1, .ghost_speed = 1,
    .inf_lives = 0, .start_level = 1, .invincible = 0,
};

/* ---- menu model -------------------------------------------------------- */
typedef enum {
    SCR_TITLE, SCR_OPTIONS, SCR_VIDEO, SCR_AUDIO, SCR_CONTROLS,
    SCR_EXTRAS, SCR_CHEATS, SCR_RESET, SCR_COUNT
} Screen;

typedef enum {
    IT_PLAY,        /* start a game; lo = 1P/2P mode */
    IT_SECTION,     /* open screen `lo` */
    IT_BACK,
    IT_TOGGLE,      /* *val 0/1 */
    IT_RANGE,       /* *val lo..hi by step, shown with fmt */
    IT_STYLE,       /* *val 0 classic / 1 modern */
    IT_RESET,       /* reset everything to defaults */
} ItemKind;

typedef struct {
    const char *label;
    ItemKind    kind;
    int        *val;
    int         lo, hi, step;
    const char *fmt;
    int         runner;         /* 1 = runner setting (config.ini) */
} Item;

typedef struct {
    const char *header;
    const Item *items;
    int         count;
    Screen      parent;
} ScreenDef;

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const Item k_title[] = {
    { "1 PLAYER",  IT_PLAY,    0, 0 },
    { "2 PLAYERS", IT_PLAY,    0, 1 },
    { "OPTIONS",   IT_SECTION, 0, SCR_OPTIONS },
};
static const Item k_options[] = {
    { "VIDEO",            IT_SECTION, 0, SCR_VIDEO },
    { "AUDIO",            IT_SECTION, 0, SCR_AUDIO },
    { "CONTROLS",         IT_SECTION, 0, SCR_CONTROLS },
    { "EXTRAS",           IT_SECTION, 0, SCR_EXTRAS },
    { "CHEATS",           IT_SECTION, 0, SCR_CHEATS },
    { "RESET TO DEFAULT", IT_SECTION, 0, SCR_RESET },
    { "BACK",             IT_BACK },
};
static const Item k_video[] = {
    { "STRETCH",        IT_TOGGLE, &g_nes_config.stretch,       0, 1, 1, 0, 1 },
    { "FILTER",         IT_TOGGLE, &g_nes_config.linear_filter, 0, 1, 1, 0, 1 },
    { "INTEGER SCALE",  IT_TOGGLE, &g_nes_config.integer_scale, 0, 1, 1, 0, 1 },
    { "INVERSE COLORS", IT_TOGGLE, &g_opt.inverse },
    { "BACK",           IT_BACK },
};
static const Item k_audio[] = {
    { "VOLUME",    IT_RANGE,  &g_nes_config.volume, 0, 100, 10, "%d%%", 1 },
    { "MUSIC",     IT_TOGGLE, &g_opt.music },
    { "SOUND FX",  IT_TOGGLE, &g_opt.sfx },
    { "ECHO",      IT_TOGGLE, &g_opt.echo },
    { "BACK",      IT_BACK },
};
static const Item k_controls[] = {
    { "MENU STYLE", IT_STYLE, &g_opt.modern },
    { "BACK",       IT_BACK },
};
static const Item k_extras[] = {
    { "PAC-MAN SPEED", IT_RANGE, &g_opt.pac_speed,   1, 3, 1, "%dX" },
    { "GHOST SPEED",   IT_RANGE, &g_opt.ghost_speed, 1, 3, 1, "%dX" },
    { "BACK",          IT_BACK },
};
static const Item k_cheats[] = {
    { "INFINITE LIVES", IT_TOGGLE, &g_opt.inf_lives },
    { "START LEVEL",    IT_RANGE,  &g_opt.start_level, 1, START_LEVEL_MAX, 1, "%d" },
    { "INVINCIBLE",     IT_TOGGLE, &g_opt.invincible },
    { "BACK",           IT_BACK },
};
static const Item k_reset[] = {
    { "NO",  IT_BACK },
    { "YES", IT_RESET },
};

static const ScreenDef k_screens[SCR_COUNT] = {
    [SCR_TITLE]    = { NULL,               k_title,    N(k_title),    SCR_TITLE },
    [SCR_OPTIONS]  = { "OPTIONS",          k_options,  N(k_options),  SCR_TITLE },
    [SCR_VIDEO]    = { "VIDEO",            k_video,    N(k_video),    SCR_OPTIONS },
    [SCR_AUDIO]    = { "AUDIO",            k_audio,    N(k_audio),    SCR_OPTIONS },
    [SCR_CONTROLS] = { "CONTROLS",         k_controls, N(k_controls), SCR_OPTIONS },
    [SCR_EXTRAS]   = { "EXTRAS",           k_extras,   N(k_extras),   SCR_OPTIONS },
    [SCR_CHEATS]   = { "CHEATS",           k_cheats,   N(k_cheats),   SCR_OPTIONS },
    [SCR_RESET]    = { "RESET TO DEFAULT", k_reset,    N(k_reset),    SCR_OPTIONS },
};

static Screen  s_scr = SCR_TITLE;
static int     s_sel[SCR_COUNT];
static int     s_was_menu;
static int     s_injected;      /* synthesized Start last frame */
static uint8_t s_prev;

/* ---- persistence ------------------------------------------------------- */
static const char *options_path(void) {
    static char path[1100];
    char dir[1024];
    nesrecomp_exe_dir(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%spacman_options.ini", dir);
    return path;
}

static struct { const char *key; int *val; } k_keys[] = {
    { "InverseColors", &g_opt.inverse },
    { "Music",         &g_opt.music },
    { "SoundFx",       &g_opt.sfx },
    { "Echo",          &g_opt.echo },
    { "ModernMenus",   &g_opt.modern },
    { "PacManSpeed",   &g_opt.pac_speed },
    { "GhostSpeed",    &g_opt.ghost_speed },
    { "InfiniteLives", &g_opt.inf_lives },
    { "StartLevel",    &g_opt.start_level },
    { "Invincible",    &g_opt.invincible },
};

static void options_save(void) {
    FILE *f = fopen(options_path(), "w");
    if (!f) return;
    fprintf(f, "# Pac-Man options - edited by the in-game OPTIONS menu.\n");
    for (int i = 0; i < N(k_keys); i++)
        fprintf(f, "%s = %d\n", k_keys[i].key, *k_keys[i].val);
    fclose(f);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void options_load(void) {
    g_opt = k_defaults;
    FILE *f = fopen(options_path(), "r");
    if (!f) return;
    char line[128], key[64];
    int v;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, " %63[A-Za-z] = %d", key, &v) != 2) continue;
        for (int i = 0; i < N(k_keys); i++)
            if (!strcmp(key, k_keys[i].key)) *k_keys[i].val = v;
    }
    fclose(f);
    g_opt.pac_speed   = clampi(g_opt.pac_speed, 1, 3);
    g_opt.ghost_speed = clampi(g_opt.ghost_speed, 1, 3);
    g_opt.start_level = clampi(g_opt.start_level, 1, START_LEVEL_MAX);
}

static void setting_changed(const Item *it) {
    if (it->runner) {
        nesrecomp_apply_video_settings();
        config_save(config_path());
    } else {
        options_save();
    }
}

static void reset_all(void) {
    g_opt = k_defaults;
    g_nes_config.stretch = 0;
    g_nes_config.linear_filter = 0;
    g_nes_config.integer_scale = 1;
    g_nes_config.volume = 100;
    nesrecomp_apply_video_settings();
    config_save(config_path());
    options_save();
}

/* ---- echo -------------------------------------------------------------- */
#define ECHO_MAX 24000
static int16_t s_echo_buf[ECHO_MAX];
static int     s_echo_pos;
static int     s_echo_live;

static void echo_filter(int16_t *s, int n, int rate) {
    if (!g_opt.echo) {
        if (s_echo_live) { memset(s_echo_buf, 0, sizeof(s_echo_buf)); s_echo_live = 0; }
        return;
    }
    s_echo_live = 1;
    int delay = rate / 5;                       /* 200 ms */
    if (delay > ECHO_MAX) delay = ECHO_MAX;
    for (int i = 0; i < n; i++) {
        int out = s[i] + s_echo_buf[s_echo_pos] * 2 / 5;
        out = clampi(out, -32768, 32767);
        s_echo_buf[s_echo_pos] = (int16_t)out;
        s[i] = (int16_t)out;
        if (++s_echo_pos >= delay) s_echo_pos = 0;
    }
}

void options_init(void) {
    options_load();
    nesrecomp_set_audio_filter(echo_filter);
}

/* ---- navigation -------------------------------------------------------- */
static void go(Screen scr) {
    s_scr = scr;
}

static void change_value(const Item *it, int dir) {
    switch (it->kind) {
    case IT_TOGGLE:
    case IT_STYLE:
        *it->val = !*it->val;
        break;
    case IT_RANGE: {
        int v = *it->val + dir * it->step;
        if (v > it->hi) v = it->lo;
        if (v < it->lo) v = it->hi;
        *it->val = v;
        break;
    }
    default:
        return;
    }
    setting_changed(it);
}

/* Returns 1 if a game was started (the pad must reach the game this frame). */
static int activate(const Item *it) {
    switch (it->kind) {
    case IT_PLAY:
        g_ram[RAM_GAME_MODE] = (uint8_t)it->lo;
        return 1;
    case IT_SECTION:
        go((Screen)it->lo);
        if (it->lo == SCR_RESET) s_sel[SCR_RESET] = 0;     /* always NO first */
        break;
    case IT_BACK:
        go(k_screens[s_scr].parent);
        break;
    case IT_RESET:
        reset_all();
        go(k_screens[s_scr].parent);
        break;
    default:
        change_value(it, +1);
        break;
    }
    return 0;
}

static void title_menu_input(uint8_t pressed) {
    const ScreenDef *sd = &k_screens[s_scr];
    int *sel = &s_sel[s_scr];
    const Item *it = &sd->items[*sel];
    int start_game = 0;

    if (!g_opt.modern) {
        /* Classic: Select moves the cursor, Start picks (the original feel). */
        if (pressed & BTN_SELECT) *sel = (*sel + 1) % sd->count;
        else if (pressed & BTN_START) start_game = activate(it);
    } else {
        /* Modern: D-pad moves / changes, A (or Start) picks, B goes back. */
        if (pressed & BTN_UP)   *sel = (*sel + sd->count - 1) % sd->count;
        if (pressed & BTN_DOWN) *sel = (*sel + 1) % sd->count;
        if (pressed & BTN_LEFT)  change_value(it, -1);
        if (pressed & BTN_RIGHT) change_value(it, +1);
        if (pressed & (BTN_A | BTN_START)) start_game = activate(it);
        else if ((pressed & BTN_B) && s_scr != SCR_TITLE) go(sd->parent);
    }

    if (start_game) {
        g_controller1_buttons = BTN_START;      /* the game's own Start */
        s_injected = 1;
        return;
    }
    g_controller1_buttons = 0;                  /* the menu owns the pad */
    if (pressed || s_scr != SCR_TITLE) {
        /* Navigation counts as activity (as the original Select did), and
         * the options screens hold the idle timer: no attract demo. */
        g_ram[RAM_TIMER_LO] = 1;
        g_ram[RAM_TIMER_HI] = 0;
    }
}

void options_on_frame(void) {
    if (s_injected) {
        s_injected = 0;
        g_controller1_buttons &= (uint8_t)~BTN_START;
    }
    uint8_t btn = g_controller1_buttons;
    uint8_t pressed = (uint8_t)(btn & ~s_prev);
    s_prev = btn;

    int menu = g_ram[RAM_FLAG_DEMO] == DEMO_TITLE && g_ram[RAM_SCRIPT] == SCRIPT_MENU;
    if (menu && !s_was_menu) {
        s_scr = SCR_TITLE;
        s_sel[SCR_TITLE] = g_ram[RAM_GAME_MODE] & 1;
    }
    s_was_menu = menu;
    if (menu) title_menu_input(pressed);
}

/* ---- drawing ----------------------------------------------------------- */
#define TITLE_ROW0   14     /* 1 PLAYER; items every 2 rows */
#define TITLE_COL    12     /* original text column; cursor 2 to the left */
#define OPT_HEADER   14
#define OPT_ROW0     16
#define OPT_COL      7
#define OPT_VAL_END  26     /* values right-aligned to this column */

static void draw_title_items(uint32_t *fb, int y_off) {
    text_clear_rows(fb, TITLE_ROW0 - 1, TITLE_ROW0 + 5, y_off);
    for (int i = 0; i < N(k_title); i++) {
        int y = (TITLE_ROW0 + i * 2) * 8 + y_off;
        text_draw_px(fb, TITLE_COL * 8, y, k_title[i].label, TEXT_WHITE);
        if (i == s_sel[SCR_TITLE])
            text_draw_px(fb, (TITLE_COL - 2) * 8, y, "@", TEXT_WHITE);
    }
}

static void value_text(const Item *it, char *buf, int n) {
    buf[0] = '\0';
    switch (it->kind) {
    case IT_TOGGLE: snprintf(buf, n, "%s", *it->val ? "ON" : "OFF"); break;
    case IT_STYLE:  snprintf(buf, n, "%s", *it->val ? "MODERN" : "CLASSIC"); break;
    case IT_RANGE:  snprintf(buf, n, it->fmt, *it->val); break;
    default: break;
    }
}

static void draw_screen(uint32_t *fb) {
    const ScreenDef *sd = &k_screens[s_scr];
    text_clear_rows(fb, 13, 29, 0);
    int hl = (int)strlen(sd->header);
    text_draw(fb, (32 - hl) / 2, OPT_HEADER, sd->header, TEXT_SALMON);

    int row0 = OPT_ROW0;
    if (s_scr == SCR_RESET) {
        text_draw(fb, 6, 17, "RESET ALL SETTINGS?", TEXT_WHITE);
        row0 = 20;
    }
    for (int i = 0; i < sd->count; i++) {
        const Item *it = &sd->items[i];
        int row = row0 + i * 2;
        int col = s_scr == SCR_RESET ? 14 : OPT_COL;
        text_draw(fb, col, row, it->label, TEXT_WHITE);
        if (i == s_sel[s_scr]) text_draw(fb, col - 2, row, "@", TEXT_WHITE);
        char val[16];
        value_text(it, val, sizeof(val));
        if (val[0])
            text_draw(fb, OPT_VAL_END + 1 - (int)strlen(val), row, val, TEXT_ORANGE);
    }
}

void options_render(uint32_t *fb) {
    if (g_ram[RAM_FLAG_DEMO] != DEMO_TITLE) return;
    if (g_ram[RAM_SCRIPT] == SCRIPT_SCROLL) {
        /* The title scrolls in from below: the picture sits 240 - scroll_Y
         * pixels lower than at rest, so our lines ride along with it. */
        s_sel[SCR_TITLE] = g_ram[RAM_GAME_MODE] & 1;
        draw_title_items(fb, 240 - g_ram[RAM_SCROLL_Y]);
    } else if (g_ram[RAM_SCRIPT] == SCRIPT_MENU) {
        if (s_scr == SCR_TITLE) draw_title_items(fb, 0);
        else draw_screen(fb);
    }
}

void options_post_process(uint32_t *fb) {
    if (!g_opt.inverse) return;
    int n = g_render_width * 240;
    for (int i = 0; i < n; i++) fb[i] ^= 0x00FFFFFFu;
}
