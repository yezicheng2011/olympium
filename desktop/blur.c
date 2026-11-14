/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 */
#include "blur.h"

#include <stdlib.h>
#include <string.h>

static void blur_axis(const uint8_t *src, uint8_t *dst, int width, int height,
		int stride, int radius, int horizontal) {
	const int window = radius * 2 + 1;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int sum_b = 0, sum_g = 0, sum_r = 0, sum_a = 0;
			for (int k = -radius; k <= radius; k++) {
				int sx = x;
				int sy = y;
				if (horizontal) {
					sx = x + k;
					if (sx < 0) {
						sx = 0;
					}
					if (sx >= width) {
						sx = width - 1;
					}
				} else {
					sy = y + k;
					if (sy < 0) {
						sy = 0;
					}
					if (sy >= height) {
						sy = height - 1;
					}
				}
				const uint8_t *p = src + sy * stride + sx * 4;
				sum_b += p[0];
				sum_g += p[1];
				sum_r += p[2];
				sum_a += p[3];
			}
			uint8_t *o = dst + y * stride + x * 4;
			o[0] = (uint8_t)(sum_b / window);
			o[1] = (uint8_t)(sum_g / window);
			o[2] = (uint8_t)(sum_r / window);
			o[3] = (uint8_t)(sum_a / window);
		}
	}
}

void dev_blur_bgra(uint8_t *pixels, int width, int height, int stride, int radius) {
	if (pixels == NULL || width <= 0 || height <= 0 || radius <= 0) {
		return;
	}
	size_t bytes = (size_t)stride * (size_t)height;
	uint8_t *tmp = malloc(bytes);
	uint8_t *tmp2 = malloc(bytes);
	if (tmp == NULL || tmp2 == NULL) {
		free(tmp);
		free(tmp2);
		return;
	}
	blur_axis(pixels, tmp, width, height, stride, radius, 1);
	blur_axis(tmp, tmp2, width, height, stride, radius, 0);
	memcpy(pixels, tmp2, bytes);
	free(tmp);
	free(tmp2);
}
