/*
 * src/options.h — in-game OPTIONS menu on Pac-Man's title screen.
 */
#pragma once
#include <stdint.h>

/* Pac-Man settings (runner display/volume settings live in g_nes_config). */
typedef struct {
    int inverse;        /* inverse colors */
    int music;          /* music on */
    int sfx;            /* sound effects on */
    int echo;           /* echo effect */
    int modern;         /* menu style: 0 classic (Select/Start), 1 modern */
    int pac_speed;      /* 1..3 */
    int ghost_speed;    /* 1..3 */
    int inf_lives;
    int start_level;    /* 1..MAX */
    int invincible;
} PacOptions;

extern PacOptions g_opt;

/* Load saved settings and install audio/effect hooks (game_on_init). */
void options_init(void);

/* Title-screen menu and options input; call first in game_on_frame(). */
void options_on_frame(void);

/* Draw the title menu / options screens; call from game_post_render(). */
void options_render(uint32_t *fb);

/* Apply whole-frame effects (inverse colors); call last in game_post_render(). */
void options_post_process(uint32_t *fb);
