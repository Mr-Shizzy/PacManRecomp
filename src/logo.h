/*
 * src/logo.h — replacement title logo from <exe>/logo.png.
 */
#pragma once
#include <stdint.h>

/* Load logo.png if present (game_on_init). */
void logo_init(void);

/* Blank the stock logo and place the replacement (game_post_render). */
void logo_render(uint32_t *fb);
