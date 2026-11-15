/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * One install window for a file the user chose. Opening a file only inspects
 * it. Import and launch require consent. This program calls the pinned
 * Olympium command for classify, plan, and add. It does not download guest
 * images, and it does not run a plan it has not tested.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct review {
	char file[512];
	char name[128];
	char guest[32];
	char kind[48];
	char detail[240];
	char message[480];
	char program[512];
	char launch_type[40];
	char id[80];
	int runtime;
	int plan_ok;
	int hides;
	int native_mode;
	int launchable;
	int cache_hit;
	const char *action;
};

static void mkdir_p(const char *path) {
	char buf[512];
	snprintf(buf, sizeof buf, "%s", path);
	for (char *p = buf + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(buf, 0755);
			*p = '/';
		}
	}
	mkdir(buf, 0755);
}

static void desk_root(char *out, size_t n) {
	const char *env = getenv("OLYMPIUM_DESK_HOME");
	if (env != NULL && env[0] != '\0') {
		snprintf(out, n, "%s", env);
		return;
	}
	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		home = "/tmp";
	}
	snprintf(out, n, "%s/.local/share/olympium-desk", home);
}

static void apps_dir(char *out, size_t n) {
	const char *env = getenv("OLYMPIUM_DESK_HOME");
	if (env != NULL && env[0] != '\0') {
		snprintf(out, n, "%s/applications", env);
		return;
	}
	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		home = "/tmp";
	}
	snprintf(out, n, "%s/.local/share/applications", home);
}

static const char *runtime_bin(void) {
	const char *env = getenv("OLYMPIUM_BIN");
	if (env != NULL && env[0] != '\0' && access(env, X_OK) == 0) {
		return env;
	}
	if (access("/usr/bin/olympium", X_OK) == 0) {
		return "/usr/bin/olympium";
	}
	return NULL;
}

static int capture(char *const argv[], char *out, size_t outlen) {
	int fds[2];
	if (pipe(fds) != 0) {
		return 1;
	}
	pid_t pid = fork();
	if (pid < 0) {
		return 1;
	}
	if (pid == 0) {
		dup2(fds[1], 1);
		close(fds[0]);
		close(fds[1]);
		execv(argv[0], argv);
		_exit(127);
	}
	close(fds[1]);
	size_t n = 0;
	while (n + 1 < outlen) {
		ssize_t r = read(fds[0], out + n, outlen - 1 - n);
		if (r <= 0) {
			break;
		}
		n += (size_t)r;
	}
	out[n] = '\0';
	close(fds[0]);
	int st = 0;
	waitpid(pid, &st, 0);
	return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}

static int json_true(const char *text, const char *key) {
	char pat[80];
	snprintf(pat, sizeof pat, "\"%s\": true", key);
	if (strstr(text, pat) != NULL) {
		return 1;
	}
	snprintf(pat, sizeof pat, "\"%s\":true", key);
	return strstr(text, pat) != NULL;
}

static int json_string(const char *text, const char *key, char *out, size_t outlen) {
	char pat[80];
	snprintf(pat, sizeof pat, "\"%s\"", key);
	const char *p = strstr(text, pat);
	if (p == NULL) {
		return -1;
	}
	p += strlen(pat);
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ':') {
		p++;
	}
	if (*p != '"') {
		return -1;
	}
	p++;
	size_t n = 0;
	while (*p && *p != '"' && n + 1 < outlen) {
		if (*p == '\\' && p[1] != '\0') {
			p++;
		}
		out[n++] = *p++;
	}
	out[n] = '\0';
	return 0;
}

static int file_sha256(const char *path, char *out, size_t outlen) {
	int fds[2];
	if (pipe(fds) != 0) {
		return -1;
	}
	pid_t pid = fork();
	if (pid == 0) {
		dup2(fds[1], 1);
		close(fds[0]);
		close(fds[1]);
		execlp("sha256sum", "sha256sum", "--", path, (char *)NULL);
		_exit(127);
	}
	close(fds[1]);
	char buf[128];
	size_t n = 0;
	while (n + 1 < sizeof buf) {
		ssize_t r = read(fds[0], buf + n, sizeof buf - 1 - n);
		if (r <= 0) {
			break;
		}
		n += (size_t)r;
	}
	buf[n] = '\0';
	close(fds[0]);
	int st = 0;
	waitpid(pid, &st, 0);
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
		return -1;
	}
	char *sp = strchr(buf, ' ');
	if (sp != NULL) {
		*sp = '\0';
	}
	snprintf(out, outlen, "%s", buf);
	return 0;
}

static void base_name(const char *path, char *out, size_t n) {
	const char *slash = strrchr(path, '/');
	snprintf(out, n, "%s", slash != NULL ? slash + 1 : path);
}

static int local_kind(const char *path, struct review *r) {
	unsigned char mag[128];
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return -1;
	}
	size_t n = fread(mag, 1, sizeof mag, f);
	fclose(f);
	struct stat st;
	if (stat(path, &st) != 0) {
		return -1;
	}
	int execbit = (st.st_mode & 0111) != 0;
	if (n >= 4 && mag[0] == 0x7f && mag[1] == 'E' && mag[2] == 'L' && mag[3] == 'F' && execbit) {
		snprintf(r->guest, sizeof r->guest, "linux");
		snprintf(r->kind, sizeof r->kind, "linux_executable");
		snprintf(r->detail, sizeof r->detail, "Linux ELF program.");
		snprintf(r->program, sizeof r->program, "%s", path);
		snprintf(r->launch_type, sizeof r->launch_type, "process");
		r->hides = 1;
		r->native_mode = 1;
		r->plan_ok = 1;
		return 0;
	}
	if (n >= 2 && mag[0] == '#' && mag[1] == '!' && execbit) {
		snprintf(r->guest, sizeof r->guest, "linux");
		snprintf(r->kind, sizeof r->kind, "linux_script");
		snprintf(r->detail, sizeof r->detail, "Script with a shebang.");
		snprintf(r->program, sizeof r->program, "%s", path);
		snprintf(r->launch_type, sizeof r->launch_type, "process");
		r->hides = 1;
		r->native_mode = 1;
		r->plan_ok = 1;
		return 0;
	}
	if (n >= 8 && memcmp(mag, "!<arch>\n", 8) == 0) {
		snprintf(r->guest, sizeof r->guest, "linux");
		snprintf(r->kind, sizeof r->kind, "linux_deb");
		snprintf(r->detail, sizeof r->detail, "Debian package.");
		return 0;
	}
	return 0;
}

static void apply_runtime(struct review *r) {
	const char *bin = runtime_bin();
	if (bin == NULL) {
		r->runtime = 0;
		local_kind(r->file, r);
		snprintf(r->message, sizeof r->message,
			"The Olympium command is not on this image. Only a Linux program is launchable here.");
		return;
	}
	r->runtime = 1;
	char text[8192];
	char *argv1[] = {(char *)bin, "--json", "classify", r->file, NULL};
	capture(argv1, text, sizeof text);
	json_string(text, "guest", r->guest, sizeof r->guest);
	json_string(text, "kind", r->kind, sizeof r->kind);
	json_string(text, "detail", r->detail, sizeof r->detail);
	if (!json_true(text, "ok")) {
		json_string(text, "message", r->message, sizeof r->message);
		return;
	}
	char *argv2[] = {(char *)bin, "--json", "plan", "--file", r->file, "--name", r->name, NULL};
	capture(argv2, text, sizeof text);
	r->plan_ok = json_true(text, "ok");
	r->hides = json_true(text, "hides_runtime_chrome");
	r->native_mode = strstr(text, "\"mode\": \"native\"") != NULL;
	if (strstr(text, "\"type\": \"process\"") != NULL) {
		snprintf(r->launch_type, sizeof r->launch_type, "process");
	} else if (strstr(text, "\"type\": \"extract_then_run\"") != NULL) {
		snprintf(r->launch_type, sizeof r->launch_type, "extract_then_run");
	}
	json_string(text, "program", r->program, sizeof r->program);
	json_string(text, "summary", r->message, sizeof r->message);
	if (!r->plan_ok) {
		json_string(text, "message", r->message, sizeof r->message);
	}
}

static void decide(struct review *r) {
	r->launchable = 0;
	r->action = "refuse";
	if (strcmp(r->guest, "ios") == 0) {
		snprintf(r->message, sizeof r->message, "iOS is not part of this system.");
		return;
	}
	int linux_program = strcmp(r->guest, "linux") == 0 &&
		(strcmp(r->kind, "linux_executable") == 0 || strcmp(r->kind, "linux_script") == 0);
	if (linux_program && r->plan_ok && r->hides && r->native_mode &&
			strcmp(r->launch_type, "process") == 0 &&
			r->program[0] != '\0' && access(r->program, X_OK) == 0) {
		r->launchable = 1;
		r->action = "import";
		if (r->message[0] == '\0') {
			snprintf(r->message, sizeof r->message,
				"Runs as its own window on this desktop. Nothing is downloaded.");
		}
		return;
	}
	if (strcmp(r->guest, "macos") == 0) {
		r->action = "keep";
		if (r->message[0] == '\0') {
			snprintf(r->message, sizeof r->message,
				"The file can be kept in the library. It will not be started.");
		}
		return;
	}
	if (r->kind[0] != '\0') {
		r->action = "review";
		if (r->message[0] == '\0') {
			snprintf(r->message, sizeof r->message,
				"Recognized. This desktop has not tested an install of this format, so it will not run it.");
		}
		return;
	}
	snprintf(r->message, sizeof r->message, "This file is not a supported package.");
}

static void cache_file(const char *path, char *out, size_t n) {
	struct stat st;
	unsigned long key = 5381;
	for (const char *p = path; *p; p++) {
		key = key * 33 + (unsigned char)*p;
	}
	if (stat(path, &st) == 0) {
		key ^= (unsigned long)st.st_mtime ^ (unsigned long)st.st_size;
	}
	char root[512];
	char dir[512];
	desk_root(root, sizeof root);
	snprintf(dir, sizeof dir, "%s/cache", root);
	mkdir_p(dir);
	snprintf(out, n, "%s/%lx", dir, key);
}

static const char *or_dash(const char *s) {
	return s[0] == '\0' ? "-" : s;
}

static void undash(char *s) {
	if (strcmp(s, "-") == 0) {
		s[0] = '\0';
	}
}

static int load_cache(struct review *r) {
	char path[512];
	cache_file(r->file, path, sizeof path);
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		return 0;
	}
	char line[1800];
	if (fgets(line, sizeof line, f) == NULL) {
		fclose(f);
		return 0;
	}
	fclose(f);
	if (sscanf(line, "%31[^\t]\t%47[^\t]\t%239[^\t]\t%479[^\t]\t%511[^\t]\t%39[^\t]\t%d\t%d\t%d",
			r->guest, r->kind, r->detail, r->message, r->program, r->launch_type,
			&r->plan_ok, &r->hides, &r->native_mode) < 9) {
		return 0;
	}
	undash(r->guest);
	undash(r->kind);
	undash(r->detail);
	undash(r->message);
	undash(r->program);
	undash(r->launch_type);
	r->cache_hit = 1;
	r->runtime = runtime_bin() != NULL;
	return 1;
}

static void save_cache(const struct review *r) {
	char path[512];
	cache_file(r->file, path, sizeof path);
	FILE *f = fopen(path, "w");
	if (f == NULL) {
		return;
	}
	fprintf(f, "%s\t%s\t%s\t%s\t%s\t%s\t%d\t%d\t%d\n",
		or_dash(r->guest), or_dash(r->kind), or_dash(r->detail), or_dash(r->message),
		or_dash(r->program), or_dash(r->launch_type),
		r->plan_ok, r->hides, r->native_mode);
	fclose(f);
}

static void print_review(const struct review *r) {
	char message[480];
	snprintf(message, sizeof message, "%s", r->message);
	for (char *p = message; *p; p++) {
		if (*p == '"' || *p == '\n' || *p == '\r') {
			*p = '\'';
		}
	}
	printf("{\n");
	printf("  \"file\": \"%s\",\n", r->file);
	printf("  \"guest\": \"%s\",\n", r->guest);
	printf("  \"kind\": \"%s\",\n", r->kind);
	printf("  \"detail\": \"%s\",\n", r->detail);
	printf("  \"action\": \"%s\",\n", r->action);
	printf("  \"launchable\": %s,\n", r->launchable ? "true" : "false");
	printf("  \"launch_type\": \"%s\",\n", r->launch_type);
	printf("  \"runtime\": \"%s\",\n", r->runtime ? "present" : "absent");
	printf("  \"cache\": \"%s\",\n", r->cache_hit ? "hit" : "miss");
	printf("  \"download\": \"none\",\n");
	printf("  \"ran\": false,\n");
	printf("  \"message\": \"%s\"\n", message);
	printf("}\n");
}

static int fill_review(const char *path, struct review *r) {
	memset(r, 0, sizeof *r);
	if (realpath(path, r->file) == NULL) {
		snprintf(r->file, sizeof r->file, "%s", path);
	}
	base_name(r->file, r->name, sizeof r->name);
	if (!load_cache(r)) {
		apply_runtime(r);
		save_cache(r);
	}
	decide(r);
	return 0;
}

static int library_path(char *out, size_t n) {
	char root[512];
	desk_root(root, sizeof root);
	mkdir_p(root);
	snprintf(out, n, "%s/library.tsv", root);
	return 0;
}

struct rec {
	char id[80];
	char name[128];
	char path[512];
	char sha[80];
	char guest[32];
	char kind[48];
	char launch[16];
	char ext[32];
};

static int load_recs(struct rec *recs, int cap) {
	char path[512];
	library_path(path, sizeof path);
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		return 0;
	}
	int n = 0;
	char line[1400];
	while (n < cap && fgets(line, sizeof line, f) != NULL) {
		if (line[0] == '#' || line[0] == '\n') {
			continue;
		}
		struct rec *r = &recs[n];
		memset(r, 0, sizeof *r);
		if (sscanf(line, "%79[^\t]\t%127[^\t]\t%511[^\t]\t%79[^\t]\t%31[^\t]\t%47[^\t]\t%15[^\t]\t%31[^\n]",
				r->id, r->name, r->path, r->sha, r->guest, r->kind, r->launch, r->ext) >= 7) {
			n++;
		}
	}
	fclose(f);
	return n;
}

static int save_recs(struct rec *recs, int n) {
	char path[512];
	library_path(path, sizeof path);
	FILE *f = fopen(path, "w");
	if (f == NULL) {
		return -1;
	}
	fprintf(f, "# id name path sha256 guest kind launch ext\n");
	for (int i = 0; i < n; i++) {
		fprintf(f, "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
			recs[i].id, recs[i].name, recs[i].path, recs[i].sha,
			recs[i].guest, recs[i].kind, recs[i].launch, recs[i].ext);
	}
	fclose(f);
	return 0;
}

static int write_launcher(const struct rec *rec) {
	char root[512];
	char dir[512];
	char wrap[512];
	char apps[512];
	char desk[512];
	char self[512];
	desk_root(root, sizeof root);
	snprintf(dir, sizeof dir, "%s/launch", root);
	mkdir_p(dir);
	snprintf(wrap, sizeof wrap, "%s/%s", dir, rec->id);
	ssize_t self_n = readlink("/proc/self/exe", self, sizeof self - 1);
	if (self_n < 0) {
		snprintf(self, sizeof self, "/usr/bin/dev-install");
	} else {
		self[self_n] = '\0';
	}
	FILE *f = fopen(wrap, "w");
	if (f == NULL) {
		return -1;
	}
	fprintf(f, "#!/bin/sh\n");
	fprintf(f, "export OLYMPIUM_DESK_HOME='%s'\n", root);
	const char *bin = runtime_bin();
	if (bin != NULL) {
		fprintf(f, "export OLYMPIUM_BIN='%s'\n", bin);
	}
	fprintf(f, "exec '%s' launch '%s' \"$@\"\n", self, rec->id);
	fclose(f);
	chmod(wrap, 0755);
	apps_dir(apps, sizeof apps);
	mkdir_p(apps);
	snprintf(desk, sizeof desk, "%s/olympium-%s.desktop", apps, rec->id);
	f = fopen(desk, "w");
	if (f == NULL) {
		return -1;
	}
	fprintf(f, "[Desktop Entry]\nType=Application\nName=%s\nExec=%s\n", rec->name, wrap);
	fprintf(f, "Terminal=false\nNoDisplay=false\n");
	if (rec->ext[0] != '\0') {
		fprintf(f, "MimeType=application/x-olympium-%s;\n", rec->ext);
	}
	fclose(f);
	char pin[512];
	snprintf(pin, sizeof pin, "%s/pinned", root);
	FILE *pins = fopen(pin, "a");
	if (pins != NULL) {
		fprintf(pins, "%s\t%s\n", rec->name, wrap);
		fclose(pins);
	}
	if (rec->ext[0] != '\0') {
		char mime[512];
		snprintf(mime, sizeof mime, "%s/mimeapps.list", root);
		FILE *m = fopen(mime, "a");
		if (m != NULL) {
			fprintf(m, "application/x-olympium-%s=olympium-%s.desktop\n", rec->ext, rec->id);
			fclose(m);
		}
	}
	printf("desktop=%s\n", desk);
	printf("wrapper=%s\n", wrap);
	printf("pinned=%s\n", pin);
	return 0;
}

static int cmd_import(const char *path, const char *ext, int consent) {
	struct review r;
	fill_review(path, &r);
	printf("progress 1/4 classify\n");
	printf("progress 2/4 plan\n");
	if (!consent) {
		fprintf(stderr, "dev-install: refused to import without consent\n");
		print_review(&r);
		return 2;
	}
	if (strcmp(r.action, "import") != 0 && strcmp(r.action, "keep") != 0) {
		fprintf(stderr, "dev-install: not installed: %s\n", r.message);
		print_review(&r);
		return 3;
	}
	char sha[80] = "";
	if (file_sha256(r.file, sha, sizeof sha) != 0) {
		fprintf(stderr, "dev-install: could not checksum the file\n");
		return 1;
	}
	printf("progress 3/4 checksum\n");
	const char *bin = runtime_bin();
	char id[80];
	snprintf(id, sizeof id, "local-%.12s", sha);
	if (bin != NULL && (strcmp(r.action, "import") == 0 || strcmp(r.action, "keep") == 0)) {
		char text[4096];
		char *argv[] = {(char *)bin, "--json", "add", "--file", r.file, "--name", r.name, NULL};
		capture(argv, text, sizeof text);
		if (!json_true(text, "ok")) {
			fprintf(stderr, "dev-install: the runtime did not accept the file\n");
			return 1;
		}
		json_string(text, "id", id, sizeof id);
	}
	struct rec recs[64];
	int n = load_recs(recs, 64);
	int slot = n;
	for (int i = 0; i < n; i++) {
		if (strcmp(recs[i].path, r.file) == 0) {
			slot = i;
			break;
		}
	}
	if (slot == n) {
		if (n >= 64) {
			fprintf(stderr, "dev-install: library is full\n");
			return 1;
		}
		n++;
	}
	struct rec *rec = &recs[slot];
	memset(rec, 0, sizeof *rec);
	snprintf(rec->id, sizeof rec->id, "%s", id);
	snprintf(rec->name, sizeof rec->name, "%s", r.name);
	snprintf(rec->path, sizeof rec->path, "%s", r.file);
	snprintf(rec->sha, sizeof rec->sha, "%s", sha);
	snprintf(rec->guest, sizeof rec->guest, "%s", r.guest);
	snprintf(rec->kind, sizeof rec->kind, "%s", r.kind);
	snprintf(rec->launch, sizeof rec->launch, "%s", r.launchable ? "native" : "refused");
	if (ext != NULL) {
		snprintf(rec->ext, sizeof rec->ext, "%s", ext);
	}
	if (save_recs(recs, n) != 0) {
		return 1;
	}
	printf("id=%s\n", rec->id);
	printf("launch=%s\n", rec->launch);
	if (r.launchable) {
		if (write_launcher(rec) != 0) {
			return 1;
		}
	}
	printf("progress 4/4 desktop\n");
	printf("download: none\n");
	return 0;
}

static int cmd_launch(const char *id, int argc, char **argv) {
	struct rec recs[64];
	int n = load_recs(recs, 64);
	struct rec *rec = NULL;
	for (int i = 0; i < n; i++) {
		if (strcmp(recs[i].id, id) == 0) {
			rec = &recs[i];
			break;
		}
	}
	if (rec == NULL) {
		fprintf(stderr, "dev-install: unknown id %s\n", id);
		return 1;
	}
	char sha[80];
	if (file_sha256(rec->path, sha, sizeof sha) != 0 || strcmp(sha, rec->sha) != 0) {
		fprintf(stderr, "dev-install: checksum changed; not started\n");
		return 4;
	}
	if (strcmp(rec->launch, "native") != 0) {
		fprintf(stderr, "dev-install: %s is in the library and is not started\n", rec->name);
		return 3;
	}
	char *args[64];
	int k = 0;
	const char *runner = getenv("OLYMPIUM_RUN");
	if (runner == NULL || runner[0] == '\0') {
		runner = "/usr/libexec/dev-desktop/olympium-run";
	}
	int elf = 0;
	FILE *f = fopen(rec->path, "rb");
	if (f != NULL) {
		unsigned char mag[4];
		if (fread(mag, 1, 4, f) == 4 && mag[0] == 0x7f && mag[1] == 'E' && mag[2] == 'L' && mag[3] == 'F') {
			elf = 1;
		}
		fclose(f);
	}
	if (elf && access(runner, X_OK) == 0) {
		args[k++] = (char *)runner;
	}
	args[k++] = rec->path;
	for (int i = 0; i < argc && k < 62; i++) {
		args[k++] = argv[i];
	}
	args[k] = NULL;
	execv(args[0], args);
	perror(args[0]);
	return 127;
}

static long millis_between(const struct timespec *a, const struct timespec *b) {
	return (b->tv_sec - a->tv_sec) * 1000 + (b->tv_nsec - a->tv_nsec) / 1000000;
}

static int cmd_timings(void) {
	const char *bin = runtime_bin();
	struct timespec a, b;
	if (bin != NULL) {
		char junk[2048];
		char *argv[] = {(char *)bin, "--json", "run", "--file", "/usr/bin/true",
			"--name", "True", "--capture", NULL};
		clock_gettime(CLOCK_MONOTONIC, &a);
		capture(argv, junk, sizeof junk);
		clock_gettime(CLOCK_MONOTONIC, &b);
		printf("cold_runtime_ms %ld\n", millis_between(&a, &b));
		clock_gettime(CLOCK_MONOTONIC, &a);
		capture(argv, junk, sizeof junk);
		clock_gettime(CLOCK_MONOTONIC, &b);
		printf("warm_runtime_ms %ld\n", millis_between(&a, &b));
	} else {
		printf("cold_runtime_ms absent\n");
		printf("warm_runtime_ms absent\n");
	}
	for (int i = 0; i < 2; i++) {
		clock_gettime(CLOCK_MONOTONIC, &a);
		pid_t pid = fork();
		if (pid == 0) {
			execl("/usr/bin/true", "true", (char *)NULL);
			_exit(127);
		}
		int st = 0;
		waitpid(pid, &st, 0);
		clock_gettime(CLOCK_MONOTONIC, &b);
		printf("%s %ld\n", i == 0 ? "cold_exec_ms" : "warm_exec_ms", millis_between(&a, &b));
	}
	return 0;
}

static struct review g_review;
static GtkWidget *g_status;
static GtkWidget *g_import;
static GtkWidget *g_launch;
static char g_ext[32];

static void on_consent(GtkToggleButton *button, gpointer data) {
	(void)data;
	int on = gtk_toggle_button_get_active(button);
	int allow = on && (strcmp(g_review.action, "import") == 0 || strcmp(g_review.action, "keep") == 0);
	gtk_widget_set_sensitive(g_import, allow);
}

static void on_import(GtkButton *button, gpointer data) {
	(void)button;
	(void)data;
	int code = cmd_import(g_review.file, g_ext[0] ? g_ext : NULL, 1);
	if (code == 0 && g_review.launchable) {
		gtk_label_set_text(GTK_LABEL(g_status), "Imported. Launch opens this program in its own window.");
		gtk_widget_set_sensitive(g_launch, TRUE);
	} else if (code == 0) {
		gtk_label_set_text(GTK_LABEL(g_status), "Kept in the library. Launch stays off.");
	} else {
		gtk_label_set_text(GTK_LABEL(g_status), "Not installed.");
	}
}

static void on_launch(GtkButton *button, gpointer data) {
	(void)button;
	(void)data;
	struct rec recs[64];
	int n = load_recs(recs, 64);
	for (int i = 0; i < n; i++) {
		if (strcmp(recs[i].path, g_review.file) == 0 && strcmp(recs[i].launch, "native") == 0) {
			pid_t pid = fork();
			if (pid == 0) {
				execl("/proc/self/exe", "dev-install", "launch", recs[i].id, (char *)NULL);
				_exit(127);
			}
			gtk_label_set_text(GTK_LABEL(g_status), "Launch started.");
			return;
		}
	}
	gtk_label_set_text(GTK_LABEL(g_status), "Nothing to launch.");
}

static GtkWidget *build_window(const struct review *r) {
	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), "Install");
	gtk_window_set_default_size(GTK_WINDOW(win), 560, 420);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(box), 18);
	gtk_container_add(GTK_CONTAINER(win), box);
	GtkWidget *title = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(title), "<span size='large' weight='bold'>Install</span>");
	gtk_widget_set_halign(title, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);
	char line[700];
	snprintf(line, sizeof line, "%s\n%s · %s\n%s", r->name, r->kind[0] ? r->kind : "unknown",
		r->guest[0] ? r->guest : "unknown", r->message);
	GtkWidget *body = gtk_label_new(line);
	gtk_label_set_xalign(GTK_LABEL(body), 0);
	gtk_label_set_line_wrap(GTK_LABEL(body), TRUE);
	gtk_box_pack_start(GTK_BOX(box), body, FALSE, FALSE, 0);
	int can_import = strcmp(r->action, "import") == 0 || strcmp(r->action, "keep") == 0;
	GtkWidget *bar = gtk_progress_bar_new();
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar), 0);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(bar),
		"No download. This desktop does not fetch guest images.");
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(bar), TRUE);
	gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
	GtkWidget *check = gtk_check_button_new_with_label(
		can_import ? "Import this file. It will not run until Launch."
			: "Import stays off. This format has not been installed here.");
	gtk_widget_set_sensitive(check, can_import);
	gtk_box_pack_start(GTK_BOX(box), check, FALSE, FALSE, 0);
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	g_import = gtk_button_new_with_label("Import");
	g_launch = gtk_button_new_with_label("Launch");
	gtk_widget_set_sensitive(g_import, FALSE);
	gtk_widget_set_sensitive(g_launch, FALSE);
	gtk_box_pack_start(GTK_BOX(row), g_import, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), g_launch, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
	g_status = gtk_label_new(can_import
		? "Nothing has been run."
		: "Recognized only. Nothing was installed or run.");
	gtk_widget_set_halign(g_status, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(box), g_status, FALSE, FALSE, 0);
	g_signal_connect(check, "toggled", G_CALLBACK(on_consent), NULL);
	g_signal_connect(g_import, "clicked", G_CALLBACK(on_import), NULL);
	g_signal_connect(g_launch, "clicked", G_CALLBACK(on_launch), NULL);
	g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	return win;
}

static int cmd_preview(const char *path, const char *png) {
	fill_review(path, &g_review);
	gtk_init(NULL, NULL);
	GtkWidget *off = gtk_offscreen_window_new();
	GtkWidget *win = build_window(&g_review);
	gtk_widget_show_all(win);
	/* The offscreen window is unused as a parent; realize the real window under Xvfb. */
	(void)off;
	while (gtk_events_pending()) {
		gtk_main_iteration();
	}
	GdkWindow *gw = gtk_widget_get_window(win);
	if (gw == NULL) {
		fprintf(stderr, "dev-install: no window\n");
		return 1;
	}
	gint w = gtk_widget_get_allocated_width(win);
	gint h = gtk_widget_get_allocated_height(win);
	GdkPixbuf *pix = gdk_pixbuf_get_from_window(gw, 0, 0, w, h);
	if (pix == NULL) {
		fprintf(stderr, "dev-install: could not read the window\n");
		return 1;
	}
	GError *err = NULL;
	if (!gdk_pixbuf_save(pix, png, "png", &err, NULL)) {
		fprintf(stderr, "dev-install: %s\n", err != NULL ? err->message : "save failed");
		return 1;
	}
	printf("preview=%s\n", png);
	return 0;
}

static int extension_of(const char *path, char *out, size_t n) {
	const char *dot = strrchr(path, '.');
	const char *slash = strrchr(path, '/');
	if (dot == NULL || (slash != NULL && dot < slash) || dot[1] == '\0') {
		out[0] = '\0';
		return -1;
	}
	snprintf(out, n, "%s", dot + 1);
	return 0;
}

static int is_package(const struct review *r) {
	if (r->launchable) {
		return 1;
	}
	if (strcmp(r->guest, "ios") == 0 || strcmp(r->guest, "macos") == 0 ||
			strcmp(r->guest, "android") == 0 || strcmp(r->guest, "windows") == 0 ||
			strcmp(r->guest, "openharmony") == 0) {
		return 1;
	}
	if (strcmp(r->kind, "linux_executable") == 0 || strcmp(r->kind, "linux_script") == 0 ||
			strcmp(r->kind, "linux_deb") == 0 || strcmp(r->kind, "linux_rpm") == 0) {
		return 1;
	}
	return 0;
}

static int cmd_open(const char *path, int no_ui) {
	char ext[32];
	extension_of(path, ext, sizeof ext);
	struct rec recs[64];
	int n = load_recs(recs, 64);
	struct review r;
	fill_review(path, &r);
	if (!is_package(&r)) {
		for (int i = 0; i < n; i++) {
			if (recs[i].ext[0] != '\0' && strcmp(recs[i].ext, ext) == 0 &&
					strcmp(recs[i].launch, "native") == 0) {
				char root[512];
				char wrap[640];
				desk_root(root, sizeof root);
				snprintf(wrap, sizeof wrap, "%s/launch/%s", root, recs[i].id);
				execl(wrap, wrap, path, (char *)NULL);
				perror(wrap);
				return 127;
			}
		}
		fprintf(stderr, "dev-install: no application for this file\n");
		return 2;
	}
	print_review(&r);
	if (!no_ui && getenv("DISPLAY") != NULL) {
		g_review = r;
		gtk_init(NULL, NULL);
		GtkWidget *win = build_window(&r);
		gtk_widget_show_all(win);
		gtk_main();
	}
	return 0;
}

static void usage(void) {
	fprintf(stderr,
		"usage: dev-install inspect FILE\n"
		"       dev-install import --consent FILE [--mime EXT]\n"
		"       dev-install launch ID [ARG...]\n"
		"       dev-install open [--no-ui] FILE\n"
		"       dev-install preview FILE PNG\n"
		"       dev-install timings\n"
		"       dev-install exchange\n");
}

int main(int argc, char **argv) {
	if (argc < 2) {
		usage();
		return 2;
	}
	if (strcmp(argv[1], "inspect") == 0 && argc == 3) {
		struct review r;
		fill_review(argv[2], &r);
		print_review(&r);
		return 0;
	}
	if (strcmp(argv[1], "import") == 0) {
		int consent = 0;
		const char *file = NULL;
		const char *ext = NULL;
		for (int i = 2; i < argc; i++) {
			if (strcmp(argv[i], "--consent") == 0) {
				consent = 1;
			} else if (strcmp(argv[i], "--mime") == 0 && i + 1 < argc) {
				ext = argv[++i];
			} else if (file == NULL) {
				file = argv[i];
			}
		}
		if (file == NULL) {
			usage();
			return 2;
		}
		return cmd_import(file, ext, consent);
	}
	if (strcmp(argv[1], "launch") == 0 && argc >= 3) {
		return cmd_launch(argv[2], argc - 3, argv + 3);
	}
	if (strcmp(argv[1], "open") == 0) {
		int no_ui = 0;
		const char *file = NULL;
		for (int i = 2; i < argc; i++) {
			if (strcmp(argv[i], "--no-ui") == 0) {
				no_ui = 1;
			} else if (file == NULL) {
				file = argv[i];
			}
		}
		if (file == NULL) {
			usage();
			return 2;
		}
		return cmd_open(file, no_ui);
	}
	if (strcmp(argv[1], "preview") == 0 && argc == 4) {
		return cmd_preview(argv[2], argv[3]);
	}
	if (strcmp(argv[1], "timings") == 0) {
		return cmd_timings();
	}
	if (strcmp(argv[1], "exchange") == 0) {
		char root[512];
		char dir[512];
		desk_root(root, sizeof root);
		snprintf(dir, sizeof dir, "%s/exchange", root);
		mkdir_p(dir);
		printf("exchange=%s\n", dir);
		return 0;
	}
	usage();
	return 2;
}
