/*
 * src/mods.c — selectable mods in <exe>/mods/<name>/ (OPTIONS > MODS).
 *
 * A mod is a folder in mods/ holding any of:
 *   graphics/      individual images (hdbuild.c turns them into an HD pack)
 *   graphics/hires.txt or hires.txt   a ready-made Mesen HD pack
 *   graphics/logo.png                 replacement title logo
 *   sounds/        replacement sounds / music (soundpack.c)
 *   mod.txt        optional "name = / author = / description =" lines
 *   preview.png    optional thumbnail shown on the MODS screen
 * One mod is active at a time, applied live and saved (pacman_options.ini
 * "Mod = <folder>"). None = the original game; loose legacy files next to
 * the exe (hdpack/, sounds/, logo.png) still apply then.
 */
#include "mods.h"
#include "options.h"
#include "soundpack.h"
#include "logo.h"
#include "hdbuild.h"
#include "nes_text.h"
#include "nes_runtime.h"
#include "config.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#  include <io.h>
#else
#  include <dirent.h>
#endif

#define MAX_MODS    64

typedef struct {
    char folder[128];
    char name[64];
    char author[64];
    char desc[160];
} Mod;

static Mod  s_mods[MAX_MODS];
static int  s_count;
static char s_exe[1024];

/* ---- files ------------------------------------------------------------- */
static int is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && (st.st_mode & S_IFDIR);
}

static int is_file(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && !(st.st_mode & S_IFDIR);
}

static void trim(char *s) {
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    char *b = s;
    while (*b && isspace((unsigned char)*b)) b++;
    memmove(s, b, strlen(b) + 1);
}

static void read_mod_txt(Mod *m) {
    char path[1400], line[256];
    snprintf(path, sizeof(path), "%smods/%s/mod.txt", s_exe, m->folder);
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = '\0';
        char *key = line, *val = eq + 1;
        trim(key); trim(val);
        if (!strcmp(key, "name"))        snprintf(m->name, sizeof(m->name), "%s", val);
        else if (!strcmp(key, "author")) snprintf(m->author, sizeof(m->author), "%s", val);
        else if (!strcmp(key, "description")) snprintf(m->desc, sizeof(m->desc), "%s", val);
    }
    fclose(f);
}

static void add_mod(const char *folder) {
    if (s_count >= MAX_MODS || folder[0] == '.') return;
    Mod *m = &s_mods[s_count++];
    memset(m, 0, sizeof(*m));
    snprintf(m->folder, sizeof(m->folder), "%s", folder);
    snprintf(m->name, sizeof(m->name), "%s", folder);
    read_mod_txt(m);
}

static int cmp_mod(const void *a, const void *b) {
    return strcmp(((const Mod *)a)->name, ((const Mod *)b)->name);
}

static void scan(void) {
    s_count = 0;
#ifdef _WIN32
    char pat[1100];
    snprintf(pat, sizeof(pat), "%smods\\*", s_exe);
    struct _finddata_t fd;
    intptr_t h = _findfirst(pat, &fd);
    if (h != -1) {
        do {
            if ((fd.attrib & _A_SUBDIR) && strcmp(fd.name, ".") && strcmp(fd.name, ".."))
                add_mod(fd.name);
        } while (_findnext(h, &fd) == 0);
        _findclose(h);
    }
#else
    char dir[1100];
    snprintf(dir, sizeof(dir), "%smods", s_exe);
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            char p[1400];
            snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
            if (is_dir(p)) add_mod(e->d_name);
        }
        closedir(d);
    }
#endif
    qsort(s_mods, (size_t)s_count, sizeof(Mod), cmp_mod);
}

/* ---- applying ---------------------------------------------------------- */
void mods_apply(const char *folder) {
    char base[1300], p[1400];
    if (folder && folder[0]) {
        snprintf(base, sizeof(base), "%smods/%s", s_exe, folder);
        if (!is_dir(base)) folder = "";
    }
    snprintf(g_opt.mod, sizeof(g_opt.mod), "%s", folder ? folder : "");
    options_save_now();

    if (!g_opt.mod[0]) {
        /* None: stock, plus any loose legacy files next to the exe. */
        snprintf(p, sizeof(p), "%ssounds", s_exe);
        soundpack_load(is_dir(p) ? p : NULL);
        snprintf(p, sizeof(p), "%slogo.png", s_exe);
        logo_load(is_file(p) ? p : NULL);
        nesrecomp_hdpack_set_dir(NULL);         /* config / <exe>/hdpack */
        return;
    }

    snprintf(p, sizeof(p), "%s/sounds", base);
    soundpack_load(is_dir(p) ? p : NULL);

    snprintf(p, sizeof(p), "%s/graphics/logo.png", base);
    if (!is_file(p)) snprintf(p, sizeof(p), "%s/logo.png", base);
    logo_load(is_file(p) ? p : NULL);

    /* Graphics: a ready-made Mesen pack wins, else build one from images. */
    char pack[1400];
    snprintf(p, sizeof(p), "%s/hires.txt", base);
    snprintf(pack, sizeof(pack), "%s/graphics/hires.txt", base);
    if (is_file(p)) {
        nesrecomp_hdpack_set_dir(base);
    } else if (is_file(pack)) {
        snprintf(pack, sizeof(pack), "%s/graphics", base);
        nesrecomp_hdpack_set_dir(pack);
    } else {
        snprintf(p, sizeof(p), "%s/graphics", base);
        if (is_dir(p) && hdbuild_make(p, pack, sizeof(pack))) nesrecomp_hdpack_set_dir(pack);
        else nesrecomp_hdpack_set_dir("");
    }
    printf("[Mods] using %s\n", g_opt.mod);
}

void mods_init(void) {
    nesrecomp_exe_dir(s_exe, sizeof(s_exe));
    scan();
    mods_apply(g_opt.mod);
}

/* ---- MODS screen --------------------------------------------------------
 * Rows 13-29 below the logo: header, a scrolling list on the left, the
 * highlighted mod's preview.png on the right and its description at the
 * bottom. Row 0 of the list is NONE; the last is BACK. */
#define LIST_ROW0    16
#define LIST_ROWS    6          /* visible items (every 2 rows) */
#define LIST_COL     4
#define NAME_CHARS   12
#define PREV_X       128        /* preview box, px */
#define PREV_Y       (LIST_ROW0 * 8)
#define PREV_W       112
#define PREV_H       88
#define DESC_ROW     28

static int s_sel, s_top;
static int s_preview, s_preview_for = -2;

static int item_count(void) { return s_count + 2; }             /* NONE + mods + BACK */
static int is_back(int i) { return i == s_count + 1; }
static const Mod *item_mod(int i) { return i >= 1 && i <= s_count ? &s_mods[i - 1] : NULL; }

static int item_active(int i) {
    const Mod *m = item_mod(i);
    if (i == 0) return !g_opt.mod[0];
    return m && !strcmp(m->folder, g_opt.mod);
}

static void load_preview(void) {
    if (s_preview_for == s_sel) return;
    s_preview_for = s_sel;
    if (s_preview) { nesrecomp_overlay_free(s_preview); s_preview = 0; }
    const Mod *m = item_mod(s_sel);
    if (!m) return;
    char p[1400];
    snprintf(p, sizeof(p), "%smods/%s/preview.png", s_exe, m->folder);
    if (is_file(p)) s_preview = nesrecomp_overlay_load_png(p);
}

void mods_menu_open(void) {
    scan();                         /* pick up mods added while running */
    s_sel = 0;
    for (int i = 0; i < item_count(); i++) if (item_active(i)) s_sel = i;
    s_top = s_sel >= LIST_ROWS ? s_sel - LIST_ROWS + 1 : 0;
    s_preview_for = -2;
}

void mods_menu_close(void) {
    if (s_preview) { nesrecomp_overlay_free(s_preview); s_preview = 0; }
    s_preview_for = -2;
}

#define BTN_A       0x80
#define BTN_B       0x40
#define BTN_SELECT  0x20
#define BTN_START   0x10
#define BTN_UP      0x08
#define BTN_DOWN    0x04

int mods_menu_input(uint8_t pressed, int modern) {
    int n = item_count(), pick = 0;
    if (!modern) {
        if (pressed & BTN_SELECT) s_sel = (s_sel + 1) % n;
        pick = pressed & BTN_START;
    } else {
        if (pressed & BTN_UP)   s_sel = (s_sel + n - 1) % n;
        if (pressed & BTN_DOWN) s_sel = (s_sel + 1) % n;
        if (pressed & BTN_B) { mods_menu_close(); return 1; }
        pick = pressed & (BTN_A | BTN_START);
    }
    if (s_sel < s_top) s_top = s_sel;
    if (s_sel >= s_top + LIST_ROWS) s_top = s_sel - LIST_ROWS + 1;
    if (pick) {
        if (is_back(s_sel)) { mods_menu_close(); return 1; }
        const Mod *m = item_mod(s_sel);
        mods_apply(m ? m->folder : "");
    }
    return 0;
}

/* Upper-case copy limited to the font and `n` characters. */
static void label(char *out, const char *in, int n) {
    int k = 0;
    for (; *in && k < n; in++) {
        char c = (char)toupper((unsigned char)*in);
        out[k++] = (isalnum((unsigned char)c) || strchr(" .-:?!%", c)) ? c : ' ';
    }
    out[k] = '\0';
}

void mods_menu_render(uint32_t *fb) {
    char buf[64];
    text_clear_rows(fb, 13, 29, 0);
    text_draw(fb, 14, 14, "MODS", TEXT_SALMON);
    for (int r = 0; r < LIST_ROWS && s_top + r < item_count(); r++) {
        int i = s_top + r, row = LIST_ROW0 + r * 2;
        const Mod *m = item_mod(i);
        label(buf, i == 0 ? "NONE" : is_back(i) ? "BACK" : m->name, NAME_CHARS);
        text_draw(fb, LIST_COL, row, buf, item_active(i) ? TEXT_ORANGE : TEXT_WHITE);
        if (i == s_sel) text_draw(fb, LIST_COL - 2, row, "@", TEXT_WHITE);
    }
    if (s_top > 0) text_draw(fb, LIST_COL + 6, LIST_ROW0 - 1, "-", TEXT_WHITE);
    if (s_top + LIST_ROWS < item_count())
        text_draw(fb, LIST_COL + 6, LIST_ROW0 + LIST_ROWS * 2 - 1, "-", TEXT_WHITE);

    /* Description (and author) of the highlighted mod, two lines. */
    const Mod *m = item_mod(s_sel);
    const char *desc = m ? m->desc : s_sel == 0 ? "THE ORIGINAL GAME" : "";
    /* Two lines of 28, broken at a space when possible. */
    char line[29];
    int cut = (int)strlen(desc);
    if (cut > 28) {
        cut = 28;
        while (cut > 0 && desc[cut] != ' ') cut--;
        if (cut == 0) cut = 28;
    }
    label(line, desc, cut);
    text_draw(fb, 2, DESC_ROW, line, TEXT_WHITE);
    const char *rest = desc + cut;
    while (*rest == ' ') rest++;
    if (m && m->author[0]) {
        char by[40];
        snprintf(by, sizeof(by), "BY %s", m->author);
        label(line, by, 14);
        text_draw(fb, PREV_X / 8, LIST_ROW0 + 11, line, TEXT_ORANGE);
    }
    if (*rest) {
        label(line, rest, 28);
        text_draw(fb, 2, DESC_ROW + 1, line, TEXT_WHITE);
    }

    load_preview();
    int w, h;
    if (s_preview && nesrecomp_overlay_size(s_preview, &w, &h)) {
        float sc = (float)PREV_W / w;
        if ((float)PREV_H / h < sc) sc = (float)PREV_H / h;
        nesrecomp_overlay_place(s_preview, 1, PREV_X + (PREV_W - w * sc) / 2,
                                PREV_Y + (PREV_H - h * sc) / 2, w * sc, h * sc);
    } else if (s_sel >= 1 && !is_back(s_sel)) {
        text_draw(fb, PREV_X / 8 + 2, LIST_ROW0 + 5, "NO PREVIEW", TEXT_RED);
    }
}
