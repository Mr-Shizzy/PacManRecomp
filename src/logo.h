/*
 * src/logo.h — replacement title logo (the active mod's logo.png).
 */
#pragma once
#include <stdint.h>

/* Use the PNG at `path` as the title logo (replacing any previous one);
 * NULL or a missing file = the stock logo. */
void logo_load(const char *path);

/* Blank the stock logo and place the replacement (game_post_render). */
void logo_render(uint32_t *fb);
