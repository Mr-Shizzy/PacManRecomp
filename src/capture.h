/*
 * src/capture.h - development tool: record which tiles make up each graphic
 * (enabled with PACMAN_HD_CAPTURE=<file>; see capture.c).
 */
#pragma once

void capture_init(void);
void capture_frame(void);
