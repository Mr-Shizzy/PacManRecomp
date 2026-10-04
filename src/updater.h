#ifndef PACMAN_UPDATER_H
#define PACMAN_UPDATER_H
/* Optional update check on the launcher's main page (off by default). */
struct RecompLauncherCHostPage;
const struct RecompLauncherCHostPage *updater_launcher_page(void);
#endif
