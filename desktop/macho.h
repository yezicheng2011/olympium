/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * Identifies Mach-O and fat headers. This is not a loader and does not
 * execute code.
 */
#ifndef DEV_MACHO_H
#define DEV_MACHO_H

#include <stddef.h>
#include <stdint.h>

enum dev_macho_kind {
	DEV_MACHO_NOT = 0,
	DEV_MACHO_THIN_32,
	DEV_MACHO_THIN_64,
	DEV_MACHO_FAT
};

struct dev_macho_info {
	enum dev_macho_kind kind;
	int swapped;
	uint32_t cputype;
	uint32_t cpusubtype;
	uint32_t filetype;
	uint32_t nfat;
	char description[160];
};

/* Returns 0 when the buffer starts with a Mach-O or fat header. */
int dev_macho_identify(const uint8_t *data, size_t length, struct dev_macho_info *out);

const char *dev_macho_cputype_name(uint32_t cputype);
const char *dev_macho_filetype_name(uint32_t filetype);

#endif
