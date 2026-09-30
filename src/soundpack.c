/*
 * src/soundpack.c — user sound effects / music (the active mod's sounds/<name>.wav).
 *
 * A WAV named after a sound in the active mod's sounds/ folder replaces
 * the game's own: the original keeps running silently (so the game's timing
 * never changes) and the file plays through the runner's mod mixer instead.
 * Missing files leave the original sound alone; no folder = stock game.
 *
 * The game's sound engine has 16 request slots ($0600 + i), nonzero while a
 * sound is wanted. One-shots start on the request's rising edge; looping
 * sounds (siren, blue ghosts, returning eyes) play while their request is
 * set. music.wav is an extra: a loop during play (the original has none).
 * Replacements follow the AUDIO menu's MUSIC / SOUND FX switches.
 *
 * WAV: PCM 8- or 16-bit, mono or stereo, any sample rate (converted to the
 * mixer's 44100 Hz mono on load).
 */
#include "soundpack.h"
#include "options.h"
#include "nes_runtime.h"
#include "mod_audio.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_SCRIPT      0x3F
#define RAM_FLAG_DEMO   0x48
#define RAM_FLAG_PAUSE  0x4A
#define RAM_SND_REQ     0x600
#define SCRIPT_PLAY     0x04
#define SCRIPT_FREEZE   0x06
#define LOOP_GRACE      4       /* frames a loop survives its request gap */

typedef struct {
    const char *file;       /* sounds/<file>.wav */
    int         slot;       /* game sound slot that triggers it; -1 = music */
    int         loop;       /* plays while the request is held */
    int         music;      /* follows MUSIC (else SOUND FX) */
    NESModAudioClip clip;
} Sound;

static Sound s_sounds[] = {
    { "start",        0,  0, 1 },
    { "extra_life",   2,  0, 0 },
    { "death",        3,  0, 0 },
    { "dot",          4,  0, 0 },
    { "dot",          5,  0, 0 },
    { "fruit",        6,  0, 0 },
    { "eat_ghost",    7,  0, 0 },
    { "eyes",         8,  1, 0 },
    { "fright",       9,  1, 0 },
    { "siren",        10, 1, 0 },
    { "siren",        11, 1, 0 },
    { "siren",        12, 1, 0 },
    { "intermission", 13, 0, 1 },
    { "pause",        15, 0, 0 },
    { "music",        -1, 1, 1 },
};
#define N_SOUNDS ((int)(sizeof(s_sounds) / sizeof(s_sounds[0])))

/* Slots 1 and 14 are the second voices of the jingle / intermission tunes. */
static int s_replaced[16];

/* ---- WAV loading ------------------------------------------------------- */
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static NESModAudioClip load_wav(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NES_MOD_AUDIO_CLIP_INVALID;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = size > 44 ? (uint8_t *)malloc((size_t)size) : NULL;
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); fclose(f); return 0; }
    fclose(f);

    NESModAudioClip clip = NES_MOD_AUDIO_CLIP_INVALID;
    int channels = 0, rate = 0, bits = 0;
    const uint8_t *data = NULL;
    uint32_t data_len = 0;
    if (memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4)) goto done;
    for (long pos = 12; pos + 8 <= size; ) {
        uint32_t len = rd32(buf + pos + 4);
        const uint8_t *body = buf + pos + 8;
        if (pos + 8 + (long)len > size) len = (uint32_t)(size - pos - 8);
        if (!memcmp(buf + pos, "fmt ", 4) && len >= 16) {
            if (rd16(body) != 1) goto done;             /* PCM only */
            channels = rd16(body + 2);
            rate = (int)rd32(body + 4);
            bits = rd16(body + 14);
        } else if (!memcmp(buf + pos, "data", 4)) {
            data = body;
            data_len = len;
        }
        pos += 8 + len + (len & 1);
    }
    if (!data || channels < 1 || channels > 2 || rate <= 0 || (bits != 8 && bits != 16)) goto done;

    uint32_t frames = data_len / (uint32_t)(channels * bits / 8);
    uint32_t out_n = (uint32_t)((double)frames * NES_MOD_AUDIO_SAMPLE_RATE / rate);
    if (!frames || !out_n) goto done;
    int16_t *out = (int16_t *)malloc(out_n * sizeof(int16_t));
    if (!out) goto done;
    for (uint32_t i = 0; i < out_n; i++) {
        double src = (double)i * rate / NES_MOD_AUDIO_SAMPLE_RATE;
        uint32_t a = (uint32_t)src, b = a + 1 < frames ? a + 1 : a;
        double t = src - a, v[2];
        for (int k = 0; k < 2; k++) {
            uint32_t fr = k ? b : a;
            int sum = 0;
            for (int c = 0; c < channels; c++) {
                const uint8_t *s = data + (fr * channels + c) * (bits / 8);
                sum += bits == 16 ? (int16_t)rd16(s) : ((int)s[0] - 128) << 8;
            }
            v[k] = (double)sum / channels;
        }
        out[i] = (int16_t)(v[0] + (v[1] - v[0]) * t);
    }
    clip = nes_mod_audio_register_pcm_s16_mono(out, out_n);
    free(out);
done:
    free(buf);
    return clip;
}

void soundpack_load(const char *sounds_dir) {
    char path[1200];
    /* Drop the previous set (a mod switch): stop and release every clip. */
    nes_mod_audio_stop_all();
    for (int i = 0; i < N_SOUNDS; i++) {
        NESModAudioClip c = s_sounds[i].clip;
        if (!c) continue;
        for (int j = i; j < N_SOUNDS; j++)
            if (s_sounds[j].clip == c) s_sounds[j].clip = NES_MOD_AUDIO_CLIP_INVALID;
        nes_mod_audio_unregister(c);
    }
    memset(s_replaced, 0, sizeof(s_replaced));
    if (!sounds_dir) return;

    for (int i = 0; i < N_SOUNDS; i++) {
        /* Shared files (dot, siren) load once and share the clip. */
        for (int j = 0; j < i; j++)
            if (!strcmp(s_sounds[j].file, s_sounds[i].file)) s_sounds[i].clip = s_sounds[j].clip;
        if (!s_sounds[i].clip) {
            snprintf(path, sizeof(path), "%s/%s.wav", sounds_dir, s_sounds[i].file);
            s_sounds[i].clip = load_wav(path);
            if (s_sounds[i].clip) printf("[Sounds] %s.wav loaded\n", s_sounds[i].file);
        }
        if (s_sounds[i].clip && s_sounds[i].slot >= 0) s_replaced[s_sounds[i].slot] = 1;
    }
    s_replaced[1]  = s_replaced[0];
    s_replaced[14] = s_replaced[13];
}

int soundpack_replaces(int slot) {
    return slot >= 0 && slot < 16 && s_replaced[slot];
}

static int allowed(const Sound *s) { return s->music ? g_opt.music : g_opt.sfx; }

void soundpack_frame(void) {
    static uint8_t prev_req[16];
    uint8_t demo = g_ram[RAM_FLAG_DEMO];
    int in_game = demo == 0x00;         /* the engine only runs in a game */
    int want[N_SOUNDS];
    for (int i = 0; i < N_SOUNDS; i++) {
        Sound *s = &s_sounds[i];
        int on;
        if (s->slot < 0)                /* music: during play, not paused */
            on = in_game && !(g_ram[RAM_FLAG_PAUSE] & 1) &&
                 (g_ram[RAM_SCRIPT] == SCRIPT_PLAY || g_ram[RAM_SCRIPT] == SCRIPT_FREEZE);
        else
            on = in_game && g_ram[RAM_SND_REQ + s->slot] != 0;
        want[i] = on && s->clip && allowed(s);
    }
    for (int i = 0; i < N_SOUNDS; i++) {
        Sound *s = &s_sounds[i];
        if (!s->clip) continue;
        if (s->loop) {
            /* Slots sharing a file (the siren's three speeds) share one loop:
             * decide it once, at the file's first entry. */
            int first = i, on = 0;
            while (first > 0 && s_sounds[first - 1].clip == s->clip) first--;
            if (first != i) continue;
            for (int j = i; j < N_SOUNDS && s_sounds[j].clip == s->clip; j++) on |= want[j];
            /* The engine restarts a looping effect with a one-frame gap;
             * ride over short gaps instead of restarting the file. */
            static int grace[N_SOUNDS];
            if (on) grace[i] = LOOP_GRACE;
            else if (grace[i] > 0) { grace[i]--; on = in_game; }
            if (on) nes_mod_audio_play_loop(s->clip, 100);
            else    nes_mod_audio_stop_loop(s->clip);
        } else if (want[i] && !prev_req[s->slot]) {
            nes_mod_audio_play(s->clip, 100);
        }
    }
    for (int i = 0; i < 16; i++) prev_req[i] = in_game ? g_ram[RAM_SND_REQ + i] : 0;
}
