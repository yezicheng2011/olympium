/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye 源码许可证（第 1.0 版）。全文见 LICENSE。
 */
#include "macho.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message) {
	fprintf(stderr, "macho test failed: %s\n", message);
	return 1;
}

int main(void) {
	uint8_t thin[32] = {
		0xCF, 0xFA, 0xED, 0xFE,
		0x07, 0x00, 0x00, 0x01,
		0x00, 0x00, 0x00, 0x00,
		0x02, 0x00, 0x00, 0x00
	};
	struct dev_macho_info info;
	if (dev_macho_identify(thin, sizeof thin, &info) != 0) {
		return fail("thin header rejected");
	}
	if (info.kind != DEV_MACHO_THIN_64 || info.cputype != 0x01000007 || info.filetype != 2) {
		return fail("thin header fields");
	}
	uint8_t fat[8] = {0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x00, 0x00, 0x02};
	if (dev_macho_identify(fat, sizeof fat, &info) != 0 || info.kind != DEV_MACHO_FAT || info.nfat != 2) {
		return fail("fat header");
	}
	uint8_t elf[4] = {0x7F, 'E', 'L', 'F'};
	if (dev_macho_identify(elf, sizeof elf, &info) == 0) {
		return fail("ELF was accepted as Mach-O");
	}
	printf("macho test passed: %s\n", "format probe only");
	return 0;
}
