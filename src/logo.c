/*
 * src/logo.c — replacement title logo from <exe>/logo.png.
 *
 * With a logo.png next to the exe, the title's PAC-MAN logo (tile rows 7-12)
 * is blanked and the image is shown there instead, fitted inside the logo
 * band with its proportions kept and centered. It is drawn as a runner
 * overlay at the image's own resolution, so a large PNG stays sharp. It rides
 * the title's scroll-in and shows only on the title and options screens.
 * No file = the stock logo.
 */
#include "logo.h"
#include "options.h"
#include "nes_text.h"
#include "nes_runtime.h"
#include "config.h"
#include <stdio.h>

#define LOGO_ROW0   7           /* logo box + TM: tile rows 7-12 */
#define LOGO_ROW1   12
#define LOGO_X      24          /* the band the image is fitted into (px) */
#define LOGO_W      208
#define LOGO_Y      (LOGO_ROW0 * 8)
#define LOGO_H      ((LOGO_ROW1 - LOGO_ROW0 + 1) * 8)

static int s_logo;
static int s_w, s_h;

void logo_init(void) {
    char dir[1024], path[1100];
    nesrecomp_exe_dir(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%slogo.png", dir);
    s_logo = nesrecomp_overlay_load_png(path);
    if (s_logo && !nesrecomp_overlay_size(s_logo, &s_w, &s_h)) s_logo = 0;
    if (s_logo) printf("[Logo] logo.png loaded (%dx%d)\n", s_w, s_h);
}

void logo_render(uint32_t *fb) {
    if (!s_logo) return;
    int y_off;
    if (!options_title_y(&y_off)) {
        nesrecomp_overlay_place(s_logo, 0, 0, 0, 0, 0);
        return;
    }
    text_clear_rows(fb, LOGO_ROW0, LOGO_ROW1, y_off);
    float scale = (float)LOGO_W / s_w;
    if ((float)LOGO_H / s_h < scale) scale = (float)LOGO_H / s_h;
    float w = s_w * scale, h = s_h * scale;
    nesrecomp_overlay_place(s_logo, 1, LOGO_X + (LOGO_W - w) / 2,
                            LOGO_Y + y_off + (LOGO_H - h) / 2, w, h);
}
