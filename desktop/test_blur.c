/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "blur.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message) {
	fprintf(stderr, "blur test failed: %s\n", message);
	return 1;
}

int main(void) {
	const int width = 16;
	const int height = 16;
	const int stride = width * 4;
	uint8_t *pixels = calloc((size_t)stride, (size_t)height);
	if (pixels == NULL) {
		return fail("alloc");
	}
	uint8_t *center = pixels + 8 * stride + 8 * 4;
	center[0] = 0;
	center[1] = 0;
	center[2] = 255;
	center[3] = 255;
	dev_blur_bgra(pixels, width, height, stride, 2);
	uint8_t *neighbor = pixels + 8 * stride + 7 * 4;
	if (center[2] == 255) {
		free(pixels);
		return fail("center pixel was not mixed");
	}
	if (neighbor[2] == 0) {
		free(pixels);
		return fail("neighbor did not receive color");
	}
	uint8_t *far = pixels + 0 * stride + 0 * 4;
	if (far[2] != 0) {
		free(pixels);
		return fail("blur reached the far corner at radius 2");
	}
	free(pixels);
	printf("blur test passed\n");
	return 0;
}
