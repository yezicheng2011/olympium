/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * Reads a Mac application bundle: its Info.plist, the CPU types of its
 * program, and its icon. Nothing here runs the program, and nothing here
 * comes from Apple. The property list and icon formats are read from their
 * published layouts.
 */
#ifndef DEV_MACBUNDLE_H
#define DEV_MACBUNDLE_H

#include <stddef.h>
#include <stdint.h>

enum mac_program_kind {
	MAC_PROGRAM_MISSING = 0,
	MAC_PROGRAM_MACHO = 1,
	MAC_PROGRAM_SCRIPT = 2,
	MAC_PROGRAM_OTHER = 3
};

struct mac_bundle {
	char name[128];        /* the name to show */
	char executable[160];  /* file name under Contents/MacOS */
	char identifier[160];
	char version[48];
	char min_system[32];
	char program[1024];    /* full path of the program */
	char icon_path[1024];  /* full path of the .icns, or empty */
	uint32_t archs[8];
	int arch_count;
	enum mac_program_kind program_kind;
};

/* 1 when path is a directory that holds Contents/Info.plist. */
int mac_is_bundle_dir(const char *path);

/*
 * Looks up one string value in a property list, XML or binary. Returns 0 and
 * fills out when the key is there and its value is a string.
 */
int mac_plist_string(const unsigned char *data, size_t length, const char *key,
	char *out, size_t n);

/* Reads the bundle at bundle_dir. Returns 0 when it has a readable Info.plist. */
int mac_bundle_read(const char *bundle_dir, struct mac_bundle *out);

/*
 * Finds the best PNG picture inside .icns bytes. Returns 0 and sets the
 * offset and length of the PNG when there is one.
 */
int mac_icns_png(const unsigned char *data, size_t length, size_t *offset, size_t *png_length);

/*
 * Writes an icon a launcher can show. A PNG taken out of the .icns goes to
 * png_out. An .icns with no PNG inside is copied to icns_out as it is.
 * Returns 1 for the PNG, 2 for the copy, 0 when there is no usable icon.
 */
int mac_icon_export(const char *icns_path, const char *png_out, const char *icns_out);

/* "x86_64, arm64" */
void mac_archs_text(const uint32_t *archs, int count, char *out, size_t n);

/* 1 when one of the CPU types is this computer's. */
int mac_host_can_run(const uint32_t *archs, int count);

/* The name of this computer's CPU as Mach-O files call it. */
const char *mac_host_arch_name(void);

#endif
