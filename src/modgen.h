/*
 * src/modgen.h — make a ready-to-paint starter mod (the in-game and launcher
 * version of tools/make_mod_template.py).
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define MODGEN_SCALE 4          /* starter pictures are 4x the original */

/* Write every starter picture of mod folder `mod_dir` that is missing (never
 * overwrites a modder's file), plus mod.txt / preview.png / sounds/README.txt
 * when missing. `chr` is the game's 8 KB of tile graphics. Returns the number
 * of pictures written, or -1 if the folder could not be created. */
int modgen_write(const uint8_t chr[0x2000], const char *mod_dir);

/* The 8 KB of tile graphics from an iNES ROM file. Returns 1 on success. */
int modgen_chr_from_rom(const char *rom_path, uint8_t chr[0x2000]);

/* The first free "My Mod N" folder name in `mods_dir`. */
void modgen_new_name(const char *mods_dir, char *out, size_t n);

/* Open a folder in the system file browser. */
void modgen_open_folder(const char *path);
