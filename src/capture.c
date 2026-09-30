/*
 * src/capture.c — development tool: record which tiles make up each graphic.
 *
 * Set PACMAN_HD_CAPTURE=<file> to enable (inactive otherwise). Every frame it
 * collects:
 *  - sprite objects: the 8x8 sprites of each color set that touch each other
 *    (Pac-Man, each ghost, fruit, cutscene sprites), as a normalized list of
 *    (dx, dy, tile, h/v flip) plus the color set;
 *  - background tiles: every (tile, color set) pair drawn, with a sample
 *    screen position.
 * Each unique entry is appended to the file once with the frame it was first
 * seen, for building the graphics layout table (tools/hd_layout.py).
 */
#include "capture.h"
#include "nes_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEEN_MAX 8192

static FILE    *s_out;
static uint32_t s_seen[SEEN_MAX];
static int      s_seen_n;

static uint32_t hash_str(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return h ? h : 1;
}

/* 1 the first time a line is seen. */
static int first_time(const char *line) {
    uint32_t h = hash_str(line);
    for (int i = 0; i < s_seen_n; i++) if (s_seen[i] == h) return 0;
    if (s_seen_n < SEEN_MAX) s_seen[s_seen_n++] = h;
    return 1;
}

void capture_init(void) {
    const char *path = getenv("PACMAN_HD_CAPTURE");
    if (path && path[0]) s_out = fopen(path, "w");
}

static uint32_t sprite_pal_key(int pal) {
    const uint8_t *p = &g_ppu_pal[0x10 + pal * 4];
    return 0xFF000000u | (uint32_t)(p[1] & 0x3F) << 16 | (uint32_t)(p[2] & 0x3F) << 8 | (p[3] & 0x3F);
}

static uint32_t bg_pal_key(int pal) {
    const uint8_t *p = &g_ppu_pal[pal * 4];
    return (uint32_t)(g_ppu_pal[0] & 0x3F) << 24 | (uint32_t)(p[1] & 0x3F) << 16 |
           (uint32_t)(p[2] & 0x3F) << 8 | (p[3] & 0x3F);
}

/* Four consecutive OAM entries forming a 16x16 2x2 square of one color set:
 * the game's standard object (Pac-Man, a ghost, eyes, fruit). */
static int is_quad(int i) {
    if (i + 3 >= 64) return 0;
    const uint8_t *e = &g_ppu_oam[i * 4];
    int minx = 255, miny = 255, mask = 0;
    for (int k = 0; k < 4; k++) {
        if (e[k * 4] >= 0xEF || (e[k * 4 + 2] & 3) != (e[2] & 3)) return 0;
        if (e[k * 4 + 3] < minx) minx = e[k * 4 + 3];
        if (e[k * 4] < miny) miny = e[k * 4];
    }
    for (int k = 0; k < 4; k++) {
        int dx = e[k * 4 + 3] - minx, dy = e[k * 4] - miny;
        if ((dx != 0 && dx != 8) || (dy != 0 && dy != 8)) return 0;
        mask |= 1 << ((dy / 8) * 2 + dx / 8);
    }
    return mask == 15;
}

static void emit_group(const int *group, int n) {
    char line[2048];
    int minx = 255, miny = 255;
    for (int g = 0; g < n; g++) {
        const uint8_t *a = &g_ppu_oam[group[g] * 4];
        if (a[3] < minx) minx = a[3];
        if (a[0] < miny) miny = a[0];
    }
    int order[64];
    for (int g = 0; g < n; g++) order[g] = group[g];
    for (int x = 0; x < n; x++)             /* stable key: sort by (dy, dx) */
        for (int y = x + 1; y < n; y++) {
            const uint8_t *a = &g_ppu_oam[order[x] * 4], *b = &g_ppu_oam[order[y] * 4];
            if (b[0] < a[0] || (b[0] == a[0] && b[3] < a[3])) { int t = order[x]; order[x] = order[y]; order[y] = t; }
        }
    int len = snprintf(line, sizeof(line), "S pal=%08X n=%d", sprite_pal_key(g_ppu_oam[order[0] * 4 + 2] & 3), n);
    for (int g = 0; g < n && len < (int)sizeof(line) - 32; g++) {
        const uint8_t *a = &g_ppu_oam[order[g] * 4];
        len += snprintf(line + len, sizeof(line) - len, " %d,%d,%02X,%c%c",
                        a[3] - minx, a[0] - miny, a[1],
                        (a[2] & 0x40) ? 'H' : '-', (a[2] & 0x80) ? 'V' : '-');
    }
    if (first_time(line))
        fprintf(s_out, "%s | f=%llu x=%d y=%d scr=%02X demo=%02X\n", line,
                (unsigned long long)g_frame_count, minx, miny + 1, g_ram[0x3F], g_ram[0x48]);
}

static void capture_sprites(void) {
    int used[64] = { 0 };
    int group[64];
    /* The game's standard objects: 2x2 quads of consecutive entries. */
    for (int i = 0; i < 64; i++) {
        if (used[i] || !is_quad(i)) continue;
        for (int k = 0; k < 4; k++) { group[k] = i + k; used[i + k] = 1; }
        emit_group(group, 4);
    }
    /* Everything else (big cutscene sprites, score pop-ups): same color set,
     * boxes touching. */
    for (int i = 0; i < 64; i++) {
        if (used[i] || g_ppu_oam[i * 4] >= 0xEF) continue;
        int n = 0;
        group[n++] = i;
        used[i] = 1;
        for (int g = 0; g < n; g++) {
            const uint8_t *a = &g_ppu_oam[group[g] * 4];
            for (int j = 0; j < 64; j++) {
                const uint8_t *b = &g_ppu_oam[j * 4];
                if (used[j] || b[0] >= 0xEF || (b[2] & 3) != (a[2] & 3)) continue;
                int dx = abs((int)b[3] - (int)a[3]), dy = abs((int)b[0] - (int)a[0]);
                if (dx <= 8 && dy <= 8) { used[j] = 1; group[n++] = j; }
            }
        }
        emit_group(group, n);
    }
}

static void capture_background(void) {
    /* Nametable 0 as displayed at rest (the maze/title screens don't scroll). */
    char line[128];
    for (int row = 0; row < 30; row++)
        for (int col = 0; col < 32; col++) {
            uint8_t t = g_ppu_nt[row * 32 + col];
            uint8_t at = g_ppu_nt[0x3C0 + (row / 4) * 8 + col / 4];
            int pal = (at >> (((row & 2) << 1) | (col & 2))) & 3;
            snprintf(line, sizeof(line), "B tile=%02X pal=%08X", t, bg_pal_key(pal));
            if (first_time(line))
                fprintf(s_out, "%s | f=%llu col=%d row=%d scr=%02X demo=%02X\n", line,
                        (unsigned long long)g_frame_count, col, row, g_ram[0x3F], g_ram[0x48]);
        }
}

/* Which sprite palette each object uses ($38-$3D: Pac-Man, Blinky, Pinky,
 * Inky, Clyde, fruit) and the sprite palettes themselves. */
static void capture_palettes(void) {
    char line[256];
    int len = snprintf(line, sizeof(line), "P objpal=%d,%d,%d,%d,%d,%d spr=",
                       g_ram[0x38], g_ram[0x39], g_ram[0x3A], g_ram[0x3B], g_ram[0x3C], g_ram[0x3D]);
    for (int i = 0; i < 4; i++)
        len += snprintf(line + len, sizeof(line) - len, "%08X%s", sprite_pal_key(i), i < 3 ? "," : "");
    if (first_time(line))
        fprintf(s_out, "%s | f=%llu stage=%d scr=%02X demo=%02X\n", line,
                (unsigned long long)g_frame_count, g_ram[0x68], g_ram[0x3F], g_ram[0x48]);
}

/* The whole nametable (tiles + attributes) once, at the first READY! of a
 * game: the maze layout for the starter maze.png. */
static void capture_nametable(void) {
    static int done;
    if (done || g_ram[0x48] != 0x00 || g_ram[0x3F] != 0x02) return;
    done = 1;
    fprintf(s_out, "N");
    for (int i = 0; i < 0x400; i++) fprintf(s_out, " %02X", g_ppu_nt[i]);
    fprintf(s_out, " | bgpal=");
    for (int i = 0; i < 16; i++) fprintf(s_out, "%02X", g_ppu_pal[i] & 0x3F);
    fprintf(s_out, "\n");
}

/* The title screen's nametable once (for the logo in the starter dump). */
static void capture_title(void) {
    static int done;
    if (done || g_ram[0x48] != 0xFF || g_ram[0x3F] != 0x02 || g_frame_count < 300) return;
    done = 1;
    fprintf(s_out, "T");
    for (int i = 0; i < 0x400; i++) fprintf(s_out, " %02X", g_ppu_nt[i]);
    fprintf(s_out, " | bgpal=");
    for (int i = 0; i < 16; i++) fprintf(s_out, "%02X", g_ppu_pal[i] & 0x3F);
    fprintf(s_out, "\n");
}

void capture_frame(void) {
    if (!s_out) return;
    capture_title();
    capture_nametable();
    capture_palettes();
    capture_sprites();
    capture_background();
    fflush(s_out);
}
