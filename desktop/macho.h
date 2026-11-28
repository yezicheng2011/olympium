/*
 * Copyright 2026 Daniel Ye
 * Licensed under the Apache License, Version 2.0. See LICENSE.
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

/*
 * Lists the CPU types a Mach-O file holds: one for a thin file, one per
 * slice for a universal file. Returns how many were written, at most max,
 * or 0 when the bytes are not a Mach-O header. data should be the first
 * 4096 bytes of the file, or the whole file when it is shorter.
 */
int dev_macho_archs(const uint8_t *data, size_t length, uint32_t *cputypes, int max);

const char *dev_macho_cputype_name(uint32_t cputype);
const char *dev_macho_filetype_name(uint32_t filetype);

#endif
