/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye 源码许可证（第 1.0 版）。全文见 LICENSE。
 *
 * Reports package identity and the layers still required before a macOS or
 * iOS GUI application could run. It does not load, sign, or execute those
 * applications.
 */
#include "macho.h"

#include <stdio.h>
#include <string.h>

static void print_matrix(void) {
	puts("Host architecture is native. Instruction translation is a separate layer.");
	puts("");
	puts("layer                  macOS GUI app                         iOS GUI app");
	puts("ISA translation        needed only when the CPU differs      needed only when the CPU differs");
	puts("Mach-O / fat loader    required, not a GUI                    required, not a GUI");
	puts("dyld and libSystem     required, Apple's are not bundled      required, Apple's are not bundled");
	puts("Objective-C runtime    required                               required");
	puts("AppKit / UIKit         AppKit required                        UIKit required");
	puts("this image             format probe only                      format probe only, IPA refused");
	puts("Olympium 25a592c       refused on Linux by that revision      refused, including IPA on a Mac");
	puts("");
	puts("A native Linux executable, including one built here, is not evidence that an original macOS or iOS GUI application runs.");
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
		puts("Not executed. Missing loader, dyld, libSystem, and AppKit or UIKit.");
		puts("Olympium at 25a592cfd65a4f07449ca03e919bfa97626bb164 refuses this guest on Linux.");
		return 3;
	}
	if (n >= 4 && buf[0] == 0x7F && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F') {
		printf("%s: Linux ELF. This is not a Mach-O, AppKit, or UIKit application.\n", path);
		return 0;
	}
	if (n >= 2 && buf[0] == 'P' && buf[1] == 'K') {
		printf("%s: zip container. An IPA is not installed or decrypted.\n", path);
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
			puts("Refusing to treat a Linux ELF as a macOS or iOS application.");
			return 2;
		}
		return rc == 0 ? 2 : rc;
	}
	fprintf(stderr, "Usage: %s matrix | probe FILE | launch FILE\n", argv[0]);
	return 1;
}
