/*
 * src/hdbuild.h - turn a mod's graphics/ folder of individual images into an
 * HD pack the runner can load.
 */
#pragma once
#include <stddef.h>

/* Build (or reuse) an HD pack for the images in `graphics_dir`. On success
 * writes the pack folder to out_dir and returns 1; 0 if there is nothing to
 * build. */
int hdbuild_make(const char *graphics_dir, char *out_dir, size_t out_n);

/* Write w x h RGBA pixels as a compressed PNG. Returns 1 on success. */
int hdbuild_write_png(const char *path, int w, int h, const unsigned char *px);
