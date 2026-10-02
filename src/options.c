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
#include "highscores.h"
#include "soundpack.h"
#include "mods.h"
#include "recomp_launcher.h"
#include <ctype.h>
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
#define SCRIPT_SETTLE   0x06    /* ~2 s between the scroll-in and the menu */
#define DEMO_TITLE      0xFF    /* also FF in the attract-demo game, so */
#define NT_TITLE_PLAY   0x20E   /* ...check the title's own "1 PLAYER" text */

/* ---- pad bits (g_controller1_buttons) ---------------------------------- */
#define BTN_A       0x80
#define BTN_B       0x40
#define BTN_SELECT  0x20
#define BTN_START   0x10
#define BTN_UP      0x08
#define BTN_DOWN    0x04
#define BTN_LEFT    0x02
#define BTN_RIGHT   0x01

#define START_LEVEL_MAX 256

PacOptions g_opt;

static const PacOptions k_defaults = {
    .inverse = 0, .music = 1, .sfx = 1, .echo = 0, .modern = 0, .rumble = 0,
    .pac_speed = 0, .show_level = 0, .highscores = 0,
    .inf_lives = 0, .start_level = 1, .invincible = 0,
};

/* ---- menu model -------------------------------------------------------- */
typedef enum {
    SCR_TITLE, SCR_OPTIONS, SCR_VIDEO, SCR_AUDIO, SCR_CONTROLS,
    SCR_EXTRAS, SCR_CHEATS, SCR_RESET, SCR_RESET_SCORES, SCR_MODS, SCR_COUNT
} Screen;

typedef enum {
    IT_PLAY,        /* start a game; lo = 1P/2P mode */
    IT_SECTION,     /* open screen `lo` */
    IT_BACK,
    IT_TOGGLE,      /* *val 0/1 */
    IT_RANGE,       /* *val lo..hi by step, shown with fmt */
    IT_STYLE,       /* *val 0 classic / 1 modern */
    IT_SPEED,       /* *val 0 normal / 1 1.25x / 2 1.5x */
    IT_RESET,       /* reset everything to defaults */
    IT_RESET_SCORES,/* clear the leaderboard */
    IT_QUIT,        /* quit the program */
} ItemKind;

typedef struct {
    const char *label;
    ItemKind    kind;
    int        *val;
    int         lo, hi, step;
    const char *fmt;
    int         runner;         /* 1 = runner setting (config.ini) */
    int         hs_only;        /* shown only while HIGH SCORES is on */
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
    { "QUIT TO DESKTOP", IT_QUIT },
};
static const Item k_options[] = {
    { "VIDEO",            IT_SECTION, 0, SCR_VIDEO },
    { "AUDIO",            IT_SECTION, 0, SCR_AUDIO },
    { "CONTROLS",         IT_SECTION, 0, SCR_CONTROLS },
    { "EXTRAS",           IT_SECTION, 0, SCR_EXTRAS },
    { "CHEATS",           IT_SECTION, 0, SCR_CHEATS },
    { "MODS",             IT_SECTION, 0, SCR_MODS },
    { "RESET TO DEFAULT", IT_SECTION, 0, SCR_RESET },
    { "BACK",             IT_BACK },
};
static const Item k_video[] = {
    { "STRETCH",        IT_TOGGLE, &g_nes_config.stretch,       0, 1, 1, 0, 1 },
    { "FILTER",         IT_TOGGLE, &g_nes_config.linear_filter, 0, 1, 1, 0, 1 },
    { "INTEGER SCALE",  IT_TOGGLE, &g_nes_config.integer_scale, 0, 1, 1, 0, 1 },
    { "HIDE OVERSCAN",  IT_TOGGLE, &g_nes_config.hide_overscan, 0, 1, 1, 0, 1 },
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
    { "MENU STYLE", IT_STYLE,  &g_opt.modern },
    { "RUMBLE",     IT_TOGGLE, &g_opt.rumble },
    { "BACK",       IT_BACK },
};
static const Item k_extras[] = {
    { "SHOW LEVEL",    IT_TOGGLE, &g_opt.show_level },
    { "HIGH SCORES",   IT_TOGGLE, &g_opt.highscores },
    { "RESET HIGH SCORES", IT_SECTION, 0, SCR_RESET_SCORES, 0, 0, 0, 0, 1 },
    { "BACK",          IT_BACK },
};
static const Item k_cheats[] = {
    { "INFINITE LIVES", IT_TOGGLE, &g_opt.inf_lives },
    { "START LEVEL",    IT_RANGE,  &g_opt.start_level, 1, START_LEVEL_MAX, 1, "%d" },
    { "PAC-MAN SPEED",  IT_SPEED,  &g_opt.pac_speed },
    { "INVINCIBLE",     IT_TOGGLE, &g_opt.invincible },
    { "BACK",           IT_BACK },
};
static const Item k_reset[] = {
    { "NO",  IT_BACK },
    { "YES", IT_RESET },
};
static const Item k_reset_scores[] = {
    { "NO",  IT_BACK },
    { "YES", IT_RESET_SCORES },
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
    [SCR_RESET_SCORES] = { "RESET HIGH SCORES", k_reset_scores, N(k_reset_scores), SCR_EXTRAS },
    [SCR_MODS]     = { "MODS",             NULL,       0,             SCR_OPTIONS },
};

/* The title loop is showing: flag FF and the title's "PLAY" still in
 * nametable 0 (the attract-demo game also runs with flag FF, over the maze). */
static int title_showing(void) {
    return g_ram[RAM_FLAG_DEMO] == DEMO_TITLE &&
           !memcmp(&g_ppu_nt[NT_TITLE_PLAY], "PLAY", 4);
}

static Screen  s_scr = SCR_TITLE;
static int     s_sel[SCR_COUNT];
static int     s_was_menu;
static int     s_injected;      /* synthesized Start last frame */
static uint8_t s_prev;
static int     s_hold;          /* frames Left/Right held: auto-repeat, then x10 */

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
    { "Rumble",        &g_opt.rumble },
    { "PacManSpeed",   &g_opt.pac_speed },
    { "ShowLevel",     &g_opt.show_level },
    { "HighScores",    &g_opt.highscores },
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
    fprintf(f, "Mod = %s\n", g_opt.mod);
    fclose(f);
}

void options_save_now(void) { options_save(); }
static void options_load(void);
void options_reload(void) { options_load(); }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void options_load(void) {
    g_opt = k_defaults;
    FILE *f = fopen(options_path(), "r");
    if (!f) return;
    char line[256], key[64];
    int v;
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "Mod = ", 6)) {
            snprintf(g_opt.mod, sizeof(g_opt.mod), "%s", line + 6);
            g_opt.mod[strcspn(g_opt.mod, "\r\n")] = '\0';
            continue;
        }
        if (sscanf(line, " %63[A-Za-z] = %d", key, &v) != 2) continue;
        if (!strcmp(key, "PacManFast")) { g_opt.pac_speed = v ? 2 : 0; continue; }   /* old: on = 1.5x */
        for (int i = 0; i < N(k_keys); i++)
            if (!strcmp(key, k_keys[i].key)) *k_keys[i].val = v;
    }
    fclose(f);
    g_opt.pac_speed   = clampi(g_opt.pac_speed, 0, 2);
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
    mods_apply("");                         /* back to the original game */
    g_nes_config.stretch = 0;
    g_nes_config.linear_filter = 0;
    g_nes_config.integer_scale = 1;
    g_nes_config.hide_overscan = 0;
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
    {
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
            /* Muted category, or a sounds/ file replaces this slot. */
            if ((music && !g_opt.music) || (!music && !g_opt.sfx) ||
                soundpack_replaces(owner[ch])) {
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
        *it->val = !*it->val;
        break;
    case IT_SPEED:
        *it->val = (*it->val + (dir < 0 ? 2 : 1)) % 3;
        break;
    case IT_RANGE: {
        int held = s_hold >= 20;                    /* auto-repeat: stop at the ends */
        int big = s_hold >= 90 && it->hi - it->lo > 50;     /* long lists: x10 */
        int v = *it->val + dir * it->step * (big ? 10 : 1);
        if (v > it->hi) v = held ? it->hi : it->lo;
        if (v < it->lo) v = held ? it->lo : it->hi;
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
        if (it->lo == SCR_MODS) mods_menu_open();
        if (it->lo == SCR_RESET || it->lo == SCR_RESET_SCORES)
            s_sel[it->lo] = 0;                          /* always NO first */
        break;
    case IT_BACK:
        go(k_screens[s_scr].parent);
        break;
    case IT_RESET:
        reset_all();
        go(k_screens[s_scr].parent);
        break;
    case IT_RESET_SCORES:
        hs_reset();
        go(k_screens[s_scr].parent);
        break;
    case IT_QUIT:
        nesrecomp_quit_to_desktop();
        break;
    default:
        change_value(it, +1);
        break;
    }
    return 0;
}

static int item_visible(const Item *it) { return !it->hs_only || g_opt.highscores; }

/* Move the cursor by dir (+1/-1), wrapping and skipping hidden items. */
static void move_sel(const ScreenDef *sd, int *sel, int dir) {
    for (int n = 0; n < sd->count; n++) {
        *sel = (*sel + dir + sd->count) % sd->count;
        if (item_visible(&sd->items[*sel])) return;
    }
}

static void title_menu_input(uint8_t pressed) {
    if (s_scr == SCR_MODS) {                /* its own list (mods.c) */
        if (mods_menu_input(pressed, g_opt.modern)) go(SCR_OPTIONS);
        g_controller1_buttons = 0;
        g_ram[RAM_TIMER_LO] = 1;
        g_ram[RAM_TIMER_HI] = 0;
        return;
    }
    const ScreenDef *sd = &k_screens[s_scr];
    int *sel = &s_sel[s_scr];
    const Item *it = &sd->items[*sel];
    int start_game = 0;

    if (!g_opt.modern) {
        /* Classic: Select moves the cursor, Start picks (the original feel). */
        if (pressed & BTN_SELECT) move_sel(sd, sel, +1);
        else if (pressed & BTN_START) start_game = activate(it);
    } else {
        /* Modern: D-pad moves / changes, A (or Start) picks, B goes back. */
        if (pressed & BTN_UP)   move_sel(sd, sel, -1);
        if (pressed & BTN_DOWN) move_sel(sd, sel, +1);
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
        /* Pac-Man only: the ghosts' speeds change round by round as part
         * of the game's difficulty, so they are left alone. */
        int speed = p <= 3 ? g_opt.pac_speed : 0;
        uint8_t *r = &g_ram[RAM_SPEEDS + 2 * p];
        uint16_t cur = (uint16_t)(r[0] | r[1] << 8);
        if (!s_speed_known || cur != s_speed_written[p]) s_speed_base[p] = cur;
        uint32_t v = speed == 2 ? (uint32_t)s_speed_base[p] * 3 / 2
                   : speed == 1 ? (uint32_t)s_speed_base[p] * 5 / 4 : s_speed_base[p];
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

/* ---- rumble -------------------------------------------------------------
 * Light while the ghosts are frightened ($88 holds one bit per blue ghost,
 * as the collision check reads it), a hard burst when a ghost is eaten (the
 * game switches to its freeze script 06), and medium through the melting
 * death animation (script 08 once $87 turns nonzero, when the death sound
 * starts). Refreshed every few frames with a short duration so it stops by
 * itself if the game stops us calling. */
#define RAM_FRIGHT       0x88
#define SCRIPT_FREEZE    0x06
#define RUMBLE_REFRESH   6          /* frames */
#define RUMBLE_SPAN_MS   200
#define EAT_BURST_FRAMES 18         /* ~300 ms */

enum { RUMBLE_OFF, RUMBLE_LIGHT, RUMBLE_MEDIUM, RUMBLE_HARD };

/* Whose turn it is (1 or 2): only that player's gamepad rumbles. $46 is the
 * current player in a 2-player game ($47 = 1). */
#define RAM_CUR_PLAYER  0x46
static int rumble_player(void) {
    return (g_ram[RAM_GAME_MODE] & 1) && (g_ram[RAM_CUR_PLAYER] & 1) ? 2 : 1;
}

static void rumble_set(int level) {
    static int cur = RUMBLE_OFF, age, who = 1;
    static const uint16_t k_low[]  = { 0, 0x1800, 0x7000, 0xFFFF };
    static const uint16_t k_high[] = { 0, 0x2400, 0x6000, 0xFFFF };
    int player = rumble_player();
    if (player != who) {                /* turn changed: stop the other pad */
        nesrecomp_rumble(who, 0, 0, 0);
        who = player;
        cur = RUMBLE_OFF;
    }
    if (level == cur && (level == RUMBLE_OFF || ++age < RUMBLE_REFRESH)) return;
    cur = level;
    age = 0;
    nesrecomp_rumble(player, k_low[level], k_high[level], level ? RUMBLE_SPAN_MS : 0);
}

/* A tiny blip for each dot eaten: the current player's pellet count ($6A)
 * counts down as dots are eaten (reset at each stage start). Fire-and-forget,
 * only while no other rumble is running. */
#define RAM_PELLETS     0x6A
#define DOT_BLIP_MS     40

static void rumble_frame(uint8_t demo, uint8_t script, uint8_t prev_script) {
    static int burst;
    static uint8_t prev_pellets;
    uint8_t pellets = g_ram[RAM_PELLETS];
    int ate_dot = demo == 0x00 && pellets < prev_pellets;
    prev_pellets = pellets;

    if (!g_opt.rumble || demo != 0x00 || (g_ram[RAM_FLAG_PAUSE] & 1)) {
        burst = 0;
        rumble_set(RUMBLE_OFF);
        return;
    }
    if (script == SCRIPT_FREEZE && prev_script != SCRIPT_FREEZE) burst = EAT_BURST_FRAMES;
    if (burst > 0) {
        burst--;
        rumble_set(RUMBLE_HARD);
    } else if (script == SCRIPT_DEATH && g_ram[RAM_TIMER_LO] != 0) {
        rumble_set(RUMBLE_MEDIUM);
    } else if ((script == SCRIPT_PLAY || script == SCRIPT_FREEZE) && (g_ram[RAM_FRIGHT] & 0x0F)) {
        rumble_set(RUMBLE_LIGHT);
    } else {
        rumble_set(RUMBLE_OFF);
        if (ate_dot) nesrecomp_rumble(rumble_player(), 0x0C00, 0x1800, DOT_BLIP_MS);
    }
}

static void gameplay_frame(void) {
    static uint8_t prev_script = 0xFF, prev_demo = 0xFF;
    static uint8_t snap_anim, snap_timer, snap_87;
    static int level_pending;
    uint8_t demo = g_ram[RAM_FLAG_DEMO], script = g_ram[RAM_SCRIPT];

    if (demo != 0x00) {                 /* title / attract demo: hands off */
        rumble_frame(demo, script, prev_script);
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
    rumble_frame(demo, script, prev_script);
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
    /* Holding Left/Right repeats (for long ranges such as the start level). */
    s_hold = (btn & (BTN_LEFT | BTN_RIGHT)) ? s_hold + 1 : 0;
    if (s_hold >= 20 && s_hold % 4 == 0) pressed |= btn & (BTN_LEFT | BTN_RIGHT);

    int menu = title_showing() && g_ram[RAM_SCRIPT] == SCRIPT_MENU;
    if (menu && !s_was_menu) {
        s_scr = SCR_TITLE;
        s_sel[SCR_TITLE] = g_ram[RAM_GAME_MODE] & 1;
    }
    s_was_menu = menu;
    if (menu) title_menu_input(pressed);
    gameplay_frame();
}

/* ---- drawing ----------------------------------------------------------- */
#define TITLE_ROW0   14     /* 1 PLAYER; items TITLE_STEP px apart */
#define TITLE_COL    12     /* original text column; cursor 2 to the left */
#define OPT_HEADER   14
#define OPT_ROW0     16
#define OPT_COL      7
#define OPT_VAL_END  26     /* values right-aligned to this column */
#define OPT_LAST_ROW 28     /* lowest item row (row 29 is overscan) */

/* 12 px apart (not the original 16) so four items fit above the namco logo. */
#define TITLE_STEP 12

static void draw_title_items(uint32_t *fb, int y_off) {
    text_clear_rows(fb, TITLE_ROW0 - 1, TITLE_ROW0 + 5, y_off);
    for (int i = 0; i < N(k_title); i++) {
        int y = TITLE_ROW0 * 8 + i * TITLE_STEP + y_off;
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
    case IT_SPEED:  snprintf(buf, n, "%s", *it->val == 2 ? "1.5X" : *it->val ? "1.25X" : "NORMAL"); break;
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
    int confirm = s_scr == SCR_RESET || s_scr == SCR_RESET_SCORES;
    if (s_scr == SCR_RESET) text_draw(fb, 6, 17, "RESET ALL SETTINGS?", TEXT_WHITE);
    if (s_scr == SCR_RESET_SCORES) text_draw(fb, 5, 17, "ERASE ALL HIGH SCORES?", TEXT_WHITE);
    if (confirm) row0 = 20;
    /* Two rows apart, closer together when that would run the list past
     * OPT_LAST_ROW (the last row clear of the bottom overscan). */
    int visible = 0;
    for (int i = 0; i < sd->count; i++) visible += item_visible(&sd->items[i]);
    int step = 16;
    if (visible > 1 && row0 * 8 + (visible - 1) * step > OPT_LAST_ROW * 8)
        step = (OPT_LAST_ROW - row0) * 8 / (visible - 1);
    int shown = 0;
    for (int i = 0; i < sd->count; i++) {
        const Item *it = &sd->items[i];
        if (!item_visible(it)) continue;
        int y = row0 * 8 + shown++ * step;
        int col = confirm ? 14 : OPT_COL;
        text_draw_px(fb, col * 8, y, it->label, TEXT_WHITE);
        if (i == s_sel[s_scr]) text_draw_px(fb, (col - 2) * 8, y, "@", TEXT_WHITE);
        char val[16];
        value_text(it, val, sizeof(val));
        if (val[0])
            text_draw_px(fb, (OPT_VAL_END + 1 - (int)strlen(val)) * 8, y, val, TEXT_ORANGE);
    }
}

/* HUD column (right of the maze): HI-SCORE row 3 / value row 5, 1UP row 7 /
 * score row 9, scores ending at column 28. LEVEL follows the same rhythm. */
#define HUD_LABEL_COL  23
#define HUD_VALUE_END  28
#define HUD_LEVEL_ROW  11     /* under the score; 2 players: 2UP is there, */
#define HUD_LEVEL_ROW_2P 15   /* so under 2UP's score instead */

static void draw_level_hud(uint32_t *fb) {
    char num[8];
    int row = (g_ram[RAM_GAME_MODE] & 1) ? HUD_LEVEL_ROW_2P : HUD_LEVEL_ROW;
    snprintf(num, sizeof(num), "%d", g_ram[RAM_STAGE] + 1);
    text_draw(fb, HUD_LABEL_COL, row, "LEVEL", TEXT_RED);
    text_draw(fb, HUD_VALUE_END + 1 - (int)strlen(num), row + 2, num, TEXT_WHITE);
}

void options_render(uint32_t *fb) {
    /* The stage reads FF before a new game's first level is set up, and also
     * on level 256: show it once the game has had a real stage. */
    static int stage_seen;
    if (g_ram[RAM_FLAG_DEMO] != 0x00) stage_seen = 0;
    else if (g_ram[RAM_STAGE] != 0xFF) stage_seen = 1;
    if (g_ram[RAM_FLAG_DEMO] == 0x00 && g_opt.show_level && stage_seen)
        draw_level_hud(fb);
    if (s_quitting)     /* unpaused behind the game's back: hide its PAUSE text */
        text_draw(fb, 23, 17, "     ", TEXT_WHITE);
    if (!title_showing()) return;
    if (g_ram[RAM_SCRIPT] == SCRIPT_SCROLL) {
        /* The title scrolls in from below: the picture sits 240 - scroll_Y
         * pixels lower than at rest, so our lines ride along with it. */
        s_sel[SCR_TITLE] = g_ram[RAM_GAME_MODE] & 1;
        draw_title_items(fb, 240 - g_ram[RAM_SCROLL_Y]);
    } else if (g_ram[RAM_SCRIPT] == SCRIPT_MENU || g_ram[RAM_SCRIPT] == SCRIPT_SETTLE) {
        if (s_scr == SCR_TITLE) draw_title_items(fb, 0);
        else if (s_scr == SCR_MODS) mods_menu_render(fb);
        else draw_screen(fb);
    }
}

void options_post_process(uint32_t *fb) {
    if (!g_opt.inverse) return;
    int n = g_render_width * 240;
    for (int i = 0; i < n; i++) fb[i] ^= 0x00FFFFFFu;
}

int options_title_y(int *y_off) {
    if (!title_showing()) return 0;
    if (g_ram[RAM_SCRIPT] == SCRIPT_SCROLL) { *y_off = 240 - g_ram[RAM_SCROLL_Y]; return 1; }
    if (g_ram[RAM_SCRIPT] == SCRIPT_MENU || g_ram[RAM_SCRIPT] == SCRIPT_SETTLE) { *y_off = 0; return 1; }
    return 0;
}

int options_menu_open(void) {
    return title_showing() && g_ram[RAM_SCRIPT] == SCRIPT_MENU && s_scr != SCR_TITLE;
}

int options_cheats_active(void) {
    return g_opt.inf_lives || g_opt.invincible || g_opt.start_level > 1 ||
           g_opt.pac_speed;
}

/* ---- launcher page ------------------------------------------------------
 * The same settings as the in-game OPTIONS screens, shown on the launcher's
 * Settings page (a recomp-ui host page with in_settings; VIDEO and AUDIO join
 * its Display and Audio cards). Built from the menu tables above so the two can
 * never drift apart. Display scaling and volume are left out: the launcher's
 * own Settings page has them. Everything saves the moment it changes. */
typedef struct { const char *header; const Item *items; int n; } PageSection;
static const PageSection k_page_sections[] = {
    { "VIDEO",    k_video,    N(k_video) },
    { "AUDIO",    k_audio,    N(k_audio) },
    { "CONTROLS", k_controls, N(k_controls) },
    { "EXTRAS",   k_extras,   N(k_extras) },
    { "CHEATS",   k_cheats,   N(k_cheats) },
};

enum { ROW_HEADER, ROW_ITEM, ROW_RESET_HEADER, ROW_RESET, ROW_NOTE };
typedef struct { int kind; const char *header; const Item *it; } PageRow;
static PageRow s_rows[64];
static int     s_nrows;
static int     s_reset_armed;
static char    s_page_status[96];

/* Rows the launcher shows: runner display/volume items are the launcher's. */
static int page_item(const Item *it) {
    if (it->kind == IT_BACK) return 0;
    if (it->runner) return it->val == &g_nes_config.hide_overscan;
    return 1;
}

static void page_build(void) {
    if (s_nrows) return;
    options_load();
    for (int i = 0; i < N(k_page_sections); i++) {
        s_rows[s_nrows++] = (PageRow){ ROW_HEADER, k_page_sections[i].header, NULL };
        if (k_page_sections[i].items == k_cheats)
            s_rows[s_nrows++] = (PageRow){ ROW_NOTE,
                "* Games played with any cheat on don't go on the high score table.", NULL };
        for (int k = 0; k < k_page_sections[i].n; k++)
            if (page_item(&k_page_sections[i].items[k]))
                s_rows[s_nrows++] = (PageRow){ ROW_ITEM, NULL, &k_page_sections[i].items[k] };
    }
    s_rows[s_nrows++] = (PageRow){ ROW_RESET_HEADER, "RESET", NULL };
    s_rows[s_nrows++] = (PageRow){ ROW_RESET, NULL, NULL };
}

static void title_case(char *out, size_t n, const char *in) {
    size_t k = 0;
    for (int first = 1; *in && k + 1 < n; in++) {
        out[k++] = first ? *in : (char)tolower((unsigned char)*in);
        first = *in == ' ' || *in == '-';
    }
    out[k] = '\0';
}

/* Launcher wording: a clearer label than the in-game menu has room for, and
 * a tip for every row. */
typedef struct { const int *val; int kind; const char *label, *help; } PageText;
static const PageText k_page_text[] = {
    { &g_nes_config.hide_overscan, IT_TOGGLE, "Hide screen edges (overscan)",
      "Black out the top and bottom 8 rows, like an old TV did. They sometimes show "
      "leftover bits the game never meant you to see." },
    { &g_opt.inverse, IT_TOGGLE, "Invert colors",
      "Show the game in negative colors (black becomes white and so on)." },
    { &g_opt.music, IT_TOGGLE, "Music",
      "The start tune and the cutscene tunes. Off = silence for those only." },
    { &g_opt.sfx, IT_TOGGLE, "Sound effects",
      "Everything else: eating dots, the siren, ghosts, dying..." },
    { &g_opt.echo, IT_TOGGLE, "Echo effect",
      "Add an echo to all the sound, like playing in a big hall." },
    { &g_opt.modern, IT_STYLE, "Menu buttons",
      "How you move through the game's menus.\nClassic: Select moves the cursor, Start "
      "picks (like the original).\nModern: D-pad moves, A picks, B goes back." },
    { &g_opt.rumble, IT_TOGGLE, "Controller rumble",
      "Shake the gamepad when you eat a ghost, lose a life and so on "
      "(gamepads that can rumble)." },
    { &g_opt.pac_speed, IT_SPEED, "Pac-Man speed",
      "How fast Pac-Man moves: Normal, 1.25 times or 1.5 times as fast. "
      "The ghosts keep their normal speed. Like every cheat, games played "
      "with it don't go on the high score table." },
    { &g_opt.show_level, IT_TOGGLE, "Show level number",
      "Show which level you're on, under the score." },
    { &g_opt.highscores, IT_TOGGLE, "High score table",
      "Keep the top 10 scores with your initials, saved between games." },
    { NULL, IT_SECTION, "Erase all high scores",
      "Clear the high score table. Needs High score table on." },
    { &g_opt.inf_lives, IT_TOGGLE, "Infinite lives",
      "Never run out of lives." },
    { &g_opt.start_level, IT_RANGE, "Start on level",
      "Begin new games on this level instead of level 1." },
    { &g_opt.invincible, IT_TOGGLE, "Invincible",
      "Ghosts can't kill you." },
};

static const PageText *page_text(const Item *it) {
    for (int i = 0; i < N(k_page_text); i++)
        if (k_page_text[i].kind == (int)it->kind && k_page_text[i].val == it->val)
            return &k_page_text[i];
    return NULL;
}

static int page_count(void *ctx) { (void)ctx; page_build(); return s_nrows; }

static int page_get(void *ctx, int i, RecompLauncherCHostRow *r) {
    (void)ctx;
    page_build();
    if (i < 0 || i >= s_nrows) return 0;
    const PageRow *pr = &s_rows[i];
    if (pr->kind == ROW_HEADER || pr->kind == ROW_RESET_HEADER) {
        r->type = RECOMP_HOST_ROW_HEADER;
        snprintf(r->label, sizeof(r->label), "%s", pr->header);
        /* Join the launcher's own Display / Audio cards. */
        if (!strcmp(pr->header, "VIDEO")) snprintf(r->merge, sizeof(r->merge), "display");
        if (!strcmp(pr->header, "AUDIO")) snprintf(r->merge, sizeof(r->merge), "audio");
        return 1;
    }
    if (pr->kind == ROW_NOTE) {
        r->type = RECOMP_HOST_ROW_TEXT;
        r->accent = 1;
        snprintf(r->label, sizeof(r->label), "%s", pr->header);
        return 1;
    }
    if (pr->kind == ROW_RESET) {
        r->type = RECOMP_HOST_ROW_BUTTON;
        snprintf(r->label, sizeof(r->label), "%s", s_reset_armed
                 ? "Click again to reset all game options"
                 : "Reset game options to default");
        snprintf(r->help, sizeof(r->help),
                 "Put every option on this page from the game back to how it came, and "
                 "switch off the mod. Window, display and volume settings are not changed.");
        return 1;
    }
    const Item *it = pr->it;
    title_case(r->label, sizeof(r->label), it->label);
    r->disabled = !item_visible(it);
    switch (it->kind) {
    case IT_TOGGLE:
        r->type = RECOMP_HOST_ROW_TOGGLE;
        r->value = *it->val != 0;
        break;
    case IT_STYLE:
        r->type = RECOMP_HOST_ROW_CHOICE;
        r->value = *it->val != 0;
        r->choice_count = 2;
        break;
    case IT_SPEED:
        r->type = RECOMP_HOST_ROW_CHOICE;
        r->value = *it->val;
        r->choice_count = 3;
        break;
    case IT_RANGE:
        r->type = RECOMP_HOST_ROW_RANGE;
        r->value = *it->val;
        r->min_value = it->lo; r->max_value = it->hi; r->step = it->step;
        snprintf(r->value_text, sizeof(r->value_text), it->fmt ? it->fmt : "%d", *it->val);
        break;
    case IT_SECTION:                    /* RESET HIGH SCORES */
        r->type = RECOMP_HOST_ROW_BUTTON;
        snprintf(r->label, sizeof(r->label), "Erase all high scores");
        break;
    default:
        r->type = RECOMP_HOST_ROW_TEXT;
        break;
    }
    const PageText *pt = page_text(it);
    if (pt) {
        snprintf(r->label, sizeof(r->label), "%s", pt->label);
        snprintf(r->help, sizeof(r->help), "%s", pt->help);
    }
    return 1;
}

static int page_choice(void *ctx, int i, int c, char *out, int cap) {
    (void)ctx;
    if (i < 0 || i >= s_nrows || !s_rows[i].it) return 0;
    static const char *const k_speed[] = { "Normal", "1.25x", "1.5x" };
    const char *lbl = s_rows[i].it->kind == IT_STYLE ? (c ? "Modern" : "Classic")
                                                     : k_speed[c >= 0 && c < 3 ? c : 0];
    snprintf(out, (size_t)cap, "%s", lbl);
    return 1;
}

static int page_set(void *ctx, int i, int v, const char *rom) {
    (void)ctx; (void)rom;
    if (i < 0 || i >= s_nrows) return 0;
    const PageRow *pr = &s_rows[i];
    if (pr->kind == ROW_RESET) {
        if (!s_reset_armed) { s_reset_armed = 1; return 1; }
        s_reset_armed = 0;
        g_opt = k_defaults;                     /* also clears the mod */
        g_nes_config.hide_overscan = 0;
        config_save(config_path());
        options_save();
        snprintf(s_page_status, sizeof(s_page_status), "Game options reset to default.");
        return 1;
    }
    s_reset_armed = 0;
    const Item *it = pr->it;
    if (!it) return 0;
    if (it->kind == IT_SECTION) {
        hs_reset();
        snprintf(s_page_status, sizeof(s_page_status), "High scores erased.");
        return 1;
    }
    *it->val = it->kind == IT_RANGE ? clampi(v, it->lo, it->hi)
             : it->kind == IT_SPEED ? clampi(v, 0, 2) : v != 0;
    if (it->runner) config_save(config_path());     /* read live each frame */
    else options_save();
    s_page_status[0] = '\0';
    return 1;
}

static const char *page_status(void *ctx) { (void)ctx; return s_page_status; }

const RecompLauncherCHostPage *options_launcher_page(void) {
    static const RecompLauncherCHostPage page = {
        NULL, "Options", page_count, page_get, page_choice, page_set, page_status,
        1                               /* in_settings: part of the Settings page */
    };
    return &page;
}
