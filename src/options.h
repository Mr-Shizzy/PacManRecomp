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
    int rumble;         /* controller rumble effects */
    int pac_speed;      /* Pac-Man: 0 normal, 1 1.25x, 2 1.5x */
    int show_level;     /* level number in the HUD */
    int highscores;     /* persistent top-10 leaderboard */
    int inf_lives;
    int start_level;    /* 1..MAX */
    int invincible;
    char mod[128];      /* active mod folder in mods/, "" = none */
} PacOptions;

extern PacOptions g_opt;

/* Load saved settings and install audio/effect hooks (game_on_init). */
void options_init(void);

/* Title-screen menu and options input; call first in game_on_frame(). */
void options_on_frame(void);

/* Per-frame effects that read the game's post-NMI state (sound mute);
 * call from game_post_nmi(). */
void options_post_nmi(void);

/* Draw the title menu / options screens; call from game_post_render(). */
void options_render(uint32_t *fb);

/* Save the Pac-Man settings now (e.g. after a mod switch). */
void options_save_now(void);

/* Re-read pacman_options.ini (the launcher, before the game starts). */
void options_reload(void);

/* Leave the current game for the title screen through the game's own
 * game-over path (keeps the high score). Call while in a game. */
void options_quit_to_title(void);

/* 1 while the title screen (scroll-in or menu) is on screen; *y_off gets
 * how far below its resting place the title currently sits. */
int options_title_y(int *y_off);

/* 1 while the title's OPTIONS screens are open (not the 1P/2P menu). */
int options_menu_open(void);

/* 1 if any cheat or speed option is on (the game can't go on the board). */
int options_cheats_active(void);

/* Apply whole-frame effects (inverse colors); call last in game_post_render(). */
void options_post_process(uint32_t *fb);

/* The OPTIONS settings as a launcher page (a RecompLauncherCHostPage). */
struct RecompLauncherCHostPage;
const struct RecompLauncherCHostPage *options_launcher_page(void);
