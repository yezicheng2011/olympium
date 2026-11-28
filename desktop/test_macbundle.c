/*
 * Copyright 2026 Daniel Ye
 * Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Reads the bundle that mac-fixtures.py writes, then feeds the readers
 * damaged copies of the same files. A damaged file may be refused. It may
 * not be read past its end, which the sanitizers this test is built with
 * would report.
 *
 *   test-macbundle FIXTURE_DIR
 */
#include "macbundle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void expect(int ok, const char *what) {
	if (!ok) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

static unsigned char *slurp(const char *path, size_t *length) {
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *data = malloc((size_t)size + 1);
	*length = fread(data, 1, (size_t)size, f);
	fclose(f);
	return data;
}

static unsigned next_random(unsigned *state) {
	*state = *state * 1103515245u + 12345u;
	return *state >> 8;
}

static void damage(const unsigned char *good, size_t length, const char *what, int icon) {
	unsigned state = 20261116u;
	for (int round = 0; round < 20000; round++) {
		size_t cut = length;
		if (next_random(&state) % 4 == 0) {
			cut = next_random(&state) % (length + 1);
		}
		/* An exact-size copy, so a read one byte past the end is caught. */
		unsigned char *copy = malloc(cut > 0 ? cut : 1);
		memcpy(copy, good, cut);
		int flips = 1 + (int)(next_random(&state) % 6);
		for (int i = 0; i < flips && cut > 0; i++) {
			copy[next_random(&state) % cut] = (unsigned char)next_random(&state);
		}
		if (icon) {
			size_t at = 0;
			size_t len = 0;
			if (mac_icns_png(copy, cut, &at, &len) == 0) {
				expect(at <= cut && len <= cut - at, what);
			}
		} else {
			char value[64];
			mac_plist_string(copy, cut, "CFBundleExecutable", value, sizeof value);
			mac_plist_string(copy, cut, "CFBundleName", value, sizeof value);
			mac_plist_string(copy, cut, "NoSuchKey", value, sizeof value);
		}
		free(copy);
	}
}

int main(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "usage: test-macbundle PLAIN_BUNDLE_DIR BINARY_PLIST\n");
		return 2;
	}
	struct mac_bundle b;
	expect(mac_is_bundle_dir(argv[1]), "the fixture is a bundle");
	expect(!mac_is_bundle_dir("/usr"), "/usr is not a bundle");
	expect(mac_bundle_read(argv[1], &b) == 0, "bundle read");
	expect(strcmp(b.name, "Desk Mac") == 0, "name");
	expect(strcmp(b.executable, "Desk Mac") == 0, "executable");
	expect(strcmp(b.version, "1.2") == 0, "version");
	expect(strcmp(b.identifier, "example.olympium.deskmac") == 0, "identifier");
	expect(strcmp(b.min_system, "10.13") == 0, "minimum system");
	expect(b.program_kind == MAC_PROGRAM_MACHO, "program is Mach-O");
	char archs[64];
	mac_archs_text(b.archs, b.arch_count, archs, sizeof archs);
	expect(strcmp(archs, "x86_64, arm64") == 0, "architectures");
	expect(strstr(b.icon_path, "/Contents/Resources/DeskMac.icns") != NULL, "icon found without its extension");

	size_t length = 0;
	char path[2048];
	snprintf(path, sizeof path, "%s/Contents/Info.plist", argv[1]);
	unsigned char *xml = slurp(path, &length);
	expect(xml != NULL, "xml plist readable");
	char value[128];
	expect(mac_plist_string(xml, length, "CFBundlePackageType", value, sizeof value) == 0 &&
		strcmp(value, "APPL") == 0, "xml lookup");
	expect(mac_plist_string(xml, length, "NoSuchKey", value, sizeof value) != 0, "xml missing key");
	damage(xml, length, "damaged xml plist", 0);
	free(xml);

	unsigned char *bin = slurp(argv[2], &length);
	expect(bin != NULL && length > 8 && memcmp(bin, "bplist00", 8) == 0, "binary plist readable");
	expect(mac_plist_string(bin, length, "CFBundleExecutable", value, sizeof value) == 0 &&
		strcmp(value, "Desk Mac") == 0, "binary lookup");
	expect(mac_plist_string(bin, length, "CFBundleShortVersionString", value, sizeof value) == 0 &&
		strcmp(value, "1.2") == 0, "binary lookup of a later key");
	expect(mac_plist_string(bin, length, "Wide", value, sizeof value) == 0 &&
		strcmp(value, "\xe8\xae\xa1\xe7\xae\x97\xe5\x99\xa8") == 0, "binary lookup of a UTF-16 value");
	expect(mac_plist_string(bin, length, "NoSuchKey", value, sizeof value) != 0, "binary missing key");
	damage(bin, length, "damaged binary plist", 0);
	free(bin);

	unsigned char *icon = slurp(b.icon_path, &length);
	size_t at = 0;
	size_t png = 0;
	expect(icon != NULL && mac_icns_png(icon, length, &at, &png) == 0, "icns holds a PNG");
	expect(png > 8 && memcmp(icon + at, "\x89PNG", 4) == 0, "the PNG starts as a PNG");
	damage(icon, length, "damaged icns", 1);
	free(icon);

	if (failures == 0) {
		printf("macbundle test passed: bundle, both plist forms, icns, 60000 damaged inputs\n");
	}
	return failures == 0 ? 0 : 1;
}
