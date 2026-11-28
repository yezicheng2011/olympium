/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 */
#include "macho.h"

#include <stdio.h>
#include <string.h>

enum {
	MH_MAGIC = 0xFEEDFACE,
	MH_CIGAM = 0xCEFAEDFE,
	MH_MAGIC_64 = 0xFEEDFACF,
	MH_CIGAM_64 = 0xCFFAEDFE,
	FAT_MAGIC = 0xCAFEBABE,
	FAT_CIGAM = 0xBEBAFECA,
	FAT_MAGIC_64 = 0xCAFEBABF,
	FAT_CIGAM_64 = 0xBFBAFECA
};

static uint32_t read_u32(const uint8_t *p, int swap) {
	uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
	if (swap) {
		v = __builtin_bswap32(v);
	}
	return v;
}

const char *dev_macho_cputype_name(uint32_t cputype) {
	switch (cputype) {
	case 7:
		return "i386";
	case 0x01000007:
		return "x86_64";
	case 12:
		return "arm";
	case 0x0100000C:
		return "arm64";
	case 18:
		return "powerpc";
	case 0x01000012:
		return "powerpc64";
	default:
		return "unknown-cputype";
	}
}

const char *dev_macho_filetype_name(uint32_t filetype) {
	switch (filetype) {
	case 1:
		return "object";
	case 2:
		return "execute";
	case 6:
		return "dylib";
	case 8:
		return "bundle";
	case 9:
		return "dylinker";
	default:
		return "other-filetype";
	}
}

int dev_macho_identify(const uint8_t *data, size_t length, struct dev_macho_info *out) {
	memset(out, 0, sizeof(*out));
	if (data == NULL || length < 8) {
		snprintf(out->description, sizeof out->description, "too short to be Mach-O");
		return -1;
	}
	uint32_t magic = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
		((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);

	if (magic == FAT_MAGIC || magic == FAT_CIGAM ||
			magic == FAT_MAGIC_64 || magic == FAT_CIGAM_64) {
		/*
		 * A normal fat header is big-endian on disk. Reading those bytes as a
		 * little-endian word yields FAT_CIGAM/FAT_CIGAM_64. FAT_MAGIC here
		 * means the file itself was stored little-endian. Java class files
		 * share 0xCAFEBABE followed by a version number, so reject impossible
		 * slice counts.
		 */
		uint32_t nfat;
		int stored_le = magic == FAT_MAGIC || magic == FAT_MAGIC_64;
		if (stored_le) {
			nfat = read_u32(data + 4, 0);
		} else {
			nfat = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
				((uint32_t)data[6] << 8) | (uint32_t)data[7];
		}
		if (nfat == 0 || nfat > 16) {
			snprintf(out->description, sizeof out->description, "not a Mach-O header");
			return -1;
		}
		out->kind = DEV_MACHO_FAT;
		out->swapped = stored_le;
		out->nfat = nfat;
		snprintf(out->description, sizeof out->description,
			"fat Mach-O header, %u architecture slot%s",
			nfat, nfat == 1 ? "" : "s");
		return 0;
	}

	int bits64 = 0;
	int swap = 0;
	if (magic == MH_MAGIC_64) {
		bits64 = 1;
	} else if (magic == MH_CIGAM_64) {
		bits64 = 1;
		swap = 1;
	} else if (magic == MH_MAGIC) {
		bits64 = 0;
	} else if (magic == MH_CIGAM) {
		bits64 = 0;
		swap = 1;
	} else {
		snprintf(out->description, sizeof out->description, "not a Mach-O header");
		return -1;
	}

	size_t need = bits64 ? 32 : 28;
	if (length < need) {
		snprintf(out->description, sizeof out->description, "truncated Mach-O header");
		return -1;
	}
	out->kind = bits64 ? DEV_MACHO_THIN_64 : DEV_MACHO_THIN_32;
	out->swapped = swap;
	out->cputype = read_u32(data + 4, swap);
	out->cpusubtype = read_u32(data + 8, swap);
	out->filetype = read_u32(data + 12, swap);
	snprintf(out->description, sizeof out->description,
		"%s Mach-O %s, filetype %s (%u)",
		dev_macho_cputype_name(out->cputype),
		bits64 ? "64-bit" : "32-bit",
		dev_macho_filetype_name(out->filetype),
		out->filetype);
	return 0;
}

static uint32_t read_be32(const uint8_t *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int dev_macho_archs(const uint8_t *data, size_t length, uint32_t *cputypes, int max) {
	if (data == NULL || length < 8 || max <= 0) {
		return 0;
	}
	uint32_t magic = read_be32(data);
	if (magic == 0xCAFEBABE || magic == 0xCAFEBABF) {
		/* A universal file: a count, then one entry per slice, all
		 * big-endian. An entry is 20 bytes, or 32 in the 64-bit variant,
		 * and starts with the CPU type. Java class files begin with the
		 * same four bytes followed by a version number, so a count this
		 * table could not hold means the file is not Mach-O. */
		uint32_t slices = read_be32(data + 4);
		if (slices == 0 || slices > 16) {
			return 0;
		}
		size_t entry = magic == 0xCAFEBABF ? 32 : 20;
		int count = 0;
		for (uint32_t i = 0; i < slices && count < max; i++) {
			size_t at = 8 + (size_t)i * entry;
			if (at + 4 > length) {
				break;
			}
			cputypes[count++] = read_be32(data + at);
		}
		return count;
	}
	struct dev_macho_info info;
	if (dev_macho_identify(data, length, &info) != 0 || info.kind == DEV_MACHO_FAT) {
		return 0;
	}
	cputypes[0] = info.cputype;
	return 1;
}
