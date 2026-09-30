/*
 * src/sounddump.c — record the game's own sounds as WAV files (for modders).
 *
 * NES sounds aren't stored as audio: the game's engine drives the sound chip
 * live. So the launcher's "Dump textures" also starts a hidden, headless copy
 * of the game (sounddump_run) with --dump-sounds <dir>. That copy starts a
 * game, pauses it (so the game itself asks for no sounds), then requests each
 * sound from the engine one at a time ($0600 + slot, see soundpack.c) and
 * records the chip's output into <dir>/<name>.wav: the same names a mod's
 * replacements use.
 */
#include "sounddump.h"
#include "nes_runtime.h"
#include "apu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#  include <windows.h>
#endif

#define RAM_FLAG_DEMO   0x48
#define RAM_FLAG_PAUSE  0x4A
#define RAM_SND_REQ     0x600
#define RATE            44100
#define MAX_SECONDS     15
#define QUIET_SAMPLES   (RATE / 4)      /* 0.25 s of no change ends a one-shot */
#define SETTLE_FRAMES   30              /* silence between recordings */

typedef struct {
    const char *name;
    uint16_t    slots;      /* request slots set together (tunes use two voices) */
    int         loop_frames;/* > 0: a loop, held this long; else a one-shot */
    uint16_t    ram;        /* nonzero: also hold RAM[ram] = val while recording */
    uint8_t     val;
} Take;

static const Take k_takes[] = {
    { "start",        1 << 0 | 1 << 1,   0 },
    { "extra_life",   1 << 2,            0 },
    { "death",        1 << 3,            0 },
    { "dot",          1 << 4,            0 },   /* one dot (the game alternates 4 and 5) */
    { "fruit",        1 << 6,            0 },
    { "eat_ghost",    1 << 7,            0 },
    /* Blue ghosts: requested with the blue-time counter ($88), like the game.
     * Before "eyes", which leaves their shared channel taken for a while. */
    { "fright",       1 << 9,            180, 0x88, 0x0F },
    { "eyes",         1 << 8,            180 },
    { "siren",        1 << 10,           240 },
    { "intermission", 1 << 13 | 1 << 14, 0 },
    { "pause",        1 << 15,           0 },
};
#define N_TAKES ((int)(sizeof(k_takes) / sizeof(k_takes[0])))

static const char *s_dir;           /* NULL = not dumping */
static int16_t    *s_buf;
static int         s_len;           /* samples recorded in the current take */
static int         s_take = -1;     /* -1: getting the game paused first */
static int         s_step;          /* 0 settle, 1 recording */
static int         s_frames;        /* frames in the current step */
static uint16_t    s_hold;          /* slots forced on */
static uint16_t    s_free;          /* slots left to the engine (a playing one-shot) */

int sounddump_arg(const char *key, const char *val) {
    if (strcmp(key, "--dump-sounds") || !val) return 0;
    s_dir = val;
    return 1;
}

int sounddump_active(void) { return s_dir != NULL; }

static void put32(FILE *f, uint32_t v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void put16(FILE *f, uint16_t v) { fputc(v, f); fputc(v >> 8, f); }

static void write_wav(const char *name, const int16_t *s, int n) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s.wav", s_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + (uint32_t)n * 2); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 1); put32(f, RATE); put32(f, RATE * 2); put16(f, 2); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, (uint32_t)n * 2);
    for (int i = 0; i < n; i++) put16(f, (uint16_t)s[i]);
    fclose(f);
    printf("[SoundDump] %s.wav (%.2f s)\n", name, n / (double)RATE);
}

/* 1 when the last `n` samples hold one value (the chip idles at a flat level). */
static int quiet_tail(int n) {
    if (s_len < n) return 0;
    for (int i = s_len - n; i < s_len; i++) if (s_buf[i] != s_buf[s_len - 1]) return 0;
    return 1;
}

/* Trim the flat lead-in and tail, then save. */
static void save_take(const Take *t) {
    int a = 0, b = s_len;
    while (a < b && s_buf[a] == s_buf[0]) a++;
    while (b > a && s_buf[b - 1] == s_buf[s_len - 1]) b--;
    if (b > a) {
        int tail = RATE / 20;                       /* keep 50 ms of release */
        b = b + tail < s_len ? b + tail : s_len;
        write_wav(t->name, s_buf + a, b - a);
    } else {
        printf("[SoundDump] %s: no sound\n", t->name);
    }
}

static void next_take(void) {
    s_take++;
    s_step = 0;
    s_frames = 0;
    s_len = 0;
    s_hold = 0;
    s_free = 0;
}

/* Before the NMI (the sound engine reads its requests there). */
void sounddump_pre_nmi(void) {
    if (!s_dir) return;
    if (s_take < 0) {
        /* The script starts a game and pauses it; begin once it's paused. */
        if (g_ram[RAM_FLAG_DEMO] == 0x00 && (g_ram[RAM_FLAG_PAUSE] & 1) && ++s_frames > 60) {
            if (!s_buf) s_buf = (int16_t *)malloc(sizeof(int16_t) * RATE * MAX_SECONDS);
            if (!s_buf) exit(1);
            next_take();
        }
        return;
    }
    if (s_take >= N_TAKES) return;
    const Take *t = &k_takes[s_take];
    if (t->ram && s_step > 0 && s_hold) g_ram[t->ram] = t->val;
    for (int i = 0; i < 16; i++) {                 /* only our request is on */
        if ((s_free >> i) & 1) continue;
        /* The game requests the blue-ghost sound with its time left ($88). */
        g_ram[RAM_SND_REQ + i] = ((s_hold >> i) & 1) ? (t->ram && i == 9 ? t->val : 1) : 0;
    }
}

/* After the NMI: drive the takes and record what the chip produced. */
void sounddump_post_nmi(void) {
    if (!s_dir) return;
    apu_set_mute_mask(0);                           /* ignore MUSIC / SOUND FX off */
    int n = apu_output_available();
    static int16_t tmp[4096];
    while (n > 0) {
        int k = n < 4096 ? n : 4096;
        apu_generate(tmp, k);
        if (s_take >= 0 && s_take < N_TAKES && s_step > 0) {
            int room = RATE * MAX_SECONDS - s_len;
            int c = k < room ? k : room;
            memcpy(s_buf + s_len, tmp, sizeof(int16_t) * c);
            s_len += c;
        }
        n -= k;
    }
    if (s_take < 0) return;
    if (s_take >= N_TAKES) {
        printf("[SoundDump] done\n");
        fflush(stdout);
        exit(0);
    }

    const Take *t = &k_takes[s_take];
    s_frames++;
    switch (s_step) {
    case 0:                                         /* let the last one die out */
        s_hold = 0;
        if (s_frames >= SETTLE_FRAMES) { s_step = 1; s_frames = 0; s_len = 0; s_hold = t->slots; }
        break;
    case 1: {
        int full = s_len >= RATE * MAX_SECONDS;
        if (t->loop_frames > 0) {                   /* hold the request, then let go */
            s_hold = s_frames < t->loop_frames ? t->slots : 0;
            if ((s_frames >= t->loop_frames && quiet_tail(QUIET_SAMPLES)) || full) {
                save_take(t);
                next_take();
            }
        } else {
            /* One-shot: set the request, then leave it to the engine, which
             * clears it when the sound ends. Done once cleared and quiet. */
            s_hold = s_frames == 1 ? t->slots : 0;
            s_free = s_frames == 1 ? 0 : t->slots;
            int busy = 0;
            for (int i = 0; i < 16; i++)
                if (((t->slots >> i) & 1) && g_ram[RAM_SND_REQ + i]) busy = 1;
            if ((s_frames > 10 && !busy && quiet_tail(QUIET_SAMPLES)) || full) {
                save_take(t);
                next_take();
            }
        }
        break;
    }
    }
}

/* ---- the launcher side: run the hidden copy and wait ---------------------- */
int sounddump_run(const char *rom_path, const char *out_dir) {
#ifdef _WIN32
    char exe[MAX_PATH], script[MAX_PATH], cmd[4096];
    if (!rom_path || !rom_path[0] || !GetModuleFileNameA(NULL, exe, sizeof(exe))) return 0;
    CreateDirectoryA(out_dir, NULL);
    /* Start a game, pause it, then wait (the recorder exits when done).
     * Turbo: no pacing, so it takes a second or two, not half a minute. */
    GetTempPathA(sizeof(script), script);
    strncat(script, "pacman_sound_dump.txt", sizeof(script) - strlen(script) - 1);
    FILE *f = fopen(script, "w");
    if (!f) return 0;
    fputs("TURBO ON\nWAIT 420\nHOLD START\nWAIT 4\nRELEASE START\nWAIT 300\n"
          "HOLD START\nWAIT 4\nRELEASE START\nWAIT 20000\nEXIT 1\n", f);
    fclose(f);
    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" --script \"%s\" --dump-sounds \"%s\"",
             exe, rom_path, script, out_dir);
    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return 0;
    DWORD code = 1;
    if (WaitForSingleObject(pi.hProcess, 60000) == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 1);
    else GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DeleteFileA(script);
    return code == 0;
#else
    (void)rom_path; (void)out_dir;
    return 0;
#endif
}
