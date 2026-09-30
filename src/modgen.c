/*
 * src/modgen.c — make a ready-to-paint starter mod from the game's own tiles.
 *
 * The C twin of tools/make_mod_template.py, so players can start a mod from
 * OPTIONS > MODS or the launcher without installing anything: every graphic
 * in the layout table (hd_layout.h) drawn in its original colors, enlarged
 * MODGEN_SCALE times, one PNG each; the maze walls; a preview; mod.txt; and a
 * sounds/ folder with the list of names. Missing files only: running it again
 * on an existing mod adds new pictures and never touches painted ones.
 */
#include "modgen.h"
#include "hd_layout.h"
#include "hdbuild.h"
#include "nes_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#  include <direct.h>
#  include <windows.h>
#  include <shellapi.h>
#  define MKDIR(p) _mkdir(p)
#else
#  define MKDIR(p) mkdir(p, 0755)
#endif

static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

/* mkdir -p for the folders above `path` (a file path). */
static void make_parents(const char *path) {
    char tmp[1400];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *c = tmp + 1; *c; c++)
        if (*c == '/' || *c == '\\') {
            char keep = *c;
            *c = '\0';
            if (!exists(tmp)) MKDIR(tmp);
            *c = keep;
        }
}

typedef struct { int w, h; unsigned char *px; } Canvas;      /* RGBA */

static Canvas canvas(int w, int h, uint32_t fill) {
    Canvas c = { w, h, (unsigned char *)malloc((size_t)w * h * 4) };
    for (int i = 0; c.px && i < w * h; i++) {
        c.px[i * 4 + 0] = (unsigned char)(fill >> 16);
        c.px[i * 4 + 1] = (unsigned char)(fill >> 8);
        c.px[i * 4 + 2] = (unsigned char)fill;
        c.px[i * 4 + 3] = (unsigned char)(fill >> 24);
    }
    return c;
}

/* colors[v] for pixel value v (1-3), ARGB; 0 = leave transparent. */
static void draw_tile(Canvas *c, const uint8_t *chr, int x0, int y0, int tile, int flags,
                      const uint32_t colors[4]) {
    const uint8_t *t = chr + tile * 16;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int v = ((t[y] >> (7 - x)) & 1) | (((t[y + 8] >> (7 - x)) & 1) << 1);
            if (!v || !colors[v]) continue;
            int px = x0 + ((flags & 1) ? 7 - x : x), py = y0 + ((flags & 2) ? 7 - y : y);
            if (px < 0 || py < 0 || px >= c->w || py >= c->h) continue;
            unsigned char *d = c->px + ((size_t)py * c->w + px) * 4;
            d[0] = (unsigned char)(colors[v] >> 16);
            d[1] = (unsigned char)(colors[v] >> 8);
            d[2] = (unsigned char)colors[v];
            d[3] = 255;
        }
}

static void pal_colors(uint32_t pal, uint32_t colors[4]) {
    colors[0] = 0;
    for (int v = 1; v <= 3; v++) colors[v] = 0xFF000000u | g_nes_palette[(pal >> ((3 - v) * 8)) & 0x3F];
}

/* Save c enlarged `s` times (nearest neighbor) unless the file exists. */
static int save_scaled(const Canvas *c, const char *path, int s) {
    if (exists(path)) return 0;
    make_parents(path);
    Canvas big = canvas(c->w * s, c->h * s, 0);
    if (!big.px) return 0;
    for (int y = 0; y < big.h; y++)
        for (int x = 0; x < big.w; x++)
            memcpy(big.px + ((size_t)y * big.w + x) * 4,
                   c->px + ((size_t)(y / s) * c->w + x / s) * 4, 4);
    int ok = hdbuild_write_png(path, big.w, big.h, big.px);
    free(big.px);
    return ok;
}

static void write_text(const char *path, const char *text) {
    if (exists(path)) return;
    make_parents(path);
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

int modgen_write(const uint8_t chr[0x2000], const char *mod_dir) {
    char path[1400];
    snprintf(path, sizeof(path), "%s/graphics/", mod_dir);
    make_parents(path);
    if (!exists(mod_dir)) return -1;

    int written = 0;
    for (int i = 0; i < HD_GRAPHICS_N; i++) {
        const HdGraphic *g = &hd_graphics[i];
        if (g->mirror_of || g->optional) continue;  /* made from its partner / extra */
        int dup = 0;                                /* one file per name */
        for (int k = 0; k < i && !dup; k++) dup = !strcmp(hd_graphics[k].name, g->name);
        if (dup) continue;
        uint32_t colors[4];
        if (g->tint) colors[0] = 0, colors[1] = colors[2] = colors[3] = 0xFFFFFFFFu;
        else pal_colors(g->npals ? g->pals[0] : g->preview, colors);
        Canvas c = canvas(g->w, g->h, 0);
        if (!c.px) continue;
        for (int p = 0; p < g->npieces; p++)
            draw_tile(&c, chr + (g->sprite ? 0x1000 : 0), g->pieces[p].dx, g->pieces[p].dy,
                      g->pieces[p].tile, g->pieces[p].flags & 3, colors);
        snprintf(path, sizeof(path), "%s/graphics/%s.png", mod_dir, g->name);
        written += save_scaled(&c, path, MODGEN_SCALE);
        free(c.px);
    }

    /* The maze: walls only (dots and pellets are separate: they disappear). */
    for (int flash = 0; flash < 2; flash++) {
        uint32_t colors[4];
        pal_colors(flash ? HD_MAZE_PAL_FLASH : HD_MAZE_PAL_NORMAL, colors);
        Canvas c = canvas(HD_MAZE_W, HD_MAZE_H, 0xFF000000u);
        if (!c.px) continue;
        for (int r = 0; r < HD_MAZE_ROWS; r++)
            for (int col = 0; col < HD_MAZE_COLS; col++) {
                int t = hd_maze_tiles[r][col];
                for (unsigned k = 0; k < sizeof(hd_maze_walls); k++)
                    if (hd_maze_walls[k] == t) { draw_tile(&c, chr, col * 8, r * 8, t, 0, colors); break; }
            }
        snprintf(path, sizeof(path), "%s/graphics/%s.png", mod_dir, flash ? "maze_flash" : "maze");
        written += save_scaled(&c, path, MODGEN_SCALE);
        if (!flash) {
            snprintf(path, sizeof(path), "%s/preview.png", mod_dir);
            save_scaled(&c, path, 1);
        }
        free(c.px);
    }

    /* The title logo (the PAC-MAN box and TM), as graphics/logo.png. */
    {
        Canvas c = canvas(HD_LOGO_COLS * 8, HD_LOGO_ROWS * 8, 0);
        if (c.px) {
            for (int r = 0; r < HD_LOGO_ROWS; r++)
                for (int col = 0; col < HD_LOGO_COLS; col++) {
                    uint32_t colors[4];
                    pal_colors(hd_logo_pals[r][col], colors);
                    draw_tile(&c, chr, col * 8, r * 8, hd_logo_tiles[r][col], 0, colors);
                }
            snprintf(path, sizeof(path), "%s/graphics/logo.png", mod_dir);
            written += save_scaled(&c, path, MODGEN_SCALE);
            free(c.px);
        }
    }

    snprintf(path, sizeof(path), "%s/sounds/README.txt", mod_dir);
    write_text(path,
        "Put replacement sounds here as WAV files named:\n"
        "start extra_life death dot fruit eat_ghost eyes fright siren\n"
        "intermission pause music (e.g. start.wav). See docs/MODDING.md.\n"
        "\n"
        "The originals folder has the game's own sounds, recorded, with the\n"
        "same names: play one to hear which sound a name is, and how long it\n"
        "lasts. The game ignores that folder. To replace a sound, put your\n"
        "WAV here (not in originals); you can start from a copy of the original.\n"
        "music has no original: it's an extra, a loop during play.\n");
    snprintf(path, sizeof(path), "%s/mod.txt", mod_dir);
    if (!exists(path)) {
        const char *name = mod_dir + strlen(mod_dir);
        while (name > mod_dir && name[-1] != '/' && name[-1] != '\\') name--;
        char text[512];
        snprintf(text, sizeof(text), "name = %s\nauthor = \ndescription = My Pac-Man mod.\n", name);
        write_text(path, text);
    }
    printf("[ModGen] %s: %d new pictures\n", mod_dir, written);
    return written;
}

int modgen_chr_from_rom(const char *rom_path, uint8_t chr[0x2000]) {
    FILE *f = rom_path && rom_path[0] ? fopen(rom_path, "rb") : NULL;
    if (!f) return 0;
    unsigned char hdr[16];
    int ok = fread(hdr, 1, 16, f) == 16 && !memcmp(hdr, "NES\x1a", 4) && hdr[5] >= 1;
    if (ok) {
        long off = 16 + (long)hdr[4] * 16384 + ((hdr[6] & 4) ? 512 : 0);
        ok = fseek(f, off, SEEK_SET) == 0 && fread(chr, 1, 0x2000, f) == 0x2000;
    }
    fclose(f);
    return ok;
}

void modgen_new_name(const char *mods_dir, char *out, size_t n) {
    char p[1400];
    for (int i = 1; i < 1000; i++) {
        snprintf(out, n, "My Mod %d", i);
        snprintf(p, sizeof(p), "%s/%s", mods_dir, out);
        if (!exists(p)) return;
    }
}

void modgen_open_folder(const char *path) {
    if (getenv("PACMAN_NO_OPEN_FOLDER")) return;   /* automated tests */
    char p[1400];
    snprintf(p, sizeof(p), "%s", path);
    make_parents(strcat(p, "/"));
#ifdef _WIN32
    for (char *c = p; *c; c++) if (*c == '/') *c = '\\';
    ShellExecuteA(NULL, "open", p, NULL, NULL, SW_SHOWNORMAL);
#else
    char cmd[1500];
    snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" &", path);
    if (system(cmd)) {}
#endif
}
