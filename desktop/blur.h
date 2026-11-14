/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * Separable box blur for live compositor pixels. Bytes are B,G,R,A.
 */
#ifndef DEV_BLUR_H
#define DEV_BLUR_H

#include <stddef.h>
#include <stdint.h>

/* Blur an 8-bit BGRA buffer in place. stride is in bytes. radius 0 is a no-op. */
void dev_blur_bgra(uint8_t *pixels, int width, int height, int stride, int radius);

#endif
