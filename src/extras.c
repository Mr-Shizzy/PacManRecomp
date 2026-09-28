/*
 * src/extras.c — Pac-Man (USA, Namco 1993) runner hooks
 *
 * Implements game_extras.h. No game-specific behavior yet: every hook is the
 * stock default (native recompiled RESET/NMI, no overrides).
 */
#include "game_extras.h"
#include "nes_runtime.h"
#include "pacman_full_decls.h"
#include <stdint.h>
#include <stddef.h>

/* Set by main_runner.c to the ROM path passed on the command line. */
const char *g_rom_path_for_extras = NULL;

uint32_t game_get_expected_crc32(void) { return 0x9E4E9CC2u; }

const char *game_get_name(void) { return "Pac-Man"; }

void game_on_init(void) {}
void game_on_frame(uint64_t frame_count) { (void)frame_count; }
void game_post_nmi(uint64_t frame_count) { (void)frame_count; }

int game_handle_arg(const char *key, const char *val) { (void)key; (void)val; return 0; }
const char *game_arg_usage(void) { return NULL; }

void game_run_nmi(void) { func_NMI(); }
void game_run_main(void) { func_RESET(); }

int game_dispatch_override(uint16_t addr) { (void)addr; return 0; }

uint8_t game_ram_read_hook(uint16_t pc, uint16_t addr, uint8_t val) {
    (void)pc; (void)addr;
    return val;
}

void game_post_render(uint32_t *framebuf) { (void)framebuf; }
void game_fill_frame_record(void *record) { (void)record; }
int game_handle_debug_cmd(const char *cmd, int id, const char *json) {
    (void)cmd; (void)id; (void)json;
    return 0;
}
