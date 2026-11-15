/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * Pid 1 for a small 64-bit guest. The guest exists so a 32-bit machine can
 * run a 64-bit program. It is not a general-purpose system.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void mounts(void) {
	mkdir("/proc", 0755);
	mkdir("/sys", 0755);
	mkdir("/dev", 0755);
	mount("proc", "/proc", "proc", 0, NULL);
	mount("sysfs", "/sys", "sysfs", 0, NULL);
	mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
}

static void console(void) {
	int fd = open("/dev/console", O_RDWR);
	if (fd >= 0) {
		dup2(fd, 0);
		dup2(fd, 1);
		dup2(fd, 2);
		if (fd > 2) {
			close(fd);
		}
	}
}

static void poweroff(void) {
	fflush(stdout);
	sync();
	reboot(RB_POWER_OFF);
	for (;;) {
		pause();
	}
}

static void read_cmd(char *cmd, size_t n) {
	snprintf(cmd, n, "hello");
	int fd = open("/payload/cmd", O_RDONLY);
	if (fd < 0) {
		return;
	}
	char raw[64];
	int got = (int)read(fd, raw, sizeof raw - 1);
	close(fd);
	if (got <= 0) {
		return;
	}
	raw[got] = '\0';
	for (int i = 0; raw[i] != '\0'; i++) {
		if (raw[i] == '\n' || raw[i] == '\r' || raw[i] == ' ') {
			raw[i] = '\0';
			break;
		}
	}
	if (raw[0] != '\0') {
		snprintf(cmd, n, "%s", raw);
	}
}

int main(void) {
	mounts();
	console();
	char cmd[32];
	read_cmd(cmd, sizeof cmd);
	printf("OLYMPIUM64_BEGIN cmd=%s\n", cmd);
	fflush(stdout);
	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		printf("OLYMPIUM64_END exit=127\n");
		poweroff();
	}
	if (pid == 0) {
		if (strcmp(cmd, "highmap") == 0) {
			execl("/usr/bin/highmap64", "highmap64", (char *)NULL);
		} else if (strcmp(cmd, "bash") == 0) {
			execl("/bin/bash", "bash", "-c",
				"echo realistic-bash; echo HOSTTYPE=$HOSTTYPE; echo MACHTYPE=$MACHTYPE; "
				"if [ -x /usr/bin/uname ]; then /usr/bin/uname -m; fi; bash --version",
				(char *)NULL);
		} else if (strcmp(cmd, "payload") == 0) {
			setenv("LD_LIBRARY_PATH", "/lib/x86_64-linux-gnu", 1);
			execl("/payload/program", "program", (char *)NULL);
		} else {
			execl("/usr/bin/hello64", "hello64", (char *)NULL);
		}
		perror("exec");
		_exit(127);
	}
	int status = 0;
	if (waitpid(pid, &status, 0) < 0) {
		perror("wait");
		status = 127 << 8;
	}
	int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
	printf("OLYMPIUM64_END exit=%d\n", code);
	poweroff();
	return 0;
}
