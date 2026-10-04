/*
 * src/extras.c — Pac-Man (USA, Namco 1993) runner hooks
 *
 * Implements game_extras.h. The game runs stock (native recompiled RESET/NMI,
 * no overrides); additions are the title-screen OPTIONS menu (options.c) and
 * the pause-screen exit prompt (pause_menu.c).
 */
#include "hdpack.h"
#include "game_extras.h"
#include "nes_runtime.h"
#include "nes_runtime.h"
#include "pause_menu.h"
#include "options.h"
#include "highscores.h"
#include "soundpack.h"
#include "logo.h"
#include "mods.h"
#include "sounddump.h"
#include "capture.h"
#include "updater.h"
#include <stdint.h>
#include <stddef.h>

/* Set by main_runner.c to the ROM path passed on the command line. */
const char *g_rom_path_for_extras = NULL;
#ifdef PACMAN_GAME_DLL
void game_dll_prepare(const char *rom);
#endif

uint32_t game_get_expected_crc32(void) { return 0x9E4E9CC2u; }

const char *game_get_name(void) { return "Pac-Man"; }

void game_on_init(void) {
    updater_startup_guard();     /* also covers "Skip launcher on boot" */
#ifdef PACMAN_GAME_DLL
    game_dll_prepare(g_rom_path_for_extras);   /* makes game.dll if needed */
#endif
    options_init();
    hs_init();
    if (!sounddump_active()) mods_init();   /* sounds, logo, HD graphics */
    capture_init();             /* dev tool, off unless PACMAN_HD_CAPTURE */
    nesrecomp_set_escape_handler(pause_menu_escape);
}
void game_on_frame(uint64_t frame_count) {
    (void)frame_count;
    if (hs_on_frame()) return;      /* leaderboard screens own the frame */
    options_on_frame();
    pause_menu_on_frame();
    sounddump_pre_nmi();            /* the hidden sound recorder, if running */
}
/* Which way Pac-Man is drawn facing, for the optional closed_<dir> pictures
 * (HD pack memory check at $5F00: 0 up, 1 left, 2 down, 3 right). Taken
 * from his open-mouth frames in OAM, so it is right in the cutscenes too,
 * where the game doesn't update his heading byte $51. Only Pac-Man uses
 * tiles 01-08. The closed frame looks the same every way, so it keeps the
 * last open frame's direction. */
static void track_pac_facing(void) {
    static uint8_t facing = 1, scene = 0xFF;
    if (g_ram[0x3F] != scene) {     /* he enters every cutscene ($3F = 10) going left */
        scene = g_ram[0x3F];
        if (scene == 0x10) facing = 1;
    }
    for (int i = 0; i < 64; i++) {
        const uint8_t *e = &g_ppu_oam[i * 4];
        if (e[0] >= 0xEF) continue;     /* any palette: the cutscenes use another */
        switch (e[1]) {
            case 0x01: case 0x02: case 0x05: case 0x06:     /* side frames */
                facing = (e[2] & 0x40) ? 3 : 1; break;
            case 0x03: case 0x04: case 0x07: case 0x08:     /* up/down frames */
                facing = (e[2] & 0x80) ? 0 : 2; break;
            default: continue;
        }
        break;
    }
    hdpack_set_var(0, facing);
}

void game_post_nmi(uint64_t frame_count) {
    (void)frame_count;
    track_pac_facing();
    options_post_nmi();
    hs_post_nmi();
    if (sounddump_active()) sounddump_post_nmi();
    else soundpack_frame();
}

int game_handle_arg(const char *key, const char *val) { return sounddump_arg(key, val); }
const char *game_arg_usage(void) { return NULL; }

void game_run_nmi(void) { func_NMI(); }
void game_run_main(void) { func_RESET(); }

int game_dispatch_override(uint16_t addr) { (void)addr; return 0; }

uint8_t game_ram_read_hook(uint16_t pc, uint16_t addr, uint8_t val) {
    (void)pc; (void)addr;
    return val;
}

void game_post_render(uint32_t *framebuf) {
    capture_frame();
    logo_render(framebuf);
    options_render(framebuf);
    pause_menu_render(framebuf);
    hs_render(framebuf);
    options_post_process(framebuf);
}
void game_fill_frame_record(void *record) { (void)record; }
int game_handle_debug_cmd(const char *cmd, int id, const char *json) {
    (void)cmd; (void)id; (void)json;
    return 0;
}
