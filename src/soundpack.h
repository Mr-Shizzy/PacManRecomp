/*
 * src/soundpack.h — user sound effects / music from <exe>/sounds/*.wav.
 */
#pragma once

/* Load whatever WAV files are present (game_on_init). */
void soundpack_init(void);

/* 1 if the game's sound slot `slot` (0-15) has a replacement file, so its
 * original channel output should be muted. */
int soundpack_replaces(int slot);

/* Start/stop replacement sounds from the game's sound requests; call once
 * per frame after the NMI (game_post_nmi). */
void soundpack_frame(void);
