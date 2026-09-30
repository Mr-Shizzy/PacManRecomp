#pragma once
/* Record the game's own sounds as WAV files (src/sounddump.c). */

/* The hidden copy: "--dump-sounds <dir>" turns it into the recorder. */
int  sounddump_arg(const char *key, const char *val);
int  sounddump_active(void);
void sounddump_pre_nmi(void);
void sounddump_post_nmi(void);

/* The launcher: run the recorder into out_dir and wait. 1 on success. */
int  sounddump_run(const char *rom_path, const char *out_dir);
