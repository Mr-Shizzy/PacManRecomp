/*
 * src/soundpack.h — user sound effects / music (the active mod's sounds/<name>.wav).
 */
#pragma once

/* Load the WAV files present in `sounds_dir` (no trailing slash), replacing
 * any previously loaded set; NULL = none (all original sounds). */
void soundpack_load(const char *sounds_dir);

/* 1 if the game's sound slot `slot` (0-15) has a replacement file, so its
 * original channel output should be muted. */
int soundpack_replaces(int slot);

/* Start/stop replacement sounds from the game's sound requests; call once
 * per frame after the NMI (game_post_nmi). */
void soundpack_frame(void);
