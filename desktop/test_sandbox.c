/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * Tries, from inside the confinement, each thing an unpacker must not be
 * able to do, and each thing it has to be able to do.
 *
 *   test-sandbox READABLE_FILE WRITABLE_DIR OUTSIDE_DIR
 *
 * OUTSIDE_DIR is a directory the confined process was not given. For the
 * kernel's confinement it should be writable by anyone, so that a refusal
 * there is the confinement's doing and not ordinary file permissions. The
 * fallback, running as the user "nobody", is only ordinary file permissions:
 * for it OUTSIDE_DIR should be one that user cannot write to, and what it
 * can still do, which is write wherever everyone can, is not hidden here.
 */
#define _GNU_SOURCE
#include "sandbox.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;

static void expect(int ok, const char *what) {
	printf("%s %s\n", ok ? "  held:" : "  FAIL:", what);
	if (!ok) {
		failures++;
	}
}

int main(int argc, char **argv) {
	if (argc != 4) {
		fprintf(stderr, "usage: test-sandbox READABLE_FILE WRITABLE_DIR OUTSIDE_DIR\n");
		return 2;
	}
	const char *readable = argv[1];
	const char *writable = argv[2];
	const char *outside = argv[3];
	char path[1024];

	/* A listening port for the connection attempt, opened before confinement. */
	int listener = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	socklen_t len = sizeof addr;
	int have_port = listener >= 0 && bind(listener, (struct sockaddr *)&addr, sizeof addr) == 0 &&
		listen(listener, 1) == 0 && getsockname(listener, (struct sockaddr *)&addr, &len) == 0;

	dev_confine_prepare_dir(writable);
	int how = dev_confine_self(readable, writable);
	printf("confinement %s\n", how < 0 ? "refused" : dev_confine_name((enum dev_confinement)how));
	if (how < 0) {
		return 1;
	}
	if (how == DEV_CONFINE_NONE) {
		printf("nothing to test: this user gets no confinement on this kernel\n");
		return 77;
	}

	int fd = open(readable, O_RDONLY);
	expect(fd >= 0, "the file it was given can be read");
	if (fd >= 0) {
		close(fd);
	}
	snprintf(path, sizeof path, "%s/made.txt", writable);
	fd = open(path, O_WRONLY | O_CREAT, 0644);
	expect(fd >= 0, "a file can be created in its folder");
	if (fd >= 0) {
		close(fd);
	}
	snprintf(path, sizeof path, "%s/sub", writable);
	expect(mkdir(path, 0755) == 0, "a folder can be created in its folder");
	snprintf(path, sizeof path, "%s/link", writable);
	expect(symlink("/etc/hostname", path) == 0, "a link can be created in its folder");

	snprintf(path, sizeof path, "%s/planted.txt", outside);
	fd = open(path, O_WRONLY | O_CREAT, 0644);
	expect(fd < 0, "a file cannot be created outside its folder");
	if (fd >= 0) {
		close(fd);
	}
	/* Through a link inside its folder that points outside. */
	snprintf(path, sizeof path, "%s/way-out", writable);
	if (symlink(outside, path) == 0) {
		snprintf(path, sizeof path, "%s/way-out/through-link.txt", writable);
		fd = open(path, O_WRONLY | O_CREAT, 0644);
		expect(fd < 0, "a file cannot be created outside through a link");
		if (fd >= 0) {
			close(fd);
		}
	}
	snprintf(path, sizeof path, "%s/device", writable);
	expect(mknod(path, S_IFCHR | 0666, makedev(1, 3)) != 0, "a device cannot be created");

	if (how == DEV_CONFINE_LANDLOCK) {
		snprintf(path, sizeof path, "%s/pipe", writable);
		expect(mkfifo(path, 0666) != 0, "a named pipe cannot be created");
		fd = open("/etc/passwd", O_WRONLY | O_APPEND);
		expect(fd < 0, "a system file cannot be opened for writing");
		if (fd >= 0) {
			close(fd);
		}
		fd = open("/proc/1/environ", O_RDONLY);
		expect(fd < 0, "another process's environment cannot be read");
		if (fd >= 0) {
			close(fd);
		}
		snprintf(path, sizeof path, "%s/secret.txt", outside);
		fd = open(path, O_RDONLY);
		expect(fd < 0, "a file outside what it was given cannot be read");
		if (fd >= 0) {
			close(fd);
		}
		/* A copy of a real program, placed in its folder, must not run. */
		snprintf(path, sizeof path, "%s/true-copy", writable);
		pid_t pid = fork();
		if (pid == 0) {
			execl(path, path, (char *)NULL);
			_exit(errno == EACCES ? 42 : 43);
		}
		int st = 0;
		waitpid(pid, &st, 0);
		expect(WIFEXITED(st) && WEXITSTATUS(st) == 42, "a program in its folder cannot be executed");
		pid = fork();
		if (pid == 0) {
			execl("/bin/true", "true", (char *)NULL);
			_exit(43);
		}
		waitpid(pid, &st, 0);
		expect(WIFEXITED(st) && WEXITSTATUS(st) == 0, "a system program can be executed");
		if (have_port) {
			int client = socket(AF_INET, SOCK_STREAM, 0);
			int rc = connect(client, (struct sockaddr *)&addr, sizeof addr);
			if (rc != 0 && errno == EACCES) {
				expect(1, "a TCP connection cannot be opened");
			} else if (rc == 0) {
				/* A kernel whose Landlock is older than its network rules. */
				printf("  note:  this kernel's Landlock does not cover TCP; a connection was opened\n");
			} else {
				expect(0, "a TCP connection attempt failed for another reason");
			}
			close(client);
		}
	} else {
		fd = open("/etc/passwd", O_WRONLY | O_APPEND);
		expect(fd < 0, "a system file cannot be opened for writing");
		if (fd >= 0) {
			close(fd);
		}
		expect(setuid(0) != 0, "root cannot be taken back");
	}
	return failures == 0 ? 0 : 1;
}
