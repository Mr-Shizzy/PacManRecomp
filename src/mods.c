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
#include "modgen.h"
#include "nes_text.h"
#include "nes_runtime.h"
#include "config.h"
#include "recomp_launcher.h"
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

/* ---- making mods --------------------------------------------------------- */
static char s_msg[96];          /* result of the last action, shown once */

/* A new starter mod from `chr`, made the active choice (not applied). */
static int create_mod(const uint8_t *chr, char *folder, size_t n) {
    char dir[1100], path[1400];
    snprintf(dir, sizeof(dir), "%smods", s_exe);
    modgen_new_name(dir, folder, n);
    snprintf(path, sizeof(path), "%s/%s", dir, folder);
    int w = modgen_write(chr, path);
    scan();
    return w;
}

/* The active mod's folder, or mods/ with none. */
static void active_folder(char *out, size_t n) {
    if (g_opt.mod[0]) snprintf(out, n, "%smods/%s", s_exe, g_opt.mod);
    else snprintf(out, n, "%smods", s_exe);
}

/* ---- MODS screen --------------------------------------------------------
 * Rows 13-29 below the logo: header, a scrolling list on the left, the
 * highlighted mod's preview.png on the right and its description at the
 * bottom. Row 0 of the list is NONE; the last is BACK. */
#define LIST_ROW0    16
#define LIST_ROWS    5          /* visible items (every 2 rows) */
#define LIST_COL     4
#define NAME_CHARS   13
#define PREV_X       136        /* preview box, px */
#define PREV_Y       (LIST_ROW0 * 8)
#define PREV_W       112
#define PREV_H       72         /* rows 16-24 */
#define AUTHOR_ROW   26         /* "BY ..." under the preview */
#define DESC_ROW     27         /* 2 lines, clear of the bottom overscan row */
#define DESC_CHARS   28         /* per line; longer text ends in "..." */

static int s_sel, s_top;
static int s_preview, s_preview_for = -2;

/* NONE, the mods, then BACK. (Making mods lives in the launcher.) */
static int item_count(void) { return s_count + 2; }
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
    s_msg[0] = '\0';
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

/* label(), but text longer than `n` ends in "..." (within the n). */
static void label_cut(char *out, const char *in, int n) {
    if ((int)strlen(in) <= n) { label(out, in, n); return; }
    label(out, in, n - 3);
    int k = (int)strlen(out);
    while (k > 0 && out[k - 1] == ' ') k--;
    strcpy(out + k, "...");
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

    /* The highlighted mod's author (under the preview) and description
     * (two lines at the bottom, "..." when it runs longer). */
    const Mod *m = item_mod(s_sel);
    if (m && m->author[0]) {
        char by[80], line[20];
        snprintf(by, sizeof(by), "BY %s", m->author);
        label_cut(line, by, 14);
        text_draw(fb, PREV_X / 8, AUTHOR_ROW, line, TEXT_ORANGE);
    }
    const char *desc = m ? m->desc : s_sel == 0 ? "THE ORIGINAL GAME" : "";
    char line[DESC_CHARS + 1];
    int cut = (int)strlen(desc);
    if (cut > DESC_CHARS) {                     /* break the first line at a space */
        cut = DESC_CHARS;
        while (cut > 0 && desc[cut] != ' ') cut--;
        if (cut == 0) cut = DESC_CHARS;
    }
    label(line, desc, cut);
    text_draw(fb, 2, DESC_ROW, line, TEXT_WHITE);
    const char *rest = desc + cut;
    while (*rest == ' ') rest++;
    if (*rest) {
        char second[DESC_CHARS + 4];
        label_cut(second, rest, DESC_CHARS);
        text_draw(fb, 2, DESC_ROW + 1, second, TEXT_WHITE);
    }

    load_preview();
    int w, h;
    if (s_preview && nesrecomp_overlay_size(s_preview, &w, &h)) {
        float sc = (float)PREV_W / w;
        if ((float)PREV_H / h < sc) sc = (float)PREV_H / h;
        nesrecomp_overlay_place(s_preview, 1, PREV_X + (PREV_W - w * sc) / 2,
                                PREV_Y + (PREV_H - h * sc) / 2, w * sc, h * sc);
    } else if (item_mod(s_sel)) {
        text_draw(fb, PREV_X / 8 + 2, LIST_ROW0 + 5, "NO PREVIEW", TEXT_RED);
    }
}

/* ---- launcher page --------------------------------------------------------
 * The MODS screen as a launcher page (recomp-ui host page): pick the active
 * mod (used when the game starts), dump the game's pictures into a new mod to
 * edit, open the picked mod's folder. The launcher runs before the game, so
 * the pictures come from the ROM file picked in the launcher. */
enum { MR_H_ACTIVE, MR_PICK, MR_PREVIEW, MR_ABOUT, MR_OPEN, MR_H_MAKE, MR_STEPS, MR_DUMP, MR_COUNT };
static int s_page_version;      /* bumps the preview image on every change */

static void page_ready(void) {
    if (s_exe[0]) return;
    nesrecomp_exe_dir(s_exe, sizeof(s_exe));
    options_reload();
    scan();
}

static int active_index(void) {                  /* 0 = none, else 1 + mod */
    for (int i = 0; i < s_count; i++) if (!strcmp(s_mods[i].folder, g_opt.mod)) return i + 1;
    return 0;
}

static void set_active(const char *folder) {
    snprintf(g_opt.mod, sizeof(g_opt.mod), "%s", folder);
    options_save_now();
    s_page_version++;
}

/* Rows shown now: "Open this mod's folder" only while a mod is picked. */
static int s_page_rows[MR_COUNT], s_page_n;
static void page_layout(void) {
    s_page_n = 0;
    for (int r = 0; r < MR_COUNT; r++)
        if (r != MR_OPEN || active_index()) s_page_rows[s_page_n++] = r;
}

static int mp_count(void *ctx) { (void)ctx; page_ready(); page_layout(); return s_page_n; }

static int mp_get(void *ctx, int i, RecompLauncherCHostRow *r) {
    (void)ctx;
    page_ready();
    if (i < 0 || i >= s_page_n) return 0;
    int a = active_index();
    const Mod *m = a ? &s_mods[a - 1] : NULL;
    switch (s_page_rows[i]) {
    case MR_H_ACTIVE:
        r->type = RECOMP_HOST_ROW_HEADER;
        snprintf(r->label, sizeof(r->label), "MOD IN USE");
        break;
    case MR_PICK:
        r->type = RECOMP_HOST_ROW_CHOICE;
        snprintf(r->label, sizeof(r->label), "Mod");
        r->value = a;
        r->choice_count = s_count + 1;
        snprintf(r->help, sizeof(r->help), "Which mod the game uses. None = the original game. "
                 "Each mod is a folder inside the mods folder next to the game. "
                 "Also in the game under OPTIONS > MODS.");
        break;
    case MR_PREVIEW:
        r->type = RECOMP_HOST_ROW_IMAGE;
        r->value = s_page_version;
        if (m) snprintf(r->image_path, sizeof(r->image_path), "%smods/%s/preview.png", s_exe, m->folder);
        break;
    case MR_ABOUT:
        r->type = RECOMP_HOST_ROW_TEXT;
        if (!m) snprintf(r->label, sizeof(r->label), "The original game.");
        else if (m->author[0]) snprintf(r->label, sizeof(r->label), "%s\nby %s", m->desc, m->author);
        else snprintf(r->label, sizeof(r->label), "%s", m->desc);
        break;
    case MR_OPEN:
        r->type = RECOMP_HOST_ROW_BUTTON;
        snprintf(r->label, sizeof(r->label), "Open this mod's folder");
        snprintf(r->help, sizeof(r->help), "Opens the folder with this mod's pictures (graphics) "
                 "and sounds, to edit them or add your own.");
        break;
    case MR_H_MAKE:
        r->type = RECOMP_HOST_ROW_HEADER;
        snprintf(r->label, sizeof(r->label), "MAKE YOUR OWN MOD");
        break;
    case MR_STEPS:
        r->type = RECOMP_HOST_ROW_TEXT;
        snprintf(r->label, sizeof(r->label),
                 "1. Dump textures: saves every picture in the game as a PNG file in a new "
                 "mod folder (My Mod 1, My Mod 2...), picks that mod and opens the folder.\n"
                 "2. Edit the pictures in any paint program and save them with the same names.\n"
                 "3. Press Play. Your pictures replace the originals.");
        break;
    case MR_DUMP:
        r->type = RECOMP_HOST_ROW_BUTTON;
        snprintf(r->label, sizeof(r->label), "Dump textures");
        snprintf(r->help, sizeof(r->help), "Saves every picture in the game as a PNG, 4x bigger "
                 "than the original, into a new folder in mods. Nothing in the game changes "
                 "until you edit them.");
        break;
    default:
        return 0;
    }
    return 1;
}

static int mp_choice(void *ctx, int i, int c, char *out, int cap) {
    (void)ctx; (void)i;
    snprintf(out, (size_t)cap, "%s", c == 0 ? "None (original)" : c <= s_count ? s_mods[c - 1].name : "");
    return 1;
}

static int mp_set(void *ctx, int i, int v, const char *rom) {
    (void)ctx;
    page_ready();
    if (i < 0 || i >= s_page_n) return 0;
    char path[1400], folder[128];
    s_msg[0] = '\0';
    switch (s_page_rows[i]) {
    case MR_PICK:
        set_active(v >= 1 && v <= s_count ? s_mods[v - 1].folder : "");
        return 1;
    case MR_DUMP: {
        uint8_t chr[0x2000];
        if (!modgen_chr_from_rom(rom, chr)) {
            snprintf(s_msg, sizeof(s_msg), "Pick your Pac-Man ROM first (the pictures come from it).");
            return 0;
        }
        int w = create_mod(chr, folder, sizeof(folder));
        if (w < 0) { snprintf(s_msg, sizeof(s_msg), "Couldn't create the mods folder."); return 0; }
        set_active(folder);
        snprintf(path, sizeof(path), "%smods/%s/graphics", s_exe, folder);
        modgen_open_folder(path);
        snprintf(s_msg, sizeof(s_msg), "Saved %d pictures as the mod \"%s\" and opened its folder. "
                 "Edit them, then press Play.", w, folder);
        return 1;
    }
    case MR_OPEN:
        active_folder(path, sizeof(path));
        modgen_open_folder(path);
        return 1;
    }
    return 0;
}

static const char *mp_status(void *ctx) { (void)ctx; return s_msg; }

const RecompLauncherCHostPage *mods_launcher_page(void) {
    static const RecompLauncherCHostPage page = {
        NULL, "Mods", mp_count, mp_get, mp_choice, mp_set, mp_status, 0
    };
    return &page;
}
