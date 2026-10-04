/* game_dll_abi.h - what the game exe and game.dll hand each other.
 *
 * game.dll holds only the recompiled game code (generated/), compiled on the
 * player's PC from their own ROM. The exe holds everything else and has no
 * game code in it. At startup the exe passes the DLL pointers to everything
 * in GAME_DLL_IMPORTS and gets back the functions in GAME_DLL_EXPORTS.
 * Bump GAME_DLL_ABI_VERSION whenever either list changes. */
#pragma once

#define GAME_DLL_ABI_VERSION 1

/* Owned by the exe, used by the game code. */
#define GAME_DLL_IMPORTS(X) \
    X(g_cpu) X(g_ram) X(g_code_window_base) \
    X(g_rti_target) X(g_rti_source) X(g_rti_bank) X(g_rts_target) \
    X(g_recomp_push_all_jsr) \
    X(maybe_trigger_vblank) X(nes_brk_executed) \
    X(nes_cpu_instruction_boundary) X(nes_dispatch_call) \
    X(nes_interp_dispatch) X(nes_read) X(nes_read16_jmpbug) X(nes_read16zp) \
    X(nes_write) X(runtime_begin_post_nmi) X(runtime_end_post_nmi) \
    X(call_by_address_tail)

/* Defined by the game code, called by the exe. */
#define GAME_DLL_EXPORTS(X) \
    X(func_RESET) X(func_NMI) X(func_IRQ) X(call_by_address) \
    X(call_by_address_cb)

#define GAME_DLL_COUNT_ONE(n) +1
#define GAME_DLL_NUM_IMPORTS (0 GAME_DLL_IMPORTS(GAME_DLL_COUNT_ONE))
#define GAME_DLL_NUM_EXPORTS (0 GAME_DLL_EXPORTS(GAME_DLL_COUNT_ONE))
