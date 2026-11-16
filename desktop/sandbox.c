/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 */
#define _GNU_SOURCE
#include "sandbox.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/landlock.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef LANDLOCK_ACCESS_FS_REFER
#define LANDLOCK_ACCESS_FS_REFER (1ULL << 13)
#endif
#ifndef LANDLOCK_ACCESS_FS_TRUNCATE
#define LANDLOCK_ACCESS_FS_TRUNCATE (1ULL << 14)
#endif

enum { NOBODY_ID = 65534 };

static int landlock_abi(void) {
#ifdef __NR_landlock_create_ruleset
	long abi = syscall(__NR_landlock_create_ruleset, NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
	return abi > 0 ? (int)abi : 0;
#else
	return 0;
#endif
}

enum dev_confinement dev_confine_probe(void) {
	const char *forced = getenv("OLYMPIUM_CONFINE");
	if (forced != NULL && strcmp(forced, "nobody") == 0) {
		/* For the test of the fallback. It can only pick the weaker of the
		 * two confinements, never none. */
		return geteuid() == 0 ? DEV_CONFINE_NOBODY : DEV_CONFINE_NONE;
	}
	if (landlock_abi() > 0) {
		return DEV_CONFINE_LANDLOCK;
	}
	return geteuid() == 0 ? DEV_CONFINE_NOBODY : DEV_CONFINE_NONE;
}

const char *dev_confine_name(enum dev_confinement how) {
	switch (how) {
	case DEV_CONFINE_LANDLOCK:
		return "landlock";
	case DEV_CONFINE_NOBODY:
		return "nobody";
	default:
		return "none";
	}
}

void dev_confine_prepare_dir(const char *write_dir) {
	if (write_dir != NULL && dev_confine_probe() == DEV_CONFINE_NOBODY) {
		if (chown(write_dir, NOBODY_ID, NOBODY_ID) != 0) {
			perror(write_dir);
		}
	}
}

#ifdef __NR_landlock_create_ruleset
static int allow(int ruleset, const char *path, __u64 access) {
	int fd = open(path, O_PATH | O_CLOEXEC);
	if (fd < 0) {
		/* A system without /lib32, say. Nothing to allow. */
		return 0;
	}
	struct stat st;
	if (fstat(fd, &st) == 0 && !S_ISDIR(st.st_mode)) {
		/* Only these rights mean anything for a single file. */
		access &= LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_WRITE_FILE |
			LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_TRUNCATE;
	}
	struct landlock_path_beneath_attr rule = {
		.allowed_access = access,
		.parent_fd = fd,
	};
	int rc = (int)syscall(__NR_landlock_add_rule, ruleset, LANDLOCK_RULE_PATH_BENEATH, &rule, 0);
	close(fd);
	return rc;
}

static int confine_landlock(const char *read_path, const char *write_dir) {
	int abi = landlock_abi();
	__u64 handled = LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_WRITE_FILE |
		LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR |
		LANDLOCK_ACCESS_FS_REMOVE_DIR | LANDLOCK_ACCESS_FS_REMOVE_FILE |
		LANDLOCK_ACCESS_FS_MAKE_CHAR | LANDLOCK_ACCESS_FS_MAKE_DIR |
		LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_MAKE_SOCK |
		LANDLOCK_ACCESS_FS_MAKE_FIFO | LANDLOCK_ACCESS_FS_MAKE_BLOCK |
		LANDLOCK_ACCESS_FS_MAKE_SYM;
	if (abi >= 2) {
		handled |= LANDLOCK_ACCESS_FS_REFER;
	}
	if (abi >= 3) {
		handled |= LANDLOCK_ACCESS_FS_TRUNCATE;
	}
	/* Only the first field is used, so the same call works on every kernel
	 * that has Landlock. Bytes a kernel does not know must be zero. */
	struct landlock_ruleset_attr attr;
	memset(&attr, 0, sizeof attr);
	attr.handled_access_fs = handled;
#ifdef LANDLOCK_ACCESS_NET_BIND_TCP
	if (abi >= 4) {
		/* An unpacker has no reason to open a connection. With no rule
		 * that allows a port, none can be bound or connected. */
		attr.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP | LANDLOCK_ACCESS_NET_CONNECT_TCP;
	}
#endif
	int ruleset = (int)syscall(__NR_landlock_create_ruleset, &attr, sizeof attr, 0);
	if (ruleset < 0) {
		return -1;
	}
	const __u64 read_only = (LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_READ_FILE |
		LANDLOCK_ACCESS_FS_READ_DIR) & handled;
	static const char *const system_dirs[] = {
		"/usr", "/bin", "/sbin", "/lib", "/lib32", "/lib64", "/libx32", "/etc",
	};
	int bad = 0;
	for (size_t i = 0; i < sizeof system_dirs / sizeof system_dirs[0]; i++) {
		bad |= allow(ruleset, system_dirs[i], read_only);
	}
	/* /dev/null and /dev/urandom, nothing else under /dev. */
	bad |= allow(ruleset, "/dev/null", LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_WRITE_FILE);
	bad |= allow(ruleset, "/dev/urandom", LANDLOCK_ACCESS_FS_READ_FILE);
	if (read_path != NULL) {
		bad |= allow(ruleset, read_path,
			(LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR) & handled);
	}
	if (write_dir != NULL) {
		/* Ordinary files, folders and links. No devices, sockets or pipes,
		 * and nothing in there may be executed. */
		__u64 writable = LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_READ_FILE |
			LANDLOCK_ACCESS_FS_READ_DIR | LANDLOCK_ACCESS_FS_REMOVE_DIR |
			LANDLOCK_ACCESS_FS_REMOVE_FILE | LANDLOCK_ACCESS_FS_MAKE_DIR |
			LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_MAKE_SYM |
			LANDLOCK_ACCESS_FS_TRUNCATE | LANDLOCK_ACCESS_FS_REFER;
		/* REFER lets a file be renamed or hard-linked from one folder to
		 * another. Both ends have to be under a folder that grants it, and
		 * this is the only one, so nothing can be moved in or out. */
		bad |= allow(ruleset, write_dir, writable & handled);
	}
	if (bad != 0 || prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
			syscall(__NR_landlock_restrict_self, ruleset, 0) != 0) {
		close(ruleset);
		return -1;
	}
	close(ruleset);
	return 0;
}
#else
static int confine_landlock(const char *read_path, const char *write_dir) {
	(void)read_path;
	(void)write_dir;
	return -1;
}
#endif

static int confine_nobody(const char *read_path) {
	if (setgroups(0, NULL) != 0 || setgid(NOBODY_ID) != 0 || setuid(NOBODY_ID) != 0) {
		return -1;
	}
	if (setuid(0) == 0) {
		/* The drop did not hold. */
		return -1;
	}
	prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	if (read_path != NULL && access(read_path, R_OK) != 0) {
		/* That user cannot read the file, so it cannot unpack it. */
		return -1;
	}
	return 0;
}

int dev_confine_self(const char *read_path, const char *write_dir) {
	enum dev_confinement how = dev_confine_probe();
	if (how == DEV_CONFINE_LANDLOCK) {
		return confine_landlock(read_path, write_dir) == 0 ? DEV_CONFINE_LANDLOCK : -1;
	}
	if (how == DEV_CONFINE_NOBODY) {
		return confine_nobody(read_path) == 0 ? DEV_CONFINE_NOBODY : -1;
	}
	/* An ordinary user on a kernel without Landlock. The unpacker can do
	 * what that user can do, which is what any archive tool gets. Root is
	 * never left unconfined: that case is DEV_CONFINE_NOBODY above. */
	return DEV_CONFINE_NONE;
}
