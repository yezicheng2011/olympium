/*
 * Copyright 2026 Daniel Ye
 * Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Reports package identity. It does not load, sign, or execute a Mach-O.
 * iOS is not part of this system.
 */
#include "macho.h"

#include <stdio.h>
#include <string.h>

static void print_matrix(void) {
	puts("Host architecture is native. Instruction translation is a separate layer.");
	puts("");
	puts("layer                  macOS GUI app");
	puts("ISA translation        needed only when the CPU differs");
	puts("Mach-O / fat loader    required, not in this image");
	puts("dyld and libSystem     required, Apple's are not bundled");
	puts("Objective-C runtime    required, not bundled");
	puts("AppKit                 required, not bundled");
	puts("this image             opens a .dmg, .pkg, or .app and copies the application into Applications");
	puts("                       starts it only through Darling, when Darling is installed");
	puts("                       a bare Mach-O file is identified and not executed");
	puts("Olympium runtime       does not start macOS software on Linux");
	puts("lawful test app        a Linux window, not a Mac application");
	puts("");
	puts("iOS is not part of this system.");
	puts("A native Linux executable, including one built here, is not a Mac application.");
	puts("Pure software instruction translation does not implement Mach-O, dyld, AppKit, or UIKit.");
	puts("This tool does not bypass code signatures, DRM, or FairPlay, and it does not download applications.");
}

static int probe_file(const char *path) {
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		perror(path);
		return 1;
	}
	uint8_t buf[64];
	size_t n = fread(buf, 1, sizeof buf, f);
	fclose(f);
	struct dev_macho_info info;
	if (dev_macho_identify(buf, n, &info) == 0) {
		printf("%s: %s\n", path, info.description);
		puts("Not executed. This image has no Mach-O loader, dyld, libSystem, or AppKit.");
		puts("The Olympium runtime does not start macOS software on Linux.");
		puts("iOS is not part of this system.");
		return 3;
	}
	if (n >= 4 && buf[0] == 0x7F && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F') {
		printf("%s: Linux ELF. This is not a Mach-O, AppKit, or UIKit application.\n", path);
		return 0;
	}
	if (n >= 2 && buf[0] == 'P' && buf[1] == 'K') {
		printf("%s: zip container. Not launched.\n", path);
		return 3;
	}
	printf("%s: unrecognized. Not launched.\n", path);
	return 2;
}

int main(int argc, char **argv) {
	if (argc < 2 || strcmp(argv[1], "matrix") == 0) {
		print_matrix();
		return argc < 2 ? 0 : 0;
	}
	if (strcmp(argv[1], "probe") == 0 && argc == 3) {
		return probe_file(argv[2]);
	}
	if (strcmp(argv[1], "launch") == 0 && argc == 3) {
		int rc = probe_file(argv[2]);
		if (rc == 0) {
			puts("Refusing to treat a Linux ELF as a Mac application.");
			return 2;
		}
		return rc == 0 ? 2 : rc;
	}
	fprintf(stderr, "Usage: %s matrix | probe FILE | launch FILE\n", argv[0]);
	return 1;
}
