/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Universal cross-architecture program launcher for Olympium.
 * Supports:
 *   - Linux ELF for x86 (i386), x86_64 (amd64), ARM 32-bit (armhf), ARM 64-bit (arm64)
 *     on ANY host architecture (native fast path when supported, qemu-*-static with
 *     foreign sysroot LD prefix for cross-architecture, or swap-backed 64-bit guest
 *     on 32-bit x86).
 *   - Windows PE (.exe) -> automatic transparent Wine via olympium-wine-exec.
 *   - macOS Mach-O / Universal2 (Intel x86_64 & M-series arm64) -> Darling via olympium-mac-run.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#define EM_386 3
#define EM_ARM 40
#define EM_X86_64 62
#define EM_AARCH64 183

static int read_elf(const char *path, int *cls, int *mach) {
	FILE *f = fopen(path, "rb");
	unsigned char hdr[20];
	if (f == NULL) {
		return -1;
	}
	if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr) {
		fclose(f);
		return -1;
	}
	fclose(f);
	if (hdr[0] != 0x7f || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F') {
		return -1;
	}
	*cls = hdr[4];
	if (hdr[5] == 1) {
		*mach = hdr[18] | (hdr[19] << 8);
	} else {
		*mach = (hdr[18] << 8) | hdr[19];
	}
	return 0;
}

static int is_pe_exe(const char *path) {
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return 0;
	}
	unsigned char hdr[64];
	int ok = 0;
	if (fread(hdr, 1, sizeof hdr, f) == sizeof hdr && hdr[0] == 'M' && hdr[1] == 'Z') {
		unsigned long off = (unsigned long)hdr[0x3c] | ((unsigned long)hdr[0x3d] << 8) |
			((unsigned long)hdr[0x3e] << 16) | ((unsigned long)hdr[0x3f] << 24);
		unsigned char pe[4];
		if (off >= 64 && off <= 1024 * 1024 && fseek(f, (long)off, SEEK_SET) == 0 &&
				fread(pe, 1, 4, f) == 4 && pe[0] == 'P' && pe[1] == 'E' && pe[2] == 0 && pe[3] == 0) {
			ok = 1;
		}
	}
	fclose(f);
	return ok;
}

static int is_macho_file(const char *path) {
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return 0;
	}
	unsigned char m[4];
	int ok = 0;
	if (fread(m, 1, 4, f) == 4) {
		uint32_t magic = ((uint32_t)m[0] << 24) | ((uint32_t)m[1] << 16) |
			((uint32_t)m[2] << 8) | (uint32_t)m[3];
		if (magic == 0xfeedfaceu || magic == 0xcefaedfeu ||
				magic == 0xfeedfacfu || magic == 0xcffaedfeu ||
				magic == 0xcafebabeu || magic == 0xbebafecau ||
				magic == 0xcafebabfu || magic == 0xbfbafecau) {
			ok = 1;
		}
	}
	fclose(f);
	return ok;
}

static const char *qemu_for(int mach) {
	switch (mach) {
	case EM_386:
		return "/usr/bin/qemu-i386-static";
	case EM_X86_64:
		return "/usr/bin/qemu-x86_64-static";
	case EM_ARM:
		return "/usr/bin/qemu-arm-static";
	case EM_AARCH64:
		return "/usr/bin/qemu-aarch64-static";
	default:
		return NULL;
	}
}

static const char *qemu_ld_prefix_for(int mach) {
	const char *candidates[4] = {NULL, NULL, NULL, NULL};
	switch (mach) {
	case EM_386:
		candidates[0] = "/etc/qemu-binfmt/i386";
		candidates[1] = "/usr/i686-linux-gnu";
		break;
	case EM_X86_64:
		candidates[0] = "/etc/qemu-binfmt/x86_64";
		candidates[1] = "/usr/x86_64-linux-gnu";
		break;
	case EM_ARM:
		candidates[0] = "/etc/qemu-binfmt/arm";
		candidates[1] = "/usr/arm-linux-gnueabihf";
		break;
	case EM_AARCH64:
		candidates[0] = "/etc/qemu-binfmt/aarch64";
		candidates[1] = "/usr/aarch64-linux-gnu";
		break;
	default:
		return NULL;
	}
	for (int i = 0; candidates[i] != NULL; i++) {
		if (access(candidates[i], R_OK | X_OK) == 0) {
			return candidates[i];
		}
	}
	return NULL;
}

static int exec_with_qemu(const char *qemu, int mach, char **args, int n) {
	const char *ld_prefix = qemu_ld_prefix_for(mach);
	char *qargs[70];
	int qn = 0;
	qargs[qn++] = (char *)qemu;
	if (ld_prefix != NULL) {
		qargs[qn++] = "-L";
		qargs[qn++] = (char *)ld_prefix;
	}
	for (int i = 0; i <= n && qn < 69; i++) {
		qargs[qn++] = args[i];
	}
	qargs[qn] = NULL;
	execv(qemu, qargs);
	perror(qemu);
	return 127;
}

int main(int argc, char **argv) {
	int hold = 0;
	int index = 1;
	if (index < argc && strcmp(argv[index], "--hold") == 0) {
		hold = 1;
		index++;
	}
	if (index >= argc) {
		fprintf(stderr, "usage: olympium-run [--hold] PROGRAM [ARG...]\n");
		return 2;
	}
	const char *path = argv[index++];
	int cls = 0;
	int mach = 0;
	if (read_elf(path, &cls, &mach) != 0) {
		if (is_pe_exe(path)) {
			const char *wine_exec = "/usr/libexec/dev-desktop/olympium-wine-exec";
			if (access(wine_exec, X_OK) != 0) {
				wine_exec = "/usr/bin/wine";
			}
			char *wargs[66];
			int wn = 0;
			wargs[wn++] = (char *)wine_exec;
			wargs[wn++] = (char *)path;
			while (index < argc && wn < 64) {
				wargs[wn++] = argv[index++];
			}
			wargs[wn] = NULL;
			execv(wine_exec, wargs);
			perror(wine_exec);
			return 127;
		}
		if (is_macho_file(path)) {
			const char *mac_exec = "/usr/libexec/dev-desktop/olympium-mac-run";
			char *margs[66];
			int mn = 0;
			margs[mn++] = (char *)mac_exec;
			margs[mn++] = (char *)path;
			margs[mn++] = (char *)path;
			while (index < argc && mn < 64) {
				margs[mn++] = argv[index++];
			}
			margs[mn] = NULL;
			execv(mac_exec, margs);
			perror(mac_exec);
			return 127;
		}
		fprintf(stderr, "olympium-run: %s is not a supported ELF, Windows PE, or Mach-O program\n", path);
		return 2;
	}

	struct utsname host;
	if (uname(&host) != 0) {
		perror("uname");
		return 1;
	}
	int host_x86_64 = strcmp(host.machine, "x86_64") == 0;
	int host_i386 = strcmp(host.machine, "i686") == 0 || strcmp(host.machine, "i386") == 0;
	int host_arm64 = strcmp(host.machine, "aarch64") == 0 || strcmp(host.machine, "arm64") == 0;
	int host_arm32 = strncmp(host.machine, "arm", 3) == 0 && !host_arm64;

	int native = 0;
	const char *how = NULL;
	if (cls == 2 && mach == EM_X86_64 && host_x86_64) {
		native = 1;
		how = "native x86_64";
	} else if (cls == 2 && mach == EM_AARCH64 && host_arm64) {
		native = 1;
		how = "native aarch64";
	} else if (cls == 1 && mach == EM_386 && (host_x86_64 || host_i386)) {
		native = 1;
		how = host_x86_64 ? "native IA-32" : "native i386";
	} else if (cls == 1 && mach == EM_ARM && (host_arm64 || host_arm32)) {
		native = 1;
		how = host_arm64 ? "native armhf" : "native arm";
	} else if (cls == 2 && mach == EM_X86_64 && host_i386 &&
			access("/usr/libexec/dev-desktop/olympium-run64", X_OK) == 0 &&
			access("/usr/lib/olympium/amd64/vmlinuz", R_OK) == 0) {
		printf("olympium-run: 64-bit program on a 32-bit system, swap-backed guest\n");
		fflush(stdout);
		if (hold) {
			execl("/usr/libexec/dev-desktop/olympium-run64", "olympium-run64",
				"--hold", path, (char *)NULL);
		} else {
			execl("/usr/libexec/dev-desktop/olympium-run64", "olympium-run64",
				"--no-hold", path, (char *)NULL);
		}
		perror("olympium-run64");
		return 127;
	}

	char *args[64];
	int n = 0;
	args[n++] = (char *)path;
	if (hold && n < 62) {
		args[n++] = "--hold";
	}
	while (index < argc && n < 62) {
		args[n++] = argv[index++];
	}
	args[n] = NULL;

	if (native) {
		printf("olympium-run: %s %s\n", how, path);
		fflush(stdout);
		execv(path, args);
		int err = errno;
		fprintf(stderr, "olympium-run: native exec failed: %s\n", strerror(err));
		/* Fallback to user-mode QEMU translation if kernel lacks native compat. */
		const char *qemu = qemu_for(mach);
		if (err == ENOEXEC && qemu != NULL && access(qemu, X_OK) == 0) {
			fprintf(stderr, "olympium-run: falling back to %s\n", qemu);
			return exec_with_qemu(qemu, mach, args, n);
		}
		return 127;
	}

	const char *qemu = qemu_for(mach);
	if (qemu != NULL && access(qemu, X_OK) == 0) {
		printf("olympium-run: cross-arch translation via %s\n", qemu);
		fflush(stdout);
		return exec_with_qemu(qemu, mach, args, n);
	}
	fprintf(stderr, "olympium-run: cannot run ELF class %d machine %d on %s (install qemu-user-static)\n",
		cls, mach, host.machine);
	return 1;
}
