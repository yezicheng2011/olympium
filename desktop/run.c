/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * Starts a program. A 32-bit program on a 64-bit CPU of the same family is
 * executed directly. A 64-bit x86 program on a 32-bit CPU is handed to the
 * swap-backed guest. That guest is not used in the other direction.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
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
		fprintf(stderr, "olympium-run: %s is not an ELF program\n", path);
		return 2;
	}
	struct utsname host;
	if (uname(&host) != 0) {
		perror("uname");
		return 1;
	}
	int host_x86_64 = strcmp(host.machine, "x86_64") == 0;
	int host_i386 = strcmp(host.machine, "i686") == 0 || strcmp(host.machine, "i386") == 0;
	int host_arm64 = strcmp(host.machine, "aarch64") == 0;
	int host_arm32 = strncmp(host.machine, "arm", 3) == 0;

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
	} else if (cls == 2 && mach == EM_X86_64 && host_i386) {
		printf("olympium-run: 64-bit program on a 32-bit system, swap-backed guest\n");
		fflush(stdout);
		if (hold) {
			execl("/usr/libexec/dev-desktop/olympium-run64", "olympium-run64",
				"--hold", path, (char *)NULL);
		}
		execl("/usr/libexec/dev-desktop/olympium-run64", "olympium-run64",
			"--no-hold", path, (char *)NULL);
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
		fprintf(stderr, "olympium-run: native exec failed: %s\n", strerror(errno));
		/* The fast path failed. A user-mode translator is only a fallback. */
		const char *qemu = qemu_for(mach);
		if (errno == ENOEXEC && qemu != NULL && access(qemu, X_OK) == 0) {
			fprintf(stderr, "olympium-run: falling back to %s\n", qemu);
			char *qargs[66];
			qargs[0] = (char *)qemu;
			for (int i = 0; i <= n; i++) {
				qargs[i + 1] = args[i];
			}
			execv(qemu, qargs);
			perror(qemu);
		}
		return 127;
	}

	const char *qemu = qemu_for(mach);
	if (qemu != NULL && access(qemu, X_OK) == 0) {
		printf("olympium-run: no native path, user-mode %s\n", qemu);
		fflush(stdout);
		char *qargs[66];
		qargs[0] = (char *)qemu;
		for (int i = 0; i <= n; i++) {
			qargs[i + 1] = args[i];
		}
		execv(qemu, qargs);
		perror(qemu);
		return 127;
	}
	fprintf(stderr, "olympium-run: cannot run class %d machine %d on %s\n",
		cls, mach, host.machine);
	return 1;
}
