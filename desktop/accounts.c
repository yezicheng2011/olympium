/*
 * Copyright 2026 Daniel Ye
 * Licensed under the Apache License, Version 2.0. See LICENSE.
 */
#define _GNU_SOURCE
#include "accounts.h"

#include <crypt.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *acct_root(void) {
	const char *root = getenv("OLYMPIUM_ACCT_ROOT");
	return root != NULL && root[0] == '/' ? root : "";
}

int acct_name_ok(const char *name) {
	if (name == NULL || name[0] < 'a' || name[0] > 'z' || strcmp(name, "root") == 0) {
		return 0;
	}
	size_t n = strlen(name);
	if (n > 32) {
		return 0;
	}
	for (size_t i = 1; i < n; i++) {
		char c = name[i];
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) {
			return 0;
		}
	}
	return 1;
}

int acct_password_ok(const char *password, char *why, size_t n) {
	size_t len = password != NULL ? strlen(password) : 0;
	if (len < 8) {
		snprintf(why, n, "A password needs at least 8 characters.");
		return 0;
	}
	if (len > 256) {
		snprintf(why, n, "A password can be at most 256 characters.");
		return 0;
	}
	for (size_t i = 0; i < len; i++) {
		if (password[i] == '\n' || password[i] == '\r') {
			snprintf(why, n, "A password cannot contain a line break.");
			return 0;
		}
	}
	return 1;
}

/* Copies field number `field` (0 based) of name's line in a colon file. */
static int colon_field(const char *file, const char *name, int field, char *out, size_t n) {
	char path[600];
	snprintf(path, sizeof path, "%s%s", acct_root(), file);
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		return -1;
	}
	char line[1024];
	size_t name_len = strlen(name);
	int found = -1;
	while (fgets(line, sizeof line, f) != NULL) {
		if (strncmp(line, name, name_len) != 0 || line[name_len] != ':') {
			continue;
		}
		line[strcspn(line, "\n")] = '\0';
		char *at = line;
		for (int i = 0; i < field && at != NULL; i++) {
			at = strchr(at, ':');
			if (at != NULL) {
				at++;
			}
		}
		if (at != NULL) {
			char *end = strchr(at, ':');
			if (end != NULL) {
				*end = '\0';
			}
			snprintf(out, n, "%s", at);
			found = 0;
		}
		break;
	}
	fclose(f);
	return found;
}

int acct_exists(const char *name) {
	char uid[32];
	return colon_field("/etc/passwd", name, 2, uid, sizeof uid) == 0;
}

int acct_has_password(const char *name) {
	char hash[512];
	if (colon_field("/etc/shadow", name, 1, hash, sizeof hash) != 0) {
		return 0;
	}
	return hash[0] != '\0' && hash[0] != '!' && hash[0] != '*';
}

int acct_check_password(const char *name, const char *password) {
	char hash[512];
	if (colon_field("/etc/shadow", name, 1, hash, sizeof hash) != 0 ||
			hash[0] == '\0' || hash[0] == '!' || hash[0] == '*') {
		return 0;
	}
	struct crypt_data data;
	memset(&data, 0, sizeof data);
	const char *made = crypt_r(password, hash, &data);
	return made != NULL && strcmp(made, hash) == 0;
}

/* Runs a program, optionally feeding it one line on standard input. */
static int run_fed(char *const argv[], const char *input) {
	int fds[2] = {-1, -1};
	if (input != NULL && pipe(fds) != 0) {
		return -1;
	}
	pid_t pid = fork();
	if (pid < 0) {
		return -1;
	}
	if (pid == 0) {
		if (input != NULL) {
			dup2(fds[0], 0);
			close(fds[0]);
			close(fds[1]);
		}
		if (input == NULL) {
			if (freopen("/dev/null", "r", stdin) == NULL) {
				_exit(126);
			}
		}
		execv(argv[0], argv);
		_exit(127);
	}
	if (input != NULL) {
		close(fds[0]);
		size_t len = strlen(input);
		size_t done = 0;
		while (done < len) {
			ssize_t w = write(fds[1], input + done, len - done);
			if (w <= 0) {
				break;
			}
			done += (size_t)w;
		}
		close(fds[1]);
	}
	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int acct_set_password(const char *name, const char *password) {
	char why[96];
	if (name == NULL || (strcmp(name, "root") != 0 && !acct_name_ok(name)) ||
			!acct_password_ok(password, why, sizeof why)) {
		return -1;
	}
	size_t need = strlen(name) + strlen(password) + 3;
	char *line = malloc(need);
	if (line == NULL) {
		return -1;
	}
	snprintf(line, need, "%s:%s\n", name, password);
	const char *root = acct_root();
	char *with_root[] = {"/usr/sbin/chpasswd", "-R", (char *)root, NULL};
	char *plain[] = {"/usr/sbin/chpasswd", NULL};
	int rc = run_fed(root[0] != '\0' ? with_root : plain, line);
	explicit_bzero(line, need);
	free(line);
	return rc == 0 ? 0 : -1;
}

int acct_create(const char *name, const char *full_name, const char *password) {
	if (!acct_name_ok(name)) {
		return -1;
	}
	char full[128];
	size_t k = 0;
	/* The full name goes into a colon-separated file. */
	for (const char *p = full_name != NULL ? full_name : ""; *p != '\0' && k < sizeof full - 1; p++) {
		if (*p != ':' && *p != ',' && *p != '\n' && *p != '\r') {
			full[k++] = *p;
		}
	}
	full[k] = '\0';
	const char *root = acct_root();
	char *argv[16];
	int n = 0;
	int exists = acct_exists(name);
	argv[n++] = exists ? "/usr/sbin/usermod" : "/usr/sbin/useradd";
	if (root[0] != '\0') {
		argv[n++] = "-R";
		argv[n++] = (char *)root;
	}
	if (!exists) {
		argv[n++] = "-m";
		argv[n++] = "-s";
		argv[n++] = "/bin/bash";
	}
	argv[n++] = "-c";
	argv[n++] = full;
	argv[n++] = (char *)name;
	argv[n] = NULL;
	if (run_fed(argv, NULL) != 0) {
		return -1;
	}
	return acct_set_password(name, password);
}

static void marker_path(char *out, size_t n) {
	snprintf(out, n, "%s/var/lib/olympium/setup-done", acct_root());
}

int acct_setup_done(void) {
	char path[600];
	marker_path(path, sizeof path);
	return access(path, F_OK) == 0;
}

int acct_mark_setup_done(const char *name) {
	char path[600];
	char dir[600];
	snprintf(dir, sizeof dir, "%s/var/lib/olympium", acct_root());
	mkdir(dir, 0755);
	marker_path(path, sizeof path);
	FILE *f = fopen(path, "w");
	if (f == NULL) {
		return -1;
	}
	fprintf(f, "%s\n", name);
	fclose(f);
	return 0;
}

int acct_primary(char *out, size_t n) {
	char path[600];
	marker_path(path, sizeof path);
	FILE *f = fopen(path, "r");
	if (f != NULL) {
		char line[64] = "";
		if (fgets(line, sizeof line, f) != NULL) {
			line[strcspn(line, "\n")] = '\0';
		}
		fclose(f);
		if (acct_name_ok(line) && acct_exists(line)) {
			snprintf(out, n, "%s", line);
			return 0;
		}
	}
	snprintf(path, sizeof path, "%s/etc/passwd", acct_root());
	f = fopen(path, "r");
	if (f == NULL) {
		return -1;
	}
	char line[1024];
	int found = -1;
	while (fgets(line, sizeof line, f) != NULL) {
		char name[64];
		unsigned uid = 0;
		if (sscanf(line, "%63[^:]:%*[^:]:%u:", name, &uid) == 2 && uid >= 1000 && uid < 60000) {
			snprintf(out, n, "%s", name);
			found = 0;
			break;
		}
	}
	fclose(f);
	return found;
}
