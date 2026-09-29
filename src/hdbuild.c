/*
 * src/hdbuild.c — turn a mod's graphics/ folder of individual images into an
 * HD pack the runner can load.
 *
 * The layout table (hd_layout.h, generated from tools/hd_layout.json) says
 * which 8x8 tiles, flips and color sets make up every named graphic. For each
 * image present we scale it to the pack's resolution, cut it into its tiles
 * (pre-flipped where the game draws the tile flipped, since the HD engine
 * flips it back) and register one HD tile per color set. A missing file falls
 * back to its mirror partner (left_* <- right_* flipped), else stays original.
 *
 * maze.png / maze_flash.png become full-screen background layers shown while
 * the maze is on screen (normal / level-clear flash colors), with the maze's
 * wall tiles made transparent and its other tiles (dots, blanks, text) drawn
 * with transparent backgrounds so the picture shows through.
 *
 * Output: <graphics>/.hdcache/{hires.txt, atlas.png, maze*.png}, rebuilt on
 * every apply.
 */
#include "hdbuild.h"
#include "hd_layout.h"
#include "nes_runtime.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#  include <direct.h>
#  define MKDIR(p) _mkdir(p)
#else
#  define MKDIR(p) mkdir(p, 0755)
#endif

#include "stb_image.h"                  /* stbi_load (runner) */

/* ---- RGBA PNG writer ---------------------------------------------------------
 * The runner's bundled writer only does RGB and stores uncompressed, so this
 * one does RGBA with a small deflate (LZ77 + fixed Huffman codes): starter
 * mods and packs are mostly flat color and transparency and shrink ~20x. */
static uint32_t crc_tab[256];
static uint32_t png_crc(const unsigned char *b, size_t n, uint32_t c) {
    if (!crc_tab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_tab[i] = v;
        }
    for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return c;
}
static void put32(FILE *f, uint32_t v) {
    unsigned char b[4] = { (unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v };
    fwrite(b, 1, 4, f);
}
static void chunk(FILE *f, const char *type, const unsigned char *data, uint32_t n) {
    put32(f, n);
    fwrite(type, 1, 4, f);
    if (n) fwrite(data, 1, n, f);
    uint32_t c = png_crc((const unsigned char *)type, 4, 0xFFFFFFFFu);
    put32(f, png_crc(data, n, c) ^ 0xFFFFFFFFu);
}

typedef struct { unsigned char *buf; size_t n, cap; uint32_t bits; int nb; } Bits;
static void put_bits(Bits *w, uint32_t v, int n) {         /* LSB first */
    w->bits |= v << w->nb;
    w->nb += n;
    while (w->nb >= 8) {
        if (w->n == w->cap) {
            w->cap = w->cap ? w->cap * 2 : 65536;
            w->buf = (unsigned char *)realloc(w->buf, w->cap);
        }
        w->buf[w->n++] = (unsigned char)w->bits;
        w->bits >>= 8;
        w->nb -= 8;
    }
}
static void put_code(Bits *w, uint32_t code, int n) {      /* Huffman codes: MSB first */
    uint32_t r = 0;
    for (int i = 0; i < n; i++) r |= ((code >> i) & 1u) << (n - 1 - i);
    put_bits(w, r, n);
}
static void put_sym(Bits *w, int v) {                      /* fixed literal/length code */
    if (v < 144)      put_code(w, 0x30 + v, 8);
    else if (v < 256) put_code(w, 0x190 + v - 144, 9);
    else if (v < 280) put_code(w, v - 256, 7);
    else              put_code(w, 0xC0 + v - 280, 8);
}
static void put_match(Bits *w, int len, int dist) {
    static const short lbase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
    static const unsigned char lext[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
    static const unsigned short dbase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
    static const unsigned char dext[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
    int l = 28;
    while (lbase[l] > len) l--;
    put_sym(w, 257 + l);
    put_bits(w, (uint32_t)(len - lbase[l]), lext[l]);
    int d = 29;
    while (dbase[d] > dist) d--;
    put_code(w, (uint32_t)d, 5);
    put_bits(w, (uint32_t)(dist - dbase[d]), dext[d]);
}

/* zlib stream of `src` (one fixed-Huffman block). Caller frees *out. */
static int zlib_compress(const unsigned char *src, size_t n, unsigned char **out, size_t *out_n) {
    enum { WIN = 32768, HBITS = 15, MAXLEN = 258, CHAIN = 32 };
    int *head = (int *)malloc(sizeof(int) << HBITS), *prev = (int *)malloc(sizeof(int) * WIN);
    if (!head || !prev) { free(head); free(prev); return 0; }
    for (int i = 0; i < 1 << HBITS; i++) head[i] = -1;
    Bits w = { 0 };
    put_bits(&w, 0x78, 8); put_bits(&w, 0x01, 8);
    put_bits(&w, 1, 1); put_bits(&w, 1, 2);                 /* final block, fixed codes */
    size_t i = 0;
    while (i < n) {
        int best = 0, dist = 0;
        if (i + 3 <= n) {
            uint32_t hsh = ((src[i] << 10) ^ (src[i + 1] << 5) ^ src[i + 2]) & ((1u << HBITS) - 1);
            int cand = head[hsh], chain = CHAIN;
            size_t lim = n - i < MAXLEN ? n - i : MAXLEN;
            while (cand >= 0 && i - (size_t)cand <= WIN - 1 && chain--) {
                size_t k = 0;
                while (k < lim && src[cand + k] == src[i + k]) k++;
                if ((int)k > best) { best = (int)k; dist = (int)(i - cand); if (k == lim) break; }
                int p = prev[cand % WIN];
                if (p >= cand) break;
                cand = p;
            }
            prev[i % WIN] = head[hsh];
            head[hsh] = (int)i;
        }
        if (best >= 3) {
            put_match(&w, best, dist);
            for (size_t k = 1; k < (size_t)best; k++) {     /* index the skipped bytes */
                size_t j = i + k;
                if (j + 3 > n) break;
                uint32_t hsh = ((src[j] << 10) ^ (src[j + 1] << 5) ^ src[j + 2]) & ((1u << HBITS) - 1);
                prev[j % WIN] = head[hsh];
                head[hsh] = (int)j;
            }
            i += (size_t)best;
        } else {
            put_sym(&w, src[i++]);
        }
    }
    put_sym(&w, 256);                                        /* end of block */
    if (w.nb) put_bits(&w, 0, 8 - w.nb);
    uint32_t a = 1, b = 0;
    for (size_t k = 0; k < n; k++) { a = (a + src[k]) % 65521; b = (b + a) % 65521; }
    put_bits(&w, b >> 8, 8); put_bits(&w, b & 0xFF, 8);
    put_bits(&w, a >> 8, 8); put_bits(&w, a & 0xFF, 8);
    free(head); free(prev);
    *out = w.buf; *out_n = w.n;
    return w.buf != NULL;
}

int hdbuild_write_png(const char *path, int w, int h, const unsigned char *px) {
    size_t raw_n = (size_t)h * (1 + (size_t)w * 4);
    unsigned char *raw = (unsigned char *)malloc(raw_n), *z = NULL;
    size_t z_n = 0;
    if (!raw) return 0;
    for (int y = 0; y < h; y++) {
        raw[y * (1 + (size_t)w * 4)] = 0;
        memcpy(raw + y * (1 + (size_t)w * 4) + 1, px + (size_t)y * w * 4, (size_t)w * 4);
    }
    int ok = zlib_compress(raw, raw_n, &z, &z_n);
    free(raw);
    if (!ok) return 0;
    FILE *f = fopen(path, "wb");
    if (f) {
        static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
        unsigned char ihdr[13] = { (unsigned char)(w >> 24), (unsigned char)(w >> 16), (unsigned char)(w >> 8), (unsigned char)w,
                                   (unsigned char)(h >> 24), (unsigned char)(h >> 16), (unsigned char)(h >> 8), (unsigned char)h,
                                   8, 6, 0, 0, 0 };
        fwrite(sig, 1, 8, f);
        chunk(f, "IHDR", ihdr, 13);
        chunk(f, "IDAT", z, (uint32_t)z_n);
        chunk(f, "IEND", NULL, 0);
        fclose(f);
    }
    free(z);
    return f != NULL;
}
#define write_png_rgba hdbuild_write_png

#define ATLAS_COLS  32
#define MAX_SCALE   8

typedef struct { int w, h; unsigned char *px; } Img;     /* RGBA */

static void img_free(Img *im) { free(im->px); im->px = NULL; }

static int load_png(const char *path, Img *out) {
    int comp;
    out->px = stbi_load(path, &out->w, &out->h, &comp, 4);
    return out->px != NULL;
}

static Img img_new(int w, int h) {
    Img im = { w, h, (unsigned char *)calloc((size_t)w * h, 4) };
    return im;
}

static void img_flip(Img *im, char axis) {
    for (int y = 0; y < (axis == 'v' ? im->h / 2 : im->h); y++)
        for (int x = 0; x < (axis == 'h' ? im->w / 2 : im->w); x++) {
            int sx = axis == 'h' ? im->w - 1 - x : x, sy = axis == 'v' ? im->h - 1 - y : y;
            unsigned char *a = im->px + (y * im->w + x) * 4, *b = im->px + (sy * im->w + sx) * 4, t[4];
            memcpy(t, a, 4); memcpy(a, b, 4); memcpy(b, t, 4);
        }
}

/* Area-averaging resize (good for both shrinking large art and enlarging). */
static Img img_resize(const Img *src, int w, int h) {
    Img dst = img_new(w, h);
    double sx = (double)src->w / w, sy = (double)src->h / h;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            double x0 = x * sx, x1 = x0 + sx, y0 = y * sy, y1 = y0 + sy, acc[4] = { 0 }, wsum = 0;
            for (int yy = (int)y0; yy < (int)ceil(y1) && yy < src->h; yy++)
                for (int xx = (int)x0; xx < (int)ceil(x1) && xx < src->w; xx++) {
                    double wx = fmin(x1, xx + 1) - fmax(x0, xx), wy = fmin(y1, yy + 1) - fmax(y0, yy);
                    double wgt = wx * wy;
                    const unsigned char *p = src->px + (yy * src->w + xx) * 4;
                    double a = p[3] / 255.0;
                    acc[0] += p[0] * a * wgt; acc[1] += p[1] * a * wgt; acc[2] += p[2] * a * wgt;
                    acc[3] += p[3] * wgt; wsum += wgt;
                }
            unsigned char *d = dst.px + (y * w + x) * 4;
            if (wsum > 0 && acc[3] > 0) {
                double a = acc[3] / wsum;
                for (int k = 0; k < 3; k++) d[k] = (unsigned char)fmin(255, acc[k] / (a / 255.0) / wsum + 0.5);
                d[3] = (unsigned char)(a + 0.5);
            }
        }
    return dst;
}

/* ---- atlas + manifest ------------------------------------------------------ */
typedef struct {
    int      tile;          /* HD tile index (sprites +256) */
    uint32_t pal;           /* color-set key, 0xFFFFFFFF = any */
    int      flags;         /* sprite flip flags the entry is for, -1 = n/a */
    int      cond;          /* neighbor condition (index into s_conds), -1 = none */
    int      slot;          /* atlas cell */
} Entry;

/* "A sprite with tile `tile` (flip `flags`) sits (dx,dy) from this tile":
 * the runner's oamNearby condition, used to tell apart pictures that share
 * a tile (a ghost's top half over different feet). */
typedef struct { int dx, dy, tile, flags; } Cond;
static Cond s_conds[1024];
static int  s_nconds;

static int cond_id(int dx, int dy, int tile, int flags) {
    for (int i = 0; i < s_nconds; i++)
        if (s_conds[i].dx == dx && s_conds[i].dy == dy && s_conds[i].tile == tile && s_conds[i].flags == flags)
            return i;
    if (s_nconds == (int)(sizeof(s_conds) / sizeof(s_conds[0]))) return -2;
    s_conds[s_nconds] = (Cond){ dx, dy, tile, flags };
    return s_nconds++;
}

static Entry *s_entries;
static int    s_nentries, s_cap_entries;
static Img    s_atlas;
static int    s_scale;
static int    s_maze;           /* maze.png present: maze tiles are registered */

static int entry_slot(int tile, uint32_t pal, int flags, int cond) {
    for (int i = 0; i < s_nentries; i++)                /* later art wins */
        if (s_entries[i].tile == tile && s_entries[i].pal == pal && s_entries[i].flags == flags &&
            s_entries[i].cond == cond)
            return s_entries[i].slot;
    if (s_nentries == s_cap_entries) {
        s_cap_entries = s_cap_entries ? s_cap_entries * 2 : 512;
        s_entries = (Entry *)realloc(s_entries, s_cap_entries * sizeof(Entry));
    }
    int slot = s_nentries;
    s_entries[s_nentries++] = (Entry){ tile, pal, flags, cond, slot };
    int cell = 8 * s_scale, rows = (slot / ATLAS_COLS) + 1;
    if (rows * cell > s_atlas.h) {                      /* grow the atlas */
        Img bigger = img_new(ATLAS_COLS * cell, rows * cell * 2);
        if (s_atlas.px) memcpy(bigger.px, s_atlas.px, (size_t)s_atlas.w * s_atlas.h * 4);
        img_free(&s_atlas);
        s_atlas = bigger;
    }
    return slot;
}

/* Put an 8S x 8S tile image into the atlas for (tile, pal, flags). */
static void add_tile_c(int tile, uint32_t pal, int flags, int cond, const Img *t) {
    int slot = entry_slot(tile, pal, flags, cond), cell = 8 * s_scale;
    int ax = (slot % ATLAS_COLS) * cell, ay = (slot / ATLAS_COLS) * cell;
    for (int y = 0; y < cell; y++)
        memcpy(s_atlas.px + ((ay + y) * s_atlas.w + ax) * 4, t->px + y * cell * 4, (size_t)cell * 4);
}
static void add_tile(int tile, uint32_t pal, int flags, const Img *t) { add_tile_c(tile, pal, flags, -1, t); }

static Img crop(const Img *im, int x, int y, int w, int h) {
    Img c = img_new(w, h);
    for (int yy = 0; yy < h; yy++)
        memcpy(c.px + yy * w * 4, im->px + ((y + yy) * im->w + x) * 4, (size_t)w * 4);
    return c;
}

/* NES palette index -> RGB for tinting / CHR rendering. */
static void nes_rgb(int idx, unsigned char rgb[3]) {
    uint32_t v = g_nes_palette[idx & 0x3F];
    rgb[0] = (unsigned char)(v >> 16); rgb[1] = (unsigned char)(v >> 8); rgb[2] = (unsigned char)v;
}

/* White art -> the color set's main color (the one the tile's pixels use). */
static Img tint(const Img *t, int tile, int sprite, uint32_t pal) {
    const uint8_t *chr = g_chr_ram + (sprite ? 0x1000 : 0) + (tile & 0xFF) * 16;
    int used[4] = { 0 };
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            used[((chr[y] >> (7 - x)) & 1) | (((chr[y + 8] >> (7 - x)) & 1) << 1)]++;
    int v = used[3] >= used[1] && used[3] >= used[2] ? 3 : used[1] >= used[2] ? 1 : 2;
    unsigned char c[3];
    nes_rgb((int)(pal >> ((3 - v) * 8)) & 0x3F, c);
    Img o = img_new(t->w, t->h);
    for (int i = 0; i < t->w * t->h; i++)
        for (int k = 0; k < 4; k++)
            o.px[i * 4 + k] = k < 3 ? (unsigned char)(t->px[i * 4 + k] * c[k] / 255) : t->px[i * 4 + 3];
    return o;
}

/* The original CHR tile at scale S with color 0 transparent. */
static Img chr_tile(int tile, int sprite, uint32_t pal) {
    const uint8_t *chr = g_chr_ram + (sprite ? 0x1000 : 0) + (tile & 0xFF) * 16;
    Img o = img_new(8 * s_scale, 8 * s_scale);
    for (int y = 0; y < 8 * s_scale; y++)
        for (int x = 0; x < 8 * s_scale; x++) {
            int cx = x / s_scale, cy = y / s_scale;
            int v = ((chr[cy] >> (7 - cx)) & 1) | (((chr[cy + 8] >> (7 - cx)) & 1) << 1);
            if (!v) continue;
            unsigned char *d = o.px + (y * o.w + x) * 4;
            nes_rgb((int)(pal >> ((3 - v) * 8)) & 0x3F, d);
            d[3] = 255;
        }
    return o;
}

/* ---- building ---------------------------------------------------------------- */
static const HdGraphic *find_graphic(const char *name) {
    for (int i = 0; i < HD_GRAPHICS_N; i++)
        if (!strcmp(hd_graphics[i].name, name)) return &hd_graphics[i];
    return NULL;
}

/* The art for graphic g: its own file, else its mirror partner flipped. */
static int load_art(const char *dir, const HdGraphic *g, Img *out) {
    char path[1400];
    snprintf(path, sizeof(path), "%s/%s.png", dir, g->name);
    if (load_png(path, out)) return 1;
    if (!g->mirror_of) return 0;
    snprintf(path, sizeof(path), "%s/%s.png", dir, g->mirror_of);
    if (!load_png(path, out)) return 0;
    if (g->mirror_axis == 'h' || g->mirror_axis == 'v') img_flip(out, g->mirror_axis);
    return 1;
}

/* ---- shared tiles -------------------------------------------------------------- */
static int pals_overlap(const HdGraphic *a, const HdGraphic *b) {
    if (!a->npals || !b->npals) return !a->npals && !b->npals;
    for (int i = 0; i < a->npals; i++)
        for (int k = 0; k < b->npals; k++) if (a->pals[i] == b->pals[k]) return 1;
    return 0;
}

/* Does graphic h have a piece with this tile/flip at (x,y)? */
static int has_piece(const HdGraphic *h, int x, int y, int tile, int flags) {
    for (int k = 0; k < h->npieces; k++) {
        const HdPiece *q = &h->pieces[k];
        if (q->dx == x && q->dy == y && q->tile == tile && (q->flags & 3) == flags) return 1;
    }
    return 0;
}

/* Other uses of piece p's sprite tile (same flip, overlapping colors),
 * excluding piece p itself. */
static int same_tile(const HdGraphic *g, const HdPiece *pc, int hi, int k, int gi, int p) {
    const HdGraphic *h = &hd_graphics[hi];
    const HdPiece *hp = &h->pieces[k];
    return !(hi == gi && k == p) && h->sprite && hp->tile == pc->tile &&
           (hp->flags & 3) == (pc->flags & 3) && pals_overlap(g, h);
}

/* How piece p of graphic gi is registered when other pictures use the same
 * sprite tile: -1 = unconditionally (it is the only one, or the first),
 * >= 0 = only with that neighbor condition, -2 = not at all (nothing tells
 * it apart; the first picture's copy is used). Blank pieces (flag 4: the
 * empty tile that pads many frames, and more places than the layout knows)
 * are only ever registered with a condition. */
static int piece_cond(int gi, int p) {
    const HdGraphic *g = &hd_graphics[gi];
    const HdPiece *pc = &g->pieces[p];
    if (!g->sprite) return -1;
    int first = 1, shared = 0;
    for (int hi = 0; hi < HD_GRAPHICS_N; hi++)
        for (int k = 0; k < hd_graphics[hi].npieces; k++)
            if (same_tile(g, pc, hi, k, gi, p)) {
                shared = 1;
                if (hi < gi || (hi == gi && k < p)) first = 0;
            }
    int blank = pc->flags & 4;
    if (!shared && !blank) return -1;
    /* A neighbor piece that no other use of this tile has at the same offset. */
    for (int q = 0; q < g->npieces; q++) {
        if (q == p) continue;
        const HdPiece *nb = &g->pieces[q];
        int dx = nb->dx - pc->dx, dy = nb->dy - pc->dy, ok = 1;
        for (int hi = 0; hi < HD_GRAPHICS_N && ok; hi++)
            for (int k = 0; k < hd_graphics[hi].npieces && ok; k++)
                if (same_tile(g, pc, hi, k, gi, p)) {
                    const HdPiece *hp = &hd_graphics[hi].pieces[k];
                    if (has_piece(&hd_graphics[hi], hp->dx + dx, hp->dy + dy, nb->tile, nb->flags & 3)) ok = 0;
                }
        if (ok) {
            /* Offsets are screen space (pieces are laid out as drawn). */
            int c = cond_id(dx, dy, nb->tile, nb->flags & 3);
            if (c >= 0) return c;
        }
    }
    return first && !blank ? -1 : -2;
}

static void add_graphic_piece(const HdGraphic *g, const HdPiece *pc, int cond, const Img *t) {
    int tile = pc->tile + (g->sprite ? 256 : 0), fl = g->sprite ? (pc->flags & 3) : -1;
    for (int k = 0; k < g->npals; k++) {
        if (g->tint) { Img tt = tint(t, pc->tile, g->sprite, g->pals[k]); add_tile_c(tile, g->pals[k], fl, cond, &tt); img_free(&tt); }
        else add_tile_c(tile, g->pals[k], fl, cond, t);
    }
    if (g->wild || !g->npals) add_tile_c(tile, 0xFFFFFFFFu, fl, cond, t);
    if (s_maze && !g->sprite && !g->npals) {
        /* Beat the maze tiles' own exact-color entries (dots, pellets). */
        add_tile(tile, HD_MAZE_PAL_NORMAL, fl, t);
        add_tile(tile, HD_MAZE_PAL_FLASH, fl, t);
    }
}

static void add_graphic(const HdGraphic *g, const Img *art) {
    Img big = img_resize(art, g->w * s_scale, g->h * s_scale);
    int cell = 8 * s_scale;
    for (int p = 0; p < g->npieces; p++) {
        const HdPiece *pc = &g->pieces[p];
        int cond = piece_cond((int)(g - hd_graphics), p);
        if (cond == -2) continue;               /* the first picture's copy is used */
        Img t = crop(&big, pc->dx * s_scale, pc->dy * s_scale, cell, cell);
        /* The engine flips the HD tile the way the game flips the sprite, so
         * store it pre-flipped: what shows is then exactly this crop. */
        if (pc->flags & 1) img_flip(&t, 'h');
        if (pc->flags & 2) img_flip(&t, 'v');
        add_graphic_piece(g, pc, cond, &t);
        img_free(&t);
    }
    img_free(&big);
}

static int file_exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

/* A full-screen background PNG with the maze art placed where the maze is. */
static int write_maze_bg(const Img *art, const char *path, int whiten) {
    Img maze = img_resize(art, HD_MAZE_W * s_scale, HD_MAZE_H * s_scale);
    if (whiten)                                 /* synthesized level-clear flash */
        for (int i = 0; i < maze.w * maze.h; i++)
            for (int k = 0; k < 3; k++) maze.px[i * 4 + k] = (unsigned char)(maze.px[i * 4 + k] / 3 + 170);
    Img full = img_new(256 * s_scale, 240 * s_scale);
    for (int y = 0; y < maze.h; y++)
        memcpy(full.px + ((HD_MAZE_Y * s_scale + y) * full.w + HD_MAZE_X * s_scale) * 4,
               maze.px + y * maze.w * 4, (size_t)maze.w * 4);
    int ok = write_png_rgba(path, full.w, full.h, full.px);
    img_free(&maze); img_free(&full);
    return ok;
}

static int is_wall(int t) {
    for (unsigned i = 0; i < sizeof(hd_maze_walls); i++) if (hd_maze_walls[i] == t) return 1;
    return 0;
}

int hdbuild_make(const char *graphics_dir, char *out_dir, size_t out_n) {
    /* 1. Which art is there, and at what scale. */
    Img arts[HD_GRAPHICS_N];
    int have = 0, scale = 0;
    for (int i = 0; i < HD_GRAPHICS_N; i++) {
        arts[i].px = NULL;
        if (!load_art(graphics_dir, &hd_graphics[i], &arts[i])) continue;
        have++;
        int s = (int)lround((double)arts[i].w / hd_graphics[i].w);
        if (s > scale) scale = s;
    }
    char path[1400];
    Img maze = { 0 }, flash = { 0 };
    snprintf(path, sizeof(path), "%s/maze.png", graphics_dir);
    if (load_png(path, &maze)) {
        have++;
        int s = (int)lround((double)maze.w / HD_MAZE_W);
        if (s > scale) scale = s;
        snprintf(path, sizeof(path), "%s/maze_flash.png", graphics_dir);
        load_png(path, &flash);
    }
    if (!have) return 0;
    s_scale = scale < 1 ? HD_DEFAULT_SCALE : scale > MAX_SCALE ? MAX_SCALE : scale;

    /* 2. Tiles. */
    s_nentries = 0;
    s_nconds = 0;
    img_free(&s_atlas);
    s_atlas = img_new(ATLAS_COLS * 8 * s_scale, 8 * 8 * s_scale);
    s_maze = maze.px != NULL;
    if (maze.px) {
        /* Walls see-through; every other maze tile keeps its pixels on a
         * transparent background (dots, pellets, blanks) in both colors. */
        Img clear = img_new(8 * s_scale, 8 * s_scale);
        const uint32_t pals[2] = { HD_MAZE_PAL_NORMAL, HD_MAZE_PAL_FLASH };
        for (int t = 0; t < 0x40; t++)
            for (int k = 0; k < 2; k++) {
                if (is_wall(t)) { add_tile(t, pals[k], -1, &clear); continue; }
                Img c = chr_tile(t, 0, pals[k]);
                add_tile(t, pals[k], -1, &c);
                img_free(&c);
            }
        img_free(&clear);
    }
    for (int i = 0; i < HD_GRAPHICS_N; i++)
        if (arts[i].px) { add_graphic(&hd_graphics[i], &arts[i]); img_free(&arts[i]); }

    /* 3. Write the pack. */
    snprintf(out_dir, out_n, "%s/.hdcache", graphics_dir);
    MKDIR(out_dir);
    int cell = 8 * s_scale, rows = (s_nentries + ATLAS_COLS - 1) / ATLAS_COLS;
    snprintf(path, sizeof(path), "%s/atlas.png", out_dir);
    write_png_rgba(path, s_atlas.w, rows * cell, s_atlas.px);

    snprintf(path, sizeof(path), "%s/hires.txt", out_dir);
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "# Built by the game from %s - rebuilt on every mod apply.\n", graphics_dir);
    fprintf(f, "<ver>106\n<scale>%d\n<img>atlas.png\n", s_scale);
    if (maze.px) {
        char bg[1400];
        snprintf(bg, sizeof(bg), "%s/maze.png", out_dir);
        write_maze_bg(&maze, bg, 0);
        snprintf(bg, sizeof(bg), "%s/maze_flash.png", out_dir);
        write_maze_bg(flash.px ? &flash : &maze, bg, !flash.px);
        fprintf(f, "<condition>mazeOn,tileAtPosition,%d,%d,%X,%08X\n",
                HD_MAZE_X, HD_MAZE_Y, HD_MAZE_COND_TILE, HD_MAZE_PAL_NORMAL);
        fprintf(f, "<condition>mazeFlash,tileAtPosition,%d,%d,%X,%08X\n",
                HD_MAZE_X, HD_MAZE_Y, HD_MAZE_COND_TILE, HD_MAZE_PAL_FLASH);
        fprintf(f, "[mazeOn]<background>maze.png,1,0,0,5\n");
        fprintf(f, "[mazeFlash]<background>maze_flash.png,1,0,0,5\n");
    }
    static const char *const k_flip_cond[4] = {
        "[!hmirror&!vmirror]", "[hmirror&!vmirror]", "[!hmirror&vmirror]", "[hmirror&vmirror]"
    };
    for (int i = 0; i < s_nconds; i++)
        fprintf(f, "<condition>nb%d,oamNearby,%d,%d,%X,%d\n", i,
                s_conds[i].dx, s_conds[i].dy, s_conds[i].tile, s_conds[i].flags);
    for (int i = 0; i < s_nentries; i++) {
        const Entry *e = &s_entries[i];
        int any = e->pal == 0xFFFFFFFFu;
        char cond[64] = "";
        if (e->flags >= 0 && e->cond >= 0)      /* "[nbN&" + "!hmirror&!vmirror]" */
            snprintf(cond, sizeof(cond), "[nb%d&%s", e->cond, k_flip_cond[e->flags] + 1);
        else if (e->flags >= 0)
            snprintf(cond, sizeof(cond), "%s", k_flip_cond[e->flags]);
        fprintf(f, "%s<tile>0,%X,%08X,%d,%d,1,%s\n", cond,
                e->tile, e->pal, (e->slot % ATLAS_COLS) * cell, (e->slot / ATLAS_COLS) * cell,
                any ? "Y" : "N");
    }
    fclose(f);
    img_free(&maze); img_free(&flash);
    printf("[HDBuild] %s: %d tiles at %dx\n", graphics_dir, s_nentries, s_scale);
    return 1;
}
