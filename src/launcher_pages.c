/*
 * src/launcher_pages.c — Pac-Man's own launcher pages: the in-game OPTIONS
 * settings and the MODS screen, so both can be changed from either place.
 */
#include "game_extras.h"
#include "options.h"
#include "mods.h"
#include "controls_page.h"
#include "recomp_launcher.h"

const struct RecompLauncherCHostPage *const *game_launcher_pages(int *count) {
    static const RecompLauncherCHostPage *pages[3];
    pages[0] = options_launcher_page();
    pages[1] = controls_launcher_page();
    pages[2] = mods_launcher_page();
    *count = 3;
    return pages;
}
