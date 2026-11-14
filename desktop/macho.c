/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
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
	FAT_CIGAM = 0xBEBAFECA
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

	if (magic == FAT_MAGIC || magic == FAT_CIGAM) {
		/*
		 * A normal fat header is big-endian on disk. Reading those bytes as a
		 * little-endian word yields FAT_CIGAM. FAT_MAGIC here means the file
		 * itself was stored little-endian.
		 */
		uint32_t nfat;
		int stored_le = magic == FAT_MAGIC;
		if (stored_le) {
			nfat = read_u32(data + 4, 0);
		} else {
			nfat = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
				((uint32_t)data[6] << 8) | (uint32_t)data[7];
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
