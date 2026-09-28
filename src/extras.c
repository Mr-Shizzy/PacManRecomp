/*
 * src/extras.c — Pac-Man (USA, Namco 1993) runner hooks
 *
 * Implements game_extras.h. The game runs stock (native recompiled RESET/NMI,
 * no overrides); additions are the title-screen OPTIONS menu (options.c) and
 * the pause-screen exit prompt (pause_menu.c).
 */
#include "game_extras.h"
#include "nes_runtime.h"
#include "pacman_full_decls.h"
#include "pause_menu.h"
#include "options.h"
#include "highscores.h"
#include "soundpack.h"
#include "logo.h"
#include "mods.h"
#include "capture.h"
#include <stdint.h>
#include <stddef.h>

/* Set by main_runner.c to the ROM path passed on the command line. */
const char *g_rom_path_for_extras = NULL;

uint32_t game_get_expected_crc32(void) { return 0x9E4E9CC2u; }

const char *game_get_name(void) { return "Pac-Man"; }

void game_on_init(void) {
    options_init();
    hs_init();
    mods_init();                /* sounds, logo, HD graphics */
    capture_init();             /* dev tool, off unless PACMAN_HD_CAPTURE */
    nesrecomp_set_escape_handler(pause_menu_escape);
}
void game_on_frame(uint64_t frame_count) {
    (void)frame_count;
    if (hs_on_frame()) return;      /* leaderboard screens own the frame */
    options_on_frame();
    pause_menu_on_frame();
}
void game_post_nmi(uint64_t frame_count) {
    (void)frame_count;
    options_post_nmi();
    hs_post_nmi();
    soundpack_frame();
}

int game_handle_arg(const char *key, const char *val) { (void)key; (void)val; return 0; }
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
