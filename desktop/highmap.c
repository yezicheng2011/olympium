/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Maps one page at 4 GiB. A 32-bit process cannot hold that address.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

int main(void) {
	void *page = mmap((void *)0x100000000ULL, 4096, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	printf("highmap pointer-bits=%d page=%p\n", (int)(sizeof(void *) * 8), page);
	if (page == MAP_FAILED) {
		perror("highmap");
		return 1;
	}
	memset(page, 0x5a, 4096);
	printf("highmap byte=0x%02x\n", ((unsigned char *)page)[0]);
	return 0;
}
