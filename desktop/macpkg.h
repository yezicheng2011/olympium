/*
 * Copyright 2026 Daniel Ye
 * Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Opens the three things a Mac application arrives as: a disk image (.dmg),
 * an installer package (.pkg), and a bare .app folder. Opening means reading
 * what is inside and copying the application into this desktop's own
 * Applications folder. It never means running anything: no program from the
 * image, no installer script from the package.
 */
#ifndef DEV_MACPKG_H
#define DEV_MACPKG_H

#include <stddef.h>

enum { MAC_APPS_MAX = 8 };

struct mac_app_info {
	char name[128];
	char version[48];
	char identifier[160];
	char archs[64];       /* "x86_64, arm64", empty when the program was not read */
	char bundle[1024];    /* installed path, or the path inside the archive in a review */
	char program[1024];   /* installed program, empty in a review */
	char icon[1024];      /* a PNG or .icns a launcher can show, or empty */
	int program_kind;     /* enum mac_program_kind */
	int runs_on_this_cpu;
};

struct mac_review {
	char kind[16];        /* mac_dmg, mac_pkg, mac_app */
	char title[160];
	int app_count;
	struct mac_app_info apps[MAC_APPS_MAX];
	int has_scripts;      /* a package with installer scripts; they are not run */
	char note[320];       /* when app_count is 0: why nothing can be installed */
};

/* "mac_dmg", "mac_pkg", "mac_app", or NULL when the path is none of them. */
const char *mac_detect(const char *path);

/* The archive tool used for disk images and packages, or NULL. */
const char *mac_archiver(void);

/*
 * Reads what is inside without installing anything. cache_dir is a directory
 * this call may fill with the few small files it has to take out to read
 * names and icons. Returns 0 when the file could be read at all.
 */
int mac_review_file(const char *path, const char *cache_dir, struct mac_review *out);

/*
 * Unpacks the applications in path and puts them in desk_root/mac/Applications.
 * Returns how many were installed, or -1 with a sentence in err. Nothing is
 * left half installed: the work happens in a staging directory and an
 * application only moves into place once it is whole.
 */
int mac_install_file(const char *path, const char *desk_root, struct mac_app_info *apps, int max,
	char *err, size_t errlen, void (*progress)(int step, int of, const char *what));

/* Removes what an install that was stopped half way left in its staging
 * folder. Nothing in Applications is touched. */
void mac_clear_stale(const char *desk_root);

/* Decodes a package payload from in_fd to out_fd: gzip, pbzx, xz, or plain. */
int mac_unpayload(int in_fd, int out_fd);

#endif
