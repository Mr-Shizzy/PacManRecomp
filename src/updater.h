#ifndef PACMAN_UPDATER_H
#define PACMAN_UPDATER_H
/* Optional update check on the launcher's main page (off by default). */
struct RecompLauncherCHostPage;
const struct RecompLauncherCHostPage *updater_launcher_page(void);

/* At startup (before the launcher and in game_on_init): if an update is
 * running, say so and exit; else delete a cut-off update's leftovers. */
void updater_startup_guard(void);
#endif
