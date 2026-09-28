/*
 * src/mods.h — selectable mods in <exe>/mods/<name>/ (OPTIONS > MODS).
 */
#pragma once
#include <stdint.h>

/* Scan mods/ and apply the saved choice (game_on_init, after options_init). */
void mods_init(void);

/* Apply mod folder `folder` ("" = none: the original game plus any loose
 * legacy files next to the exe), live, and remember it. */
void mods_apply(const char *folder);

/* The MODS screen, driven by the options menu. */
void mods_menu_open(void);
void mods_menu_close(void);
/* Returns 1 when the player backs out of the screen. */
int  mods_menu_input(uint8_t pressed, int modern);
void mods_menu_render(uint32_t *fb);
