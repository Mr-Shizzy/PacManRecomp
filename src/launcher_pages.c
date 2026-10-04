/*
 * src/launcher_pages.c — Pac-Man's own launcher pages: the in-game OPTIONS
 * settings, the MODS screen (so both can be changed from either place), and
 * the update check on the main page.
 */
#include "game_extras.h"
#include "options.h"
#include "mods.h"
#include "controls_page.h"
#include "updater.h"
#include "recomp_launcher.h"

const struct RecompLauncherCHostPage *const *game_launcher_pages(int *count) {
    static const RecompLauncherCHostPage *pages[4];
    int n = 0;
    updater_startup_guard();     /* an update is running: don't open */
    pages[n++] = options_launcher_page();
    pages[n++] = controls_launcher_page();
    pages[n++] = mods_launcher_page();
    if (updater_launcher_page()) pages[n++] = updater_launcher_page();
    *count = n;
    return pages;
}
