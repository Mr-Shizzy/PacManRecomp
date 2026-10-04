/* game_dll.c - builds game.dll from the recompiled game code (generated/).
 *
 * Compiled on the player's PC by TinyCC (src/game_dll_host.c runs it):
 *   tcc -shared -DGAME_DLL_STAMP=<version> -I generated
 *       -I <nesrecomp>/runner/include game_dll.c
 * The stamp is the exe's version: an exe only uses a game.dll made for it.
 *
 * Every name in GAME_DLL_IMPORTS is renamed to a pointer filled in by
 * game_dll_init(), so `g_cpu.A` becomes `(*gd_g_cpu).A` and `nes_read(a)`
 * becomes `(*gd_nes_read)(a)`. The declarations in nes_runtime.h turn into
 * the matching pointer variables the same way. */
#include "game_dll_abi.h"

#ifndef GAME_DLL_STAMP
#error "build with -DGAME_DLL_STAMP=<the exe's version>"
#endif
#define GD_STR_(x) #x
#define GD_STR(x) GD_STR_(x)

/* As in the exe's build (nesrecomp/runner/runner.cmake). */
#define NESRECOMP_ENABLE_MODS 0
#define NESRECOMP_TRACE 0

#define g_cpu                        (*gd_g_cpu)
#define g_ram                        (*gd_g_ram)
#define g_code_window_base           (*gd_g_code_window_base)
#define g_rti_target                 (*gd_g_rti_target)
#define g_rti_source                 (*gd_g_rti_source)
#define g_rti_bank                   (*gd_g_rti_bank)
#define g_rts_target                 (*gd_g_rts_target)
#define g_recomp_push_all_jsr        (*gd_g_recomp_push_all_jsr)
#define maybe_trigger_vblank         (*gd_maybe_trigger_vblank)
#define nes_brk_executed             (*gd_nes_brk_executed)
#define nes_cpu_instruction_boundary (*gd_nes_cpu_instruction_boundary)
#define nes_dispatch_call            (*gd_nes_dispatch_call)
#define nes_interp_dispatch          (*gd_nes_interp_dispatch)
#define nes_read                     (*gd_nes_read)
#define nes_read16_jmpbug            (*gd_nes_read16_jmpbug)
#define nes_read16zp                 (*gd_nes_read16zp)
#define nes_write                    (*gd_nes_write)
#define runtime_begin_post_nmi       (*gd_runtime_begin_post_nmi)
#define runtime_end_post_nmi         (*gd_runtime_end_post_nmi)
#define call_by_address_tail         (*gd_call_by_address_tail)

#include "pacman_full.c"
#include "pacman_dispatch.c"

/* The data the runtime headers only declare (extern). */
CPU6502State *gd_g_cpu;
uint8_t (*gd_g_ram)[0x0800];
uint16_t *gd_g_code_window_base;

__declspec(dllexport) int game_dll_init(int abi, const char *stamp,
                                        void **imports, void **exports)
{
    const char *mine = GD_STR(GAME_DLL_STAMP);
    int i = 0;
    if (abi != GAME_DLL_ABI_VERSION) return 0;
    while (*mine && *mine == *stamp) { mine++; stamp++; }
    if (*mine || *stamp) return 0;
#define GD_IMPORT(n) *(void **)&gd_##n = imports[i++];
    GAME_DLL_IMPORTS(GD_IMPORT)
#undef GD_IMPORT
    i = 0;
#define GD_EXPORT(n) exports[i++] = (void *)&n;
    GAME_DLL_EXPORTS(GD_EXPORT)
#undef GD_EXPORT
    return GAME_DLL_ABI_VERSION;
}
