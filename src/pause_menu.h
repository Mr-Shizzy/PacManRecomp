/*
 * src/pause_menu.h — "exit to launcher" prompt on Pac-Man's pause screen.
 */
#pragma once
#include <stdint.h>

/* Call from game_on_frame(): reads/filters controller 1 before the NMI. */
void pause_menu_on_frame(void);

/* Escape-key handler for nesrecomp_set_escape_handler(): pauses and opens
 * the prompt in a game, toggles it while paused, and returns to the launcher
 * from the title screen / attract demo. Always consumes the key. */
int pause_menu_escape(void);

/* Call from game_post_render(): draws the prompt over the native frame. */
void pause_menu_render(uint32_t *fb);
