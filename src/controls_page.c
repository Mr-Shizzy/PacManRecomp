/*
 * src/controls_page.c — the launcher's Controls page: which keys and pad
 * buttons play the game. The keyboard keys are read from keybinds.ini next to
 * the exe (re-read when it changes), so the page follows any remapping made
 * with Settings > Player 1 > Configure.
 */
#include "controls_page.h"
#include "config.h"
#include "recomp_launcher.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

/* Player 1's keyboard keys, as SDL key names from keybinds.ini [player1]. */
enum { K_A, K_B, K_SELECT, K_START, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_COUNT };
static const char *const k_ini_keys[K_COUNT] = {
    "a", "b", "select", "start", "up", "down", "left", "right"
};
static const char *const k_defaults[K_COUNT] = {
    "Z", "X", "\\", "Return", "Up", "Down", "Left", "Right"
};
static char s_keys[K_COUNT][32];
static time_t s_loaded_mtime = -1;

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static void load_keys(void) {
    char path[1100], dir[1024];
    nesrecomp_exe_dir(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%skeybinds.ini", dir);
    struct stat st;
    time_t mtime = stat(path, &st) == 0 ? st.st_mtime : 0;
    if (mtime == s_loaded_mtime) return;
    s_loaded_mtime = mtime;

    for (int k = 0; k < K_COUNT; ++k)
        snprintf(s_keys[k], sizeof(s_keys[k]), "%s", k_defaults[k]);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    int in_p1 = 0;
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (*s == '[') { in_p1 = !strncmp(s, "[player1]", 9); continue; }
        char *eq = strchr(s, '=');
        if (!in_p1 || !eq || *s == '#') continue;
        *eq = '\0';
        char *key = trim(s), *val = trim(eq + 1);
        for (int k = 0; k < K_COUNT; ++k)
            if (!strcmp(key, k_ini_keys[k]) && *val)
                snprintf(s_keys[k], sizeof(s_keys[k]), "%s", val);
    }
    fclose(f);
}

/* A key name as players know it. */
static const char *friendly(const char *name) {
    if (!strcmp(name, "Return"))    return "Enter";
    if (!strcmp(name, "\\"))        return "\\ (backslash, above Enter)";
    if (!strcmp(name, "Backslash")) return "\\ (backslash, above Enter)";
    return name;
}

static int is_arrows(void) {
    return !strcmp(s_keys[K_UP], "Up") && !strcmp(s_keys[K_DOWN], "Down") &&
           !strcmp(s_keys[K_LEFT], "Left") && !strcmp(s_keys[K_RIGHT], "Right");
}

enum {
    R_KB_HEADER, R_MOVE, R_START, R_SELECT, R_A, R_B, R_ESC,
    R_PAD_HEADER, R_PAD_MOVE, R_PAD_START, R_PAD_SELECT,
    R_CHANGE_HEADER, R_CHANGE, R_COUNT
};

static int page_count(void *ctx) { (void)ctx; return R_COUNT; }

static int page_get(void *ctx, int i, RecompLauncherCHostRow *r) {
    (void)ctx;
    if (i < 0 || i >= R_COUNT) return 0;
    load_keys();
    r->type = RECOMP_HOST_ROW_TEXT;
    switch (i) {
    case R_KB_HEADER:
        r->type = RECOMP_HOST_ROW_HEADER;
        snprintf(r->label, sizeof(r->label), "KEYBOARD");
        break;
    case R_MOVE:
        if (is_arrows())
            snprintf(r->label, sizeof(r->label), "Move: arrow keys");
        else
            snprintf(r->label, sizeof(r->label), "Move: %s, %s, %s, %s (up, down, left, right)",
                     friendly(s_keys[K_UP]), friendly(s_keys[K_DOWN]),
                     friendly(s_keys[K_LEFT]), friendly(s_keys[K_RIGHT]));
        break;
    case R_START:
        snprintf(r->label, sizeof(r->label), "Start a game / pause: %s", friendly(s_keys[K_START]));
        break;
    case R_SELECT:
        snprintf(r->label, sizeof(r->label), "Select (moves the menu cursor): %s",
                 friendly(s_keys[K_SELECT]));
        break;
    case R_A:
        snprintf(r->label, sizeof(r->label), "A button: %s", friendly(s_keys[K_A]));
        break;
    case R_B:
        snprintf(r->label, sizeof(r->label), "B button: %s", friendly(s_keys[K_B]));
        break;
    case R_ESC:
        snprintf(r->label, sizeof(r->label),
                 "Menu while playing (main menu, launcher, quit): Esc, or pause and press Select");
        break;
    case R_PAD_HEADER:
        r->type = RECOMP_HOST_ROW_HEADER;
        snprintf(r->label, sizeof(r->label), "CONTROLLER");
        break;
    case R_PAD_MOVE:
        snprintf(r->label, sizeof(r->label), "Move: d-pad or left stick");
        break;
    case R_PAD_START:
        snprintf(r->label, sizeof(r->label), "Start a game / pause: Start (Menu on Xbox pads)");
        break;
    case R_PAD_SELECT:
        snprintf(r->label, sizeof(r->label), "Select: Back (View on Xbox pads)");
        break;
    case R_CHANGE_HEADER:
        r->type = RECOMP_HOST_ROW_HEADER;
        snprintf(r->label, sizeof(r->label), "CHANGE THE KEYS");
        break;
    case R_CHANGE:
        snprintf(r->label, sizeof(r->label),
                 "Go to Settings, then Configure under Player 1. This page shows the new keys "
                 "straight away.");
        break;
    }
    return 1;
}

static int page_set(void *ctx, int i, int v, const char *rom) {
    (void)ctx; (void)i; (void)v; (void)rom;
    return 0;
}

const RecompLauncherCHostPage *controls_launcher_page(void) {
    static const RecompLauncherCHostPage page = {
        NULL, "Controls", page_count, page_get, NULL, page_set, NULL,
        0                               /* a page of its own in the menu */
    };
    return &page;
}
