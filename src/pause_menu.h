/*
 * src/pause_menu.h — "exit to launcher" prompt on Pac-Man's pause screen.
 */
#pragma once
#include <stdint.h>

/* Call from game_on_frame(): reads/filters controller 1 before the NMI. */
void pause_menu_on_frame(void);

/* Call from game_post_render(): draws the prompt over the native frame. */
void pause_menu_render(uint32_t *fb);
