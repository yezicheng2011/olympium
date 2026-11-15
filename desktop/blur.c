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

static int glass_sample(int v, int limit) {
	if (v < 0) {
		return 0;
	}
	if (v >= limit) {
		return limit - 1;
	}
	return v;
}

void dev_glass_bgra(uint8_t *pixels, int width, int height, int stride, int radius) {
	if (pixels == NULL || width <= 1 || height <= 1) {
		return;
	}
	size_t bytes = (size_t)stride * (size_t)height;
	uint8_t *src = malloc(bytes);
	if (src == NULL) {
		dev_blur_bgra(pixels, width, height, stride, radius);
		return;
	}
	memcpy(src, pixels, bytes);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int edge_x = x < width - 1 - x ? x : width - 1 - x;
			int edge_y = y < height - 1 - y ? y : height - 1 - y;
			int edge = edge_x < edge_y ? edge_x : edge_y;
			int reach = width < height ? width : height;
			if (reach > 48) {
				reach = 48;
			}
			int bend = reach - edge;
			if (bend < 0) {
				bend = 0;
			}
			int dx = 0;
			int dy = 0;
			if (bend > 0) {
				dx = (x < width / 2 ? bend / 4 : -(bend / 4));
				dy = (y < height / 2 ? bend / 5 : -(bend / 5));
			}
			int sx = glass_sample(x + dx, width);
			int sy = glass_sample(y + dy, height);
			const uint8_t *p = src + sy * stride + sx * 4;
			uint8_t *o = pixels + y * stride + x * 4;
			o[0] = p[0];
			o[1] = p[1];
			o[2] = p[2];
			o[3] = p[3];
		}
	}
	free(src);
	if (radius > 0) {
		dev_blur_bgra(pixels, width, height, stride, radius);
	}
}

void dev_soft_bgra(uint8_t *pixels, int width, int height, int stride) {
	enum { STEP = 8 };
	if (pixels == NULL || width < STEP * 2 || height < STEP * 2) {
		dev_blur_bgra(pixels, width, height, stride, 4);
		return;
	}
	int sw = (width + STEP - 1) / STEP;
	int sh = (height + STEP - 1) / STEP;
	int sstride = sw * 4;
	uint8_t *small = malloc((size_t)sstride * (size_t)sh);
	if (small == NULL) {
		return;
	}
	for (int sy = 0; sy < sh; sy++) {
		for (int sx = 0; sx < sw; sx++) {
			unsigned sum[4] = {0, 0, 0, 0};
			unsigned count = 0;
			for (int y = sy * STEP; y < (sy + 1) * STEP && y < height; y++) {
				const uint8_t *row = pixels + y * stride;
				for (int x = sx * STEP; x < (sx + 1) * STEP && x < width; x++) {
					sum[0] += row[x * 4 + 0];
					sum[1] += row[x * 4 + 1];
					sum[2] += row[x * 4 + 2];
					sum[3] += row[x * 4 + 3];
					count++;
				}
			}
			uint8_t *o = small + sy * sstride + sx * 4;
			for (int c = 0; c < 4; c++) {
				o[c] = (uint8_t)(sum[c] / count);
			}
		}
	}
	dev_blur_bgra(small, sw, sh, sstride, 3);
	dev_blur_bgra(small, sw, sh, sstride, 2);
	for (int y = 0; y < height; y++) {
		int fy = (y * 256) / STEP - 128;
		if (fy < 0) {
			fy = 0;
		}
		int y0 = fy >> 8;
		int wy = fy & 255;
		int y1 = y0 + 1 < sh ? y0 + 1 : sh - 1;
		if (y0 >= sh) {
			y0 = sh - 1;
		}
		for (int x = 0; x < width; x++) {
			int fx = (x * 256) / STEP - 128;
			if (fx < 0) {
				fx = 0;
			}
			int x0 = fx >> 8;
			int wx = fx & 255;
			int x1 = x0 + 1 < sw ? x0 + 1 : sw - 1;
			if (x0 >= sw) {
				x0 = sw - 1;
			}
			const uint8_t *p00 = small + y0 * sstride + x0 * 4;
			const uint8_t *p01 = small + y0 * sstride + x1 * 4;
			const uint8_t *p10 = small + y1 * sstride + x0 * 4;
			const uint8_t *p11 = small + y1 * sstride + x1 * 4;
			uint8_t *o = pixels + y * stride + x * 4;
			for (int c = 0; c < 4; c++) {
				int top = p00[c] * (256 - wx) + p01[c] * wx;
				int bottom = p10[c] * (256 - wx) + p11[c] * wx;
				o[c] = (uint8_t)((top * (256 - wy) + bottom * wy) >> 16);
			}
		}
	}
	free(small);
}
