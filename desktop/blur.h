/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Separable box blur for live compositor pixels. Bytes are B,G,R,A.
 */
#ifndef DEV_BLUR_H
#define DEV_BLUR_H

#include <stddef.h>
#include <stdint.h>

/* Blur an 8-bit BGRA buffer in place. stride is in bytes. radius 0 is a no-op. */
void dev_blur_bgra(uint8_t *pixels, int width, int height, int stride, int radius);

/* Refract toward the interior near the edges, then blur. Live pixels in, live pixels out. */
void dev_glass_bgra(uint8_t *pixels, int width, int height, int stride, int radius);

/* Heavy, cheap blur for a full-screen backdrop: shrink, blur, and stretch back. */
void dev_soft_bgra(uint8_t *pixels, int width, int height, int stride);

#endif
