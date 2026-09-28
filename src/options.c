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
#include "apu.h"
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
    .pac_fast = 0, .ghost_fast = 0, .show_level = 0,
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
    IT_SPEED,       /* *val 0 normal / 1 1.5x */
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
    { "PAC-MAN SPEED", IT_SPEED,  &g_opt.pac_fast },
    { "GHOST SPEED",   IT_SPEED,  &g_opt.ghost_fast },
    { "SHOW LEVEL",    IT_TOGGLE, &g_opt.show_level },
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
    { "PacManFast",    &g_opt.pac_fast },
    { "GhostFast",     &g_opt.ghost_fast },
    { "ShowLevel",     &g_opt.show_level },
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
    g_opt.pac_fast    = clampi(g_opt.pac_fast, 0, 1);
    g_opt.ghost_fast  = clampi(g_opt.ghost_fast, 0, 1);
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

/* ---- music / sound-effect mute ------------------------------------------
 * The sound engine runs 16 slots: request flags at $0600+i, state at
 * $0620+8i whose first byte names the APU channel (1-4 queued, 5-8 playing;
 * pulse1, pulse2, triangle, noise). Lower slots win a shared channel. Music
 * is the start jingle (slots 0-1) and the intermission tunes (slots 13-14);
 * the rest are effects. Muting silences a channel at the mixer only, so the
 * game (which waits on the jingle) keeps its exact timing. */
#define RAM_SND_REQ   0x600
#define RAM_SND_SLOT  0x620

static int slot_is_music(int i) { return i <= 1 || i == 13 || i == 14; }

static void update_sound_mute(void) {
    uint8_t mask = 0;
    if (!g_opt.music || !g_opt.sfx) {
        int owner[4] = { -1, -1, -1, -1 };
        for (int i = 0; i < 16; i++) {
            if (!g_ram[RAM_SND_REQ + i]) continue;
            int b = g_ram[RAM_SND_SLOT + 8 * i];
            int ch = b >= 5 ? b - 5 : b - 1;
            if (ch < 0 || ch > 3 || owner[ch] >= 0) continue;
            owner[ch] = i;
        }
        /* A looping effect (the siren) restarts with a one-frame gap; hold
         * the mute across short ownerless gaps so nothing blips through. */
        static int hold[4];
        for (int ch = 0; ch < 4; ch++) {
            if (owner[ch] < 0) {
                if (hold[ch] > 0) { hold[ch]--; mask |= (uint8_t)(1 << ch); }
                continue;
            }
            int music = slot_is_music(owner[ch]);
            hold[ch] = 0;
            if ((music && !g_opt.music) || (!music && !g_opt.sfx)) {
                mask |= (uint8_t)(1 << ch);
                hold[ch] = 4;
            }
        }
    }
    apu_set_mute_mask(mask);
}

void options_post_nmi(void) {
    update_sound_mute();
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
    case IT_SPEED:
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

/* ---- gameplay: cheats and speeds -----------------------------------------
 * Game loop (demo flag $48 = 00): $3F 04 = play, 08 = death sequence (set
 * only by the ghost-collision check, which also sets $32, $DB and $87).
 * Lives: $67 current player, $77 the other. Stage: $68/$78, FF at game
 * start and incremented at each new stage's setup (stage 0 = level 1).
 * Speeds: 11 pairs (fraction, whole pixels) at $9F-$B4 reloaded from the
 * stage table at every stage/life start; pairs 0-3 are Pac-Man's, 6-10 the
 * ghosts' (normal, frightened, tunnel, Blinky's two "Elroy" speeds). */
#define RAM_ANIM        0x32
#define RAM_LIVES       0x67
#define RAM_STAGE       0x68
#define RAM_LIVES_2     0x77
#define RAM_STAGE_2     0x78
#define RAM_SPEEDS      0x9F
#define RAM_DEATH_TMR   0xDB
#define SCRIPT_PLAY     0x04
#define SCRIPT_DEATH    0x08
#define SPEED_PAIRS     11

static uint16_t s_speed_base[SPEED_PAIRS];
static uint16_t s_speed_written[SPEED_PAIRS];
static int      s_speed_known;

static void apply_speeds(void) {
    for (int p = 0; p < SPEED_PAIRS; p++) {
        int fast = p <= 3 ? g_opt.pac_fast : p >= 6 ? g_opt.ghost_fast : 0;
        uint8_t *r = &g_ram[RAM_SPEEDS + 2 * p];
        uint16_t cur = (uint16_t)(r[0] | r[1] << 8);
        if (!s_speed_known || cur != s_speed_written[p]) s_speed_base[p] = cur;
        uint32_t v = fast ? (uint32_t)s_speed_base[p] * 3 / 2 : s_speed_base[p];
        r[0] = (uint8_t)v;
        r[1] = (uint8_t)(v >> 8);
        s_speed_written[p] = (uint16_t)v;
    }
    s_speed_known = 1;
}

/* "Main menu" from the pause prompt: end the game the way the game ends it
 * (both players out of lives, game-over script 0A), so the title screen
 * comes back through the stock path and the high score is kept. */
#define RAM_FLAG_PAUSE    0x4A
#define RAM_NEW_STAGE     0x69
#define RAM_SND_PAUSE     0x60F
#define SCRIPT_GAME_OVER  0x0A
#define GAME_OVER_SHOWN   0xA0      /* $87 counts up to wrap: ~1.5 s left */

static int s_quitting;

void options_quit_to_title(void) {
    g_ram[RAM_LIVES]     = 0;
    g_ram[RAM_LIVES_2]   = 0;
    g_ram[RAM_NEW_STAGE] = 0;
    g_ram[RAM_TIMER_LO]  = 0;
    g_ram[RAM_SCRIPT]    = SCRIPT_GAME_OVER;
    if (g_ram[RAM_FLAG_PAUSE] & 1) g_ram[RAM_FLAG_PAUSE]++;     /* unpause */
    g_ram[RAM_SND_PAUSE] = 0;
    s_quitting = 1;
}

static void gameplay_frame(void) {
    static uint8_t prev_script = 0xFF, prev_demo = 0xFF;
    static uint8_t snap_anim, snap_timer, snap_87;
    static int level_pending;
    uint8_t demo = g_ram[RAM_FLAG_DEMO], script = g_ram[RAM_SCRIPT];

    if (demo != 0x00) {                 /* title / attract demo: hands off */
        s_quitting = 0;
        prev_demo = demo;
        prev_script = script;
        s_speed_known = 0;
        level_pending = 0;
        return;
    }
    if (prev_demo != 0x00) level_pending = g_opt.start_level > 1;
    if (level_pending) {
        /* New game: the stage reads FF (set a frame or two after the demo
         * flag clears) until the first stage setup increments it. */
        if (g_ram[RAM_STAGE] == 0xFF) {
            uint8_t st = (uint8_t)(g_opt.start_level - 2);
            g_ram[RAM_STAGE] = st;
            if (g_ram[RAM_STAGE_2] == 0xFF) g_ram[RAM_STAGE_2] = st;
            level_pending = 0;
        } else if (script == SCRIPT_PLAY) {
            level_pending = 0;          /* missed it; never touch a live stage */
        }
    }

    if (g_opt.inf_lives) {
        if (g_ram[RAM_LIVES] && g_ram[RAM_LIVES] < 3)     g_ram[RAM_LIVES] = 3;
        if (g_ram[RAM_LIVES_2] && g_ram[RAM_LIVES_2] < 3) g_ram[RAM_LIVES_2] = 3;
    }

    if (g_opt.invincible && prev_script == SCRIPT_PLAY && script == SCRIPT_DEATH) {
        /* A ghost caught Pac-Man last frame: undo the switch to the death
         * sequence so he passes straight through. */
        g_ram[RAM_SCRIPT]    = SCRIPT_PLAY;
        g_ram[RAM_ANIM]      = snap_anim;
        g_ram[RAM_DEATH_TMR] = snap_timer;
        g_ram[RAM_TIMER_LO]  = snap_87;
        script = SCRIPT_PLAY;
    }
    if (script == SCRIPT_PLAY) {
        snap_anim  = g_ram[RAM_ANIM];
        snap_timer = g_ram[RAM_DEATH_TMR];
        snap_87    = g_ram[RAM_TIMER_LO];
    }

    if (s_quitting && script == SCRIPT_GAME_OVER &&
        g_ram[RAM_TIMER_LO] && g_ram[RAM_TIMER_LO] < GAME_OVER_SHOWN)
        g_ram[RAM_TIMER_LO] = GAME_OVER_SHOWN;      /* shorter GAME OVER */

    apply_speeds();
    prev_demo = demo;
    prev_script = script;
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
    gameplay_frame();
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
    case IT_SPEED:  snprintf(buf, n, "%s", *it->val ? "1.5X" : "NORMAL"); break;
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

/* HUD column (right of the maze): HI-SCORE row 3 / value row 5, 1UP row 7 /
 * score row 9, scores ending at column 28. LEVEL follows the same rhythm. */
#define HUD_LABEL_COL  23
#define HUD_VALUE_END  28
#define HUD_LEVEL_ROW  11

static void draw_level_hud(uint32_t *fb) {
    char num[8];
    snprintf(num, sizeof(num), "%d", g_ram[RAM_STAGE] + 1);
    text_draw(fb, HUD_LABEL_COL, HUD_LEVEL_ROW, "LEVEL", TEXT_RED);
    text_draw(fb, HUD_VALUE_END + 1 - (int)strlen(num), HUD_LEVEL_ROW + 2, num, TEXT_WHITE);
}

void options_render(uint32_t *fb) {
    if (g_ram[RAM_FLAG_DEMO] == 0x00 && g_opt.show_level && g_ram[RAM_STAGE] != 0xFF)
        draw_level_hud(fb);
    if (s_quitting)     /* unpaused behind the game's back: hide its PAUSE text */
        text_draw(fb, 23, 17, "     ", TEXT_WHITE);
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
