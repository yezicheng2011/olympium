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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "macbundle.h"
#include "macpkg.h"
#include "sandbox.h"

struct review {
	char file[512];
	char name[128];
	char guest[32];
	char kind[48];
	char detail[240];
	char message[480];
	char program[512];
	char launch_type[40];
	char engine[32];
	char id[80];
	int runtime;
	int plan_ok;
	int hides;
	int native_mode;
	int host_window;
	int launchable;
	int cache_hit;
	const char *action;
	/* Filled for a Mac disk image, package, or application folder. */
	int mac_apps;
	char mac_title[160];
	char mac_icon[1024];
	char mac_first[128];
};

enum { REC_MAX = 256 };

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

static const char *mobile_runtime(int openharmony) {
	const char *env = getenv(openharmony ? "OLYMPIUM_OPENHARMONY_RUNTIME" : "OLYMPIUM_ANDROID_RUNTIME");
	if (env != NULL && env[0] == '/' && access(env, X_OK) == 0) {
		return env;
	}
	const char *installed = openharmony ? "/usr/libexec/dev-desktop/olympium-openharmony"
		: "/usr/libexec/dev-desktop/olympium-android";
	return access(installed, X_OK) == 0 ? installed : NULL;
}

static const char *android_runtime(void) {
	return mobile_runtime(0);
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

static int real_pe(const char *path);

/* What this desktop can tell about a file by itself, for an image that does
 * not carry the Olympium runtime. Debian and RPM packages and Windows
 * programs are unpacked and started by this desktop and by Wine, so nothing
 * about them needs that runtime's answer. */
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
	if (n >= 4 && mag[0] == 0xed && mag[1] == 0xab && mag[2] == 0xee && mag[3] == 0xdb) {
		snprintf(r->guest, sizeof r->guest, "linux");
		snprintf(r->kind, sizeof r->kind, "linux_rpm");
		snprintf(r->detail, sizeof r->detail, "RPM package.");
		return 0;
	}
	if (n >= 2 && mag[0] == 'M' && mag[1] == 'Z' && real_pe(path)) {
		snprintf(r->guest, sizeof r->guest, "windows");
		snprintf(r->kind, sizeof r->kind, "windows_exe");
		snprintf(r->detail, sizeof r->detail, "Windows program.");
		snprintf(r->engine, sizeof r->engine, "wine");
		snprintf(r->launch_type, sizeof r->launch_type, "process");
		snprintf(r->program, sizeof r->program, "/usr/bin/wine");
		r->hides = 1;
		r->plan_ok = 1;
		if (access(r->program, X_OK) != 0) {
			snprintf(r->message, sizeof r->message,
				"Windows program. Wine is not installed on this system, so it cannot be started here.");
		}
		return 0;
	}
	return 0;
}

static void apply_runtime(struct review *r) {
	const char *bin = runtime_bin();
	if (bin == NULL) {
		r->runtime = 0;
		local_kind(r->file, r);
		if (r->message[0] == '\0') {
			snprintf(r->message, sizeof r->message,
				"The Olympium runtime is not on this image, so this desktop read the file itself.");
		}
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
	json_string(text, "engine_id", r->engine, sizeof r->engine);
	json_string(text, "summary", r->message, sizeof r->message);
	r->host_window = strstr(text, "\"mode\": \"host_window\"") != NULL;
	if (!r->plan_ok) {
		json_string(text, "message", r->message, sizeof r->message);
	}
}

static int real_pe(const char *path) {
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return 0;
	}
	unsigned char hdr[64];
	if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr || hdr[0] != 'M' || hdr[1] != 'Z') {
		fclose(f);
		return 0;
	}
	unsigned long lfanew = (unsigned long)hdr[0x3c] | ((unsigned long)hdr[0x3d] << 8) |
		((unsigned long)hdr[0x3e] << 16) | ((unsigned long)hdr[0x3f] << 24);
	if (lfanew < 64 || lfanew > 1024 * 1024) {
		fclose(f);
		return 0;
	}
	if (fseek(f, (long)lfanew, SEEK_SET) != 0) {
		fclose(f);
		return 0;
	}
	unsigned char pe[4];
	int ok = fread(pe, 1, 4, f) == 4 && pe[0] == 'P' && pe[1] == 'E' && pe[2] == 0 && pe[3] == 0;
	fclose(f);
	return ok;
}

/* The CPU a PE file was built for, from its COFF header. 0 when unreadable. */
static unsigned pe_machine(const char *path) {
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return 0;
	}
	unsigned char hdr[64];
	unsigned machine = 0;
	if (fread(hdr, 1, sizeof hdr, f) == sizeof hdr && hdr[0] == 'M' && hdr[1] == 'Z') {
		unsigned long lfanew = (unsigned long)hdr[0x3c] | ((unsigned long)hdr[0x3d] << 8) |
			((unsigned long)hdr[0x3e] << 16) | ((unsigned long)hdr[0x3f] << 24);
		unsigned char pe[6];
		if (lfanew >= 64 && lfanew <= 1024 * 1024 && fseek(f, (long)lfanew, SEEK_SET) == 0 &&
				fread(pe, 1, 6, f) == 6 && pe[0] == 'P' && pe[1] == 'E') {
			machine = (unsigned)pe[4] | ((unsigned)pe[5] << 8);
		}
	}
	fclose(f);
	return machine;
}

static const char *pe_machine_name(unsigned machine) {
	switch (machine) {
	case 0x014c: return "32-bit x86";
	case 0x8664: return "64-bit x86";
	case 0xaa64: return "64-bit ARM";
	case 0x01c0:
	case 0x01c4: return "32-bit ARM";
	default: return "an unknown CPU";
	}
}

/* Whether the Wine on this system can start a program built for that CPU.
 * Wine is not an emulator: it runs the program's own instructions. Starting
 * one this system cannot run does not fail cleanly. Wine prints an error to a
 * log nobody sees, opens no window, and exits 0. So the answer is decided
 * here, before anything is started. OLYMPIUM_HOST_MACHINE is for the test. */
static int wine_can_run(unsigned machine, char *why, size_t n) {
	const char *host = getenv("OLYMPIUM_HOST_MACHINE");
	struct utsname u;
	if (host == NULL || host[0] == '\0') {
		host = uname(&u) == 0 ? u.machine : "";
	}
	int host_x64 = strcmp(host, "x86_64") == 0;
	int host_x86 = host[0] == 'i' && strcmp(host + 2, "86") == 0;
	int host_arm64 = strcmp(host, "aarch64") == 0 || strcmp(host, "arm64") == 0;
	const char *what = pe_machine_name(machine);
	if (machine == 0x8664) {
		if (host_x64) {
			return 1;
		}
		snprintf(why, n, host_arm64
			? "Windows program for %s. This is an ARM computer, and this image has no x86 translator for Windows programs, so it was not started."
			: "Windows program for %s. This system is 32-bit and cannot run it.", what);
		return 0;
	}
	if (machine == 0x014c) {
		if (host_x86) {
			return 1;
		}
		if (host_x64) {
			const char *forced = getenv("OLYMPIUM_WINE32");
			if (forced != NULL ? strcmp(forced, "1") == 0 :
					(access("/usr/lib/wine/wine", X_OK) == 0 ||
					access("/usr/lib/i386-linux-gnu/wine/wine", X_OK) == 0)) {
				return 1;
			}
			snprintf(why, n, "Windows program for %s. The 32-bit half of Wine (wine32) is not installed on this image, so it was not started.", what);
			return 0;
		}
		snprintf(why, n, "Windows program for %s. This is an ARM computer, and this image has no x86 translator for Windows programs, so it was not started.", what);
		return 0;
	}
	if (machine == 0xaa64) {
		if (host_arm64) {
			return 1;
		}
		snprintf(why, n, "Windows program for %s. This computer is not ARM, so it was not started.", what);
		return 0;
	}
	snprintf(why, n, "Windows program for %s. Wine on this system cannot run it.", what);
	return 0;
}

/* Where the helper that starts a Mac program is, and whether it can. The
 * helper is a script, so the one place that knows how the Mac runtime is
 * called can be changed without rebuilding this program. OLYMPIUM_MAC_RUNTIME
 * names another helper, which is how the test stands in for the runtime. */
static int mac_runtime(char *out, size_t n) {
	const char *env = getenv("OLYMPIUM_MAC_RUNTIME");
	snprintf(out, n, "%s", env != NULL && env[0] != '\0' ? env
		: "/usr/libexec/dev-desktop/olympium-mac-run");
	if (access(out, X_OK) != 0) {
		return 0;
	}
	pid_t pid = fork();
	if (pid < 0) {
		return 0;
	}
	if (pid == 0) {
		int nul = open("/dev/null", O_RDWR);
		if (nul >= 0) {
			dup2(nul, 0);
			dup2(nul, 1);
			dup2(nul, 2);
		}
		execl(out, out, "--check", (char *)NULL);
		_exit(127);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

static void cache_file(const char *path, char *out, size_t n);

/* A Mac disk image, installer package, or application folder. What is in it
 * is read here. Nothing in it is run, and nothing is copied until Install. */
static int review_mac(struct review *r) {
	const char *kind = mac_detect(r->file);
	if (kind == NULL) {
		return 0;
	}
	char cache[600];
	char work[640];
	cache_file(r->file, cache, sizeof cache);
	snprintf(work, sizeof work, "%s.mac", cache);
	struct mac_review m;
	if (mac_review_file(r->file, work, &m) != 0) {
		return 0;
	}
	snprintf(r->guest, sizeof r->guest, "macos");
	snprintf(r->kind, sizeof r->kind, "%s", kind);
	snprintf(r->engine, sizeof r->engine, "mac");
	r->runtime = runtime_bin() != NULL;
	r->mac_apps = m.app_count;
	snprintf(r->mac_title, sizeof r->mac_title, "%s", m.title);
	const char *what = strcmp(kind, "mac_dmg") == 0 ? "Mac disk image"
		: strcmp(kind, "mac_pkg") == 0 ? "Mac installer package" : "Mac application";
	snprintf(r->detail, sizeof r->detail, "%s", what);
	if (m.app_count == 0) {
		snprintf(r->message, sizeof r->message, "%s. %s", what, m.note);
		return 1;
	}
	snprintf(r->mac_icon, sizeof r->mac_icon, "%s", m.apps[0].icon);
	snprintf(r->mac_first, sizeof r->mac_first, "%s", m.apps[0].name);
	char names[240] = "";
	for (int i = 0; i < m.app_count; i++) {
		size_t used = strlen(names);
		snprintf(names + used, sizeof names - used, "%s%.60s%s%.20s", i > 0 ? ", " : "",
			m.apps[i].name, m.apps[i].version[0] != '\0' ? " " : "", m.apps[i].version);
	}
	char runtime[512];
	const char *opens = mac_runtime(runtime, sizeof runtime)
		? "It opens through the Mac runtime on this system."
		: "Opening it needs the Darling runtime, which is not on this system.";
	if (strcmp(kind, "mac_app") == 0) {
		snprintf(r->message, sizeof r->message,
			"Mac application %s%s%s%s. Install copies it into Applications on this desktop. "
			"It is not run. %s", names, m.apps[0].archs[0] != '\0' ? ", built for " : "",
			m.apps[0].archs, "", opens);
	} else {
		snprintf(r->message, sizeof r->message,
			"%s. It holds %d application%s: %s. Install copies %s into Applications on this "
			"desktop. Nothing in it is run%s. %s", what, m.app_count, m.app_count == 1 ? "" : "s",
			names, m.app_count == 1 ? "it" : "them",
			strcmp(kind, "mac_pkg") == 0 ? ", and the package's installer scripts are not used" : "",
			opens);
	}
	return 1;
}

static void decide(struct review *r) {
	r->launchable = 0;
	r->action = "refuse";
	if (strcmp(r->kind, "android_apk") == 0 && strcmp(r->engine, "android") == 0) {
		r->action = r->plan_ok ? "android" : "review";
		return;
	}
	if (strcmp(r->kind, "openharmony_hap") == 0 && strcmp(r->engine, "openharmony") == 0) {
		r->action = r->plan_ok ? "openharmony" : "review";
		return;
	}
	if (strncmp(r->kind, "mac_", 4) == 0 && strcmp(r->engine, "mac") == 0) {
		/* Install is offered when there is an application in it. Launch is
		 * decided later, for each application, once it has been unpacked. */
		r->action = r->mac_apps > 0 ? "mac" : "review";
		return;
	}
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
	if (strcmp(r->kind, "linux_deb") == 0) {
		char pkg[80];
		char text[160];
		char *argv[] = {"/usr/bin/dpkg-deb", "-f", r->file, "Package", NULL};
		if (capture(argv, text, sizeof text) == 0) {
			char *nl = strchr(text, '\n');
			if (nl != NULL) {
				*nl = '\0';
			}
			snprintf(pkg, sizeof pkg, "%s", text);
			if (pkg[0] != '\0') {
				r->action = "install";
				snprintf(r->message, sizeof r->message,
					"Debian package %s. Import extracts it for this user and does not run it. "
					"Launch opens its window. Missing dependencies are named and not downloaded.",
					pkg);
				return;
			}
		}
	}
	if (strcmp(r->kind, "windows_exe") == 0 && real_pe(r->file) && r->plan_ok && r->hides &&
			strcmp(r->engine, "wine") == 0 &&
			strcmp(r->launch_type, "process") == 0 &&
			r->program[0] != '\0' && access(r->program, X_OK) == 0) {
		char why[320];
		if (!wine_can_run(pe_machine(r->file), why, sizeof why)) {
			r->action = "review";
			snprintf(r->message, sizeof r->message, "%s", why);
			return;
		}
		r->action = "wine";
		r->launchable = 1;
		snprintf(r->message, sizeof r->message,
			"Windows program. Import does not run it. Launch starts it with Wine, "
			"in its own window on this desktop. Wine is not a Windows license, "
			"and no Windows image is downloaded.");
		return;
	}
	if (strcmp(r->kind, "linux_rpm") == 0 && access("/usr/bin/rpm", X_OK) == 0 &&
			access("/usr/bin/rpm2cpio", X_OK) == 0) {
		char name[80];
		char *argv[] = {"/usr/bin/rpm", "-qp", "--queryformat", "%{NAME}", r->file, NULL};
		if (capture(argv, name, sizeof name) == 0 && name[0] != '\0') {
			char *nl = strchr(name, '\n');
			if (nl != NULL) {
				*nl = '\0';
			}
			r->action = "rpm";
			snprintf(r->message, sizeof r->message,
				"RPM package %s. Import extracts it with rpm2cpio for this user and does not run scripts. "
				"Launch opens the program. Nothing is downloaded.",
				name);
			return;
		}
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
	line[strcspn(line, "\r\n")] = '\0';
	char engine[32] = "";
	int got = sscanf(line, "%31[^\t]\t%47[^\t]\t%239[^\t]\t%479[^\t]\t%511[^\t]\t%39[^\t]\t%d\t%d\t%d\t%31[^\t]",
			r->guest, r->kind, r->detail, r->message, r->program, r->launch_type,
			&r->plan_ok, &r->hides, &r->native_mode, engine);
	if (got < 9) {
		return 0;
	}
	if (got >= 10) {
		snprintf(r->engine, sizeof r->engine, "%s", engine);
		undash(r->engine);
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
	fprintf(f, "%s\t%s\t%s\t%s\t%s\t%s\t%d\t%d\t%d\t%s\n",
		or_dash(r->guest), or_dash(r->kind), or_dash(r->detail), or_dash(r->message),
		or_dash(r->program), or_dash(r->launch_type),
		r->plan_ok, r->hides, r->native_mode, or_dash(r->engine));
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
	/* realpath needs room for PATH_MAX. r->file is shorter, and glibc aborts
	 * the program when it is handed a buffer that small. */
	char resolved[PATH_MAX];
	if (realpath(path, resolved) != NULL && strlen(resolved) < sizeof r->file) {
		snprintf(r->file, sizeof r->file, "%s", resolved);
	} else {
		snprintf(r->file, sizeof r->file, "%s", path);
	}
	base_name(r->file, r->name, sizeof r->name);
	const char *extension = strrchr(r->file, '.');
	int openharmony = extension != NULL && strcasecmp(extension, ".hap") == 0;
	if (openharmony || (extension != NULL && strcasecmp(extension, ".apk") == 0)) {
		snprintf(r->guest, sizeof r->guest, "%s", openharmony ? "openharmony" : "android");
		snprintf(r->kind, sizeof r->kind, "%s", openharmony ? "openharmony_hap" : "android_apk");
		snprintf(r->engine, sizeof r->engine, "%s", r->guest);
		snprintf(r->detail, sizeof r->detail, "%s application", openharmony ? "OpenHarmony" : "Android");
		const char *helper = mobile_runtime(openharmony);
		if (helper != NULL) {
			char root[512], text[4096];
			desk_root(root, sizeof root);
			char *args[] = {(char *)helper, "inspect", r->file, "--root", root, NULL};
			if (capture(args, text, sizeof text) == 0 && json_true(text, "ok")) {
				json_string(text, "name", r->name, sizeof r->name);
				json_string(text, "package", r->program, sizeof r->program);
				json_string(text, "id", r->id, sizeof r->id);
				json_string(text, "icon", r->mac_icon, sizeof r->mac_icon);
				snprintf(r->mac_first, sizeof r->mac_first, "%s", r->name);
				r->plan_ok = 1;
				snprintf(r->message, sizeof r->message,
					"Install adds this app to %s and Launchpad. Open starts its own desktop window.",
					openharmony ? "the configured OpenHarmony SDK" : "Android");
				if (!openharmony && json_true(text, "needs_runtime_download")) {
					snprintf(r->message, sizeof r->message,
						"Android support needs a one-time download (about 1.4 GB). "
						"Install sets it up and adds this app to Launchpad.");
				}
			} else {
				json_string(text, "message", r->message, sizeof r->message);
			}
		} else {
			snprintf(r->message, sizeof r->message, "The %s runtime is not installed.", openharmony ? "OpenHarmony" : "Android");
		}
		decide(r);
		return 0;
	}
	/* Mac files are read here and are not sent to the Olympium runtime. Its
	 * answer for them is a refusal, and that refusal is left as it is. */
	if (review_mac(r)) {
		decide(r);
		return 0;
	}
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
	char program[512];
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
		int got = sscanf(line,
			"%79[^\t]\t%127[^\t]\t%511[^\t]\t%79[^\t]\t%31[^\t]\t%47[^\t]\t%15[^\t]\t%31[^\t]\t%511[^\n]",
			r->id, r->name, r->path, r->sha, r->guest, r->kind, r->launch, r->ext, r->program);
		if (got >= 7) {
			if (strcmp(r->program, "-") == 0) {
				r->program[0] = '\0';
			}
			if (strcmp(r->ext, "-") == 0) {
				r->ext[0] = '\0';
			}
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
		fprintf(f, "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
			recs[i].id, recs[i].name, recs[i].path, recs[i].sha,
			recs[i].guest, recs[i].kind, recs[i].launch,
			recs[i].ext[0] ? recs[i].ext : "-",
			recs[i].program[0] ? recs[i].program : "-");
	}
	fclose(f);
	return 0;
}

/* Writes the wrapper and the desktop entry Launchpad reads. icon is a picture
 * file for the entry, or NULL. pin adds the program to the dock. */
static int write_launcher_for(const struct rec *rec, const char *icon, int pin) {
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
	char *quoted_root = g_shell_quote(root);
	fprintf(f, "export OLYMPIUM_DESK_HOME=%s\n", quoted_root);
	g_free(quoted_root);
	const char *bin = runtime_bin();
	if (bin != NULL) {
		char *quoted_bin = g_shell_quote(bin);
		fprintf(f, "export OLYMPIUM_BIN=%s\n", quoted_bin);
		g_free(quoted_bin);
	}
	char *quoted_self = g_shell_quote(self), *quoted_id = g_shell_quote(rec->id);
	fprintf(f, "exec %s launch %s \"$@\"\n", quoted_self, quoted_id);
	g_free(quoted_self);
	g_free(quoted_id);
	fclose(f);
	chmod(wrap, 0755);
	apps_dir(apps, sizeof apps);
	mkdir_p(apps);
	snprintf(desk, sizeof desk, "%s/olympium-%s.desktop", apps, rec->id);
	f = fopen(desk, "w");
	if (f == NULL) {
		return -1;
	}
	fprintf(f, "[Desktop Entry]\nType=Application\nName=%s\nExec=/bin/sh \"", rec->name);
	/* Use a fixed interpreter: GIO checks the first executable before
	 * expanding %% field codes in a path. Pass our wrapper as its argument.
	 * Quote that path, then encode the desktop-entry string layer.
	 * Percent signs are field codes even inside a quoted Exec argument. */
	for (const char *p = wrap; *p; p++) {
		if (*p == '\\' || *p == '"' || *p == '$' || *p == '`') fputs("\\\\", f);
		if (*p == '\\') fputs("\\\\", f);
		else if (*p == '%') fputs("%%", f);
		else fputc(*p, f);
	}
	fputs("\"\n", f);
	fprintf(f, "Terminal=false\nNoDisplay=false\n");
	if (icon != NULL && icon[0] != '\0') {
		fprintf(f, "Icon=%s\n", icon);
	}
	if (rec->ext[0] != '\0') {
		fprintf(f, "MimeType=application/x-olympium-%s;\n", rec->ext);
	}
	fclose(f);
	char pin_path[512];
	snprintf(pin_path, sizeof pin_path, "%s/pinned", root);
	if (pin) {
		/* Once. Importing the same program again does not pin it twice. */
		char want[900];
		char line[900];
		int already = 0;
		snprintf(want, sizeof want, "%s\t%s\n", rec->name, wrap);
		FILE *have = fopen(pin_path, "r");
		if (have != NULL) {
			while (fgets(line, sizeof line, have) != NULL) {
				if (strcmp(line, want) == 0) {
					already = 1;
					break;
				}
			}
			fclose(have);
		}
		FILE *pins = already ? NULL : fopen(pin_path, "a");
		if (pins != NULL) {
			fputs(want, pins);
			fclose(pins);
		}
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
	if (pin) {
		printf("pinned=%s\n", pin_path);
	}
	return 0;
}

static int write_launcher(const struct rec *rec) {
	return write_launcher_for(rec, NULL, 1);
}

static int run_wait(char *const argv[]) {
	pid_t pid = fork();
	if (pid < 0) {
		return -1;
	}
	if (pid == 0) {
		execv(argv[0], argv);
		_exit(127);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}

static int dep_installed(const char *pkg) {
	char text[160];
	char *argv[] = {"/usr/bin/dpkg-query", "-W", "-f", "${Status}", (char *)pkg, NULL};
	if (capture(argv, text, sizeof text) != 0) {
		return 0;
	}
	return strstr(text, "install ok installed") != NULL;
}

static void desktop_key(FILE *f, const char *key, char *out, size_t n) {
	rewind(f);
	char line[512];
	size_t klen = strlen(key);
	out[0] = '\0';
	while (fgets(line, sizeof line, f) != NULL) {
		if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
			line[strcspn(line, "\r\n")] = '\0';
			snprintf(out, n, "%s", line + klen + 1);
			return;
		}
	}
}

/* Extract a .deb into this user's directory. Maintainer scripts are not run. */
static int install_deb(const char *deb, const char *id, struct rec *rec) {
	char depends[512];
	char *dep_argv[] = {"/usr/bin/dpkg-deb", "-f", (char *)deb, "Depends", NULL};
	depends[0] = '\0';
	capture(dep_argv, depends, sizeof depends);
	char missing[240] = "";
	char *cursor = depends;
	while (*cursor) {
		while (*cursor == ' ' || *cursor == ',' || *cursor == '\n') {
			cursor++;
		}
		if (*cursor == '\0') {
			break;
		}
		char name[80];
		size_t n = 0;
		while (*cursor && *cursor != ',' && *cursor != ' ' && *cursor != '(' && *cursor != '|' && *cursor != '\n' && n + 1 < sizeof name) {
			name[n++] = *cursor++;
		}
		name[n] = '\0';
		while (*cursor && *cursor != ',') {
			cursor++;
		}
		if (name[0] == '\0') {
			continue;
		}
		if (!dep_installed(name)) {
			if (missing[0] != '\0') {
				strncat(missing, " ", sizeof missing - strlen(missing) - 1);
			}
			strncat(missing, name, sizeof missing - strlen(missing) - 1);
		}
	}
	if (missing[0] != '\0') {
		fprintf(stderr, "dev-install: missing dependencies: %s\n", missing);
		fprintf(stderr, "dev-install: dependencies were not downloaded\n");
		return 3;
	}
	char root[512];
	char dest[512];
	desk_root(root, sizeof root);
	snprintf(dest, sizeof dest, "%s/runtimes/linux/%s/root", root, id);
	char parent[512];
	snprintf(parent, sizeof parent, "%s/runtimes/linux/%s", root, id);
	mkdir_p(parent);
	char *rm_argv[] = {"/bin/rm", "-rf", dest, NULL};
	run_wait(rm_argv);
	mkdir_p(dest);
	/* The package is whatever its author made it. dpkg-deb, and the tar it
	 * starts, unpack it confined: they can read the package and write only
	 * under dest. What comes out then loses its set-ID bits, and any device
	 * or pipe in it is removed. */
	char *x_argv[] = {"/usr/bin/dpkg-deb", "-x", (char *)deb, dest, NULL};
	int unpacked = dev_confined_run(x_argv, deb, dest);
	if (unpacked != 0) {
		fprintf(stderr, unpacked == 125
			? "dev-install: the unpacker could not be confined; the package was not extracted\n"
			: "dev-install: could not extract the package\n");
		char *undo[] = {"/bin/rm", "-rf", dest, NULL};
		run_wait(undo);
		return 1;
	}
	dev_tree_settle(dest);
	printf("confinement=%s\n", dev_confine_name(dev_confine_probe()));
	printf("extracted=%s\n", dest);
	char apps_src[512];
	snprintf(apps_src, sizeof apps_src, "%s/usr/share/applications", dest);
	DIR *dir = opendir(apps_src);
	char primary_exec[512] = "";
	char primary_name[128] = "";
	char primary_ext[32] = "";
	char primary_desktop[256] = "";
	int primary_match = 0;
	if (dir != NULL) {
		struct dirent *ent;
		while ((ent = readdir(dir)) != NULL) {
			if (!strstr(ent->d_name, ".desktop")) {
				continue;
			}
			char dpath[512];
			snprintf(dpath, sizeof dpath, "%s/%s", apps_src, ent->d_name);
			FILE *df = fopen(dpath, "r");
			if (df == NULL) {
				continue;
			}
			char name[128];
			char execkey[240];
			char ext[32];
			desktop_key(df, "Name", name, sizeof name);
			desktop_key(df, "Exec", execkey, sizeof execkey);
			desktop_key(df, "X-Olympium-Extension", ext, sizeof ext);
			fclose(df);
			execkey[strcspn(execkey, " \t")] = '\0';
			if (execkey[0] != '/') {
				continue;
			}
			char full[512];
			snprintf(full, sizeof full, "%s%s", dest, execkey);
			if (access(full, X_OK) != 0) {
				continue;
			}
			const char *base = strrchr(full, '/');
			base = base != NULL ? base + 1 : full;
			size_t blen = strlen(base);
			int matches = blen > 0 && strncmp(id, base, blen) == 0 && id[blen] == '-';
			if (primary_exec[0] != '\0' && !(matches && !primary_match)) {
				char apps[512];
				char out[512];
				apps_dir(apps, sizeof apps);
				mkdir_p(apps);
				snprintf(out, sizeof out, "%s/%s", apps, ent->d_name);
				FILE *copy = fopen(out, "w");
				if (copy != NULL) {
					fprintf(copy, "[Desktop Entry]\nType=Application\nName=%s\nExec=%s\nTerminal=false\n",
						name[0] ? name : ent->d_name, full);
					fclose(copy);
					printf("extra_desktop=%s\n", out);
				}
				continue;
			}
			if (primary_exec[0] != '\0') {
				char apps[512];
				char out[512];
				apps_dir(apps, sizeof apps);
				mkdir_p(apps);
				snprintf(out, sizeof out, "%s/%s", apps, primary_desktop);
				FILE *copy = fopen(out, "w");
				if (copy != NULL) {
					fprintf(copy, "[Desktop Entry]\nType=Application\nName=%s\nExec=%s\nTerminal=false\n",
						primary_name, primary_exec);
					fclose(copy);
					printf("extra_desktop=%s\n", out);
				}
			}
			snprintf(primary_exec, sizeof primary_exec, "%s", full);
			snprintf(primary_name, sizeof primary_name, "%s", name[0] ? name : id);
			snprintf(primary_ext, sizeof primary_ext, "%s", ext);
			snprintf(primary_desktop, sizeof primary_desktop, "%s", ent->d_name);
			primary_match = matches;
		}
		closedir(dir);
	}
	if (primary_exec[0] == '\0') {
		fprintf(stderr, "dev-install: the package has no executable desktop entry\n");
		return 1;
	}
	snprintf(rec->program, sizeof rec->program, "%s", primary_exec);
	snprintf(rec->name, sizeof rec->name, "%s", primary_name);
	if (primary_ext[0] != '\0') {
		snprintf(rec->ext, sizeof rec->ext, "%s", primary_ext);
	}
	printf("program=%s\n", rec->program);
	return 0;
}

static int install_rpm(const char *file, const char *id, struct rec *rec) {
	char root[512];
	char dest[640];
	desk_root(root, sizeof root);
	snprintf(dest, sizeof dest, "%s/runtimes/linux/%s/root", root, id);
	char *rm[] = {"/bin/rm", "-rf", dest, NULL};
	run_wait(rm);
	mkdir_p(dest);
	/* rpm2cpio reads the package and cpio writes the files, each confined:
	 * the first can write nowhere, the second only under dest. */
	char *producer[] = {"/usr/bin/rpm2cpio", (char *)file, NULL};
	char *consumer[] = {"/usr/bin/cpio", "-idm", "--quiet", "--no-absolute-filenames",
		"--no-preserve-owner", NULL};
	int unpacked = dev_confined_pipe(producer, file, consumer, dest);
	if (unpacked != 0) {
		fprintf(stderr, unpacked == 125
			? "dev-install: the unpacker could not be confined; the package was not extracted\n"
			: "dev-install: rpm2cpio extract failed\n");
		char *undo[] = {"/bin/rm", "-rf", dest, NULL};
		run_wait(undo);
		return 1;
	}
	dev_tree_settle(dest);
	printf("confinement=%s\n", dev_confine_name(dev_confine_probe()));
	char bin[640];
	snprintf(bin, sizeof bin, "%s/usr/bin", dest);
	DIR *dir = opendir(bin);
	char found[640] = "";
	if (dir != NULL) {
		struct dirent *ent;
		while ((ent = readdir(dir)) != NULL) {
			if (ent->d_name[0] == '.') {
				continue;
			}
			snprintf(found, sizeof found, "%s/%s", bin, ent->d_name);
			if (access(found, X_OK) == 0) {
				break;
			}
			found[0] = '\0';
		}
		closedir(dir);
	}
	if (found[0] == '\0') {
		fprintf(stderr, "dev-install: the rpm has no executable under usr/bin\n");
		return 1;
	}
	snprintf(rec->program, sizeof rec->program, "%s", found);
	printf("program=%s\n", rec->program);
	printf("extracted=%s\n", dest);
	return 0;
}

static int runtime_up(const char *sockpath) {
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		return 0;
	}
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof addr.sun_path, "%s", sockpath);
	if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
		close(fd);
		return 0;
	}
	ssize_t wrote = write(fd, "ping\n", 5);
	char buf[16];
	ssize_t got = read(fd, buf, sizeof buf);
	close(fd);
	return wrote > 0 && got > 0;
}

static void runtime_daemon(const char *sockpath) {
	signal(SIGPIPE, SIG_IGN);
	setsid();
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		_exit(1);
	}
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof addr.sun_path, "%s", sockpath);
	unlink(sockpath);
	if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
		_exit(1);
	}
	listen(fd, 4);
	for (;;) {
		int client = accept(fd, NULL, NULL);
		if (client < 0) {
			continue;
		}
		char buf[32];
		ssize_t ignore = read(client, buf, sizeof buf);
		(void)ignore;
		ssize_t wrote = write(client, "ready\n", 6);
		(void)wrote;
		close(client);
	}
}

static long millis_between(const struct timespec *a, const struct timespec *b);

static long mono_ms(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int run_quiet(char *const argv[]) {
	pid_t pid = fork();
	if (pid < 0) {
		return -1;
	}
	if (pid == 0) {
		int nul = open("/dev/null", O_RDWR);
		if (nul >= 0) {
			dup2(nul, 0);
			dup2(nul, 1);
			dup2(nul, 2);
		}
		execv(argv[0], argv);
		_exit(127);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* The X display Wine draws through. DISPLAY, when it is set. Otherwise the
 * name the compositor wrote down once Xwayland was ready: a program can be
 * started in the first seconds of a session, before that, and its
 * environment then has no DISPLAY in it. So this waits for the name, up to
 * OLYMPIUM_XWAIT seconds (20 unless set), and gives up at once when the
 * compositor wrote that the session has no X display. */
static int env_seconds(const char *name, int fallback);

static int find_x_display(char *out, size_t n) {
	const char *env = getenv("DISPLAY");
	if (env != NULL && env[0] != '\0') {
		snprintf(out, n, "%s", env);
		return 1;
	}
	const char *run = getenv("XDG_RUNTIME_DIR");
	char path[600];
	snprintf(path, sizeof path, "%s/olympium-xdisplay",
		run != NULL && run[0] != '\0' ? run : "/run/dev-desktop");
	long wait_ms = (long)env_seconds("OLYMPIUM_XWAIT", 20) * 1000;
	long start = mono_ms();
	for (;;) {
		FILE *f = fopen(path, "r");
		if (f != NULL) {
			char line[64] = "";
			if (fgets(line, sizeof line, f) == NULL) {
				line[0] = '\0';
			}
			fclose(f);
			line[strcspn(line, "\r\n")] = '\0';
			if (strcmp(line, "none") == 0) {
				return 0;
			}
			if (line[0] == ':' && line[1] != '\0') {
				snprintf(out, n, "%s", line);
				return 1;
			}
		}
		if (mono_ms() - start >= wait_ms) {
			return 0;
		}
		usleep(100000);
	}
}

/* The Wine prefix: "wine" in the desk directory, or OLYMPIUM_WINE_PREFIX for
 * someone who keeps theirs elsewhere. The boot test uses the second to time
 * a prefix Wine has to build, beside the one the image already carries. */
static void wine_prefix_path(char *out, size_t n) {
	const char *env = getenv("OLYMPIUM_WINE_PREFIX");
	if (env != NULL && env[0] == '/') {
		snprintf(out, n, "%s", env);
		return;
	}
	char root[512];
	desk_root(root, sizeof root);
	snprintf(out, n, "%s/wine", root);
}

static int dir_is_empty(const char *path) {
	DIR *d = opendir(path);
	if (d == NULL) {
		return 1;
	}
	struct dirent *e;
	int empty = 1;
	while ((e = readdir(d)) != NULL) {
		if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
			empty = 0;
			break;
		}
	}
	closedir(d);
	return empty;
}

/* Makes the Wine prefix usable before Wine starts. A prefix that Wine has to
 * build itself is about 690 MB of copied DLLs and was most of the first-launch
 * wait. When the image carries a prefix that was built at image time, it is
 * copied in whole, into a sibling directory first and then renamed, so an
 * interrupted copy never leaves a half prefix behind. Returns "existing",
 * "seeded", or "fresh" (Wine builds it on this launch). */
static const char *wine_prepare(const char *prefix, long *ms) {
	long start = mono_ms();
	char probe[768];
	*ms = 0;
	snprintf(probe, sizeof probe, "%s/system.reg", prefix);
	if (access(probe, F_OK) == 0) {
		return "existing";
	}
	const char *tpl = getenv("OLYMPIUM_WINE_TEMPLATE");
	if (tpl == NULL || tpl[0] == '\0') {
		tpl = "/usr/lib/olympium/wine-prefix";
	}
	snprintf(probe, sizeof probe, "%s/system.reg", tpl);
	if (access(probe, F_OK) != 0 || (access(prefix, F_OK) == 0 && !dir_is_empty(prefix))) {
		/* No template, or a directory with someone's files in it and no
		 * registry. Wine decides what to do with that; nothing is removed. */
		mkdir_p(prefix);
		return "fresh";
	}
	char tmp[768];
	char src[768];
	snprintf(tmp, sizeof tmp, "%s.seed-%ld", prefix, (long)getpid());
	snprintf(src, sizeof src, "%s/.", tpl);
	char parent[768];
	snprintf(parent, sizeof parent, "%s", prefix);
	char *slash = strrchr(parent, '/');
	if (slash != NULL) {
		*slash = '\0';
		mkdir_p(parent);
	}
	mkdir(tmp, 0755);
	char *cp[] = {"/bin/cp", "-a", "--reflink=auto", src, tmp, NULL};
	int rc = run_quiet(cp);
	if (rc == 0) {
		rmdir(prefix);
		if (rename(tmp, prefix) == 0) {
			*ms = mono_ms() - start;
			return "seeded";
		}
	}
	char *rm[] = {"/bin/rm", "-rf", tmp, NULL};
	run_quiet(rm);
	mkdir_p(prefix);
	*ms = mono_ms() - start;
	return "fresh";
}

/* The environment every Wine process of ours is started with. TMPDIR is
 * removed: with it set, even to an empty string, Ubuntu's Wine 9.0 aborts
 * with "free(): invalid pointer" or a segmentation fault in its 32-bit loader
 * before the program's window opens, every time (seen with
 * 9.0~repack-4build3). Many systems export TMPDIR, so this is not a test
 * artefact. Wine then uses /tmp, which is what it does without it. */
static void wine_environment(const char *prefix) {
	setenv("WINEPREFIX", prefix, 1);
	setenv("WINEDLLOVERRIDES", "mscoree,mshtml=", 1);
	unsetenv("TMPDIR");
}

/* Asks wineserver to stay for a while after the last Windows program exits,
 * so the next launch reuses it, and to exit by itself after that. 0 keeps
 * Wine's own default (about three seconds). Nothing runs before the first
 * launch unless prewarm is on. */
static void wine_keep_server(int seconds) {
	if (seconds <= 0 || access("/usr/bin/wineserver", X_OK) != 0) {
		return;
	}
	char opt[32];
	snprintf(opt, sizeof opt, "-p%d", seconds);
	char *argv[] = {"/usr/bin/wineserver", opt, NULL};
	run_quiet(argv);
}

static int env_seconds(const char *name, int fallback) {
	const char *v = getenv(name);
	if (v == NULL || v[0] == '\0') {
		return fallback;
	}
	int n = atoi(v);
	return n < 0 ? 0 : (n > 86400 ? 86400 : n);
}

static void prewarm_flag(char *out, size_t n) {
	char root[512];
	desk_root(root, sizeof root);
	snprintf(out, n, "%s/prewarm", root);
}

/* Prewarm is off unless the user turned it on. "run" is what the session
 * calls at start; it does nothing while prewarm is off. */
static int cmd_prewarm(const char *what) {
	char flag[640];
	prewarm_flag(flag, sizeof flag);
	if (strcmp(what, "on") == 0) {
		char root[512];
		desk_root(root, sizeof root);
		mkdir_p(root);
		FILE *f = fopen(flag, "w");
		if (f == NULL) {
			perror(flag);
			return 1;
		}
		fputs("on\n", f);
		fclose(f);
		printf("prewarm on\n");
		return 0;
	}
	if (strcmp(what, "off") == 0) {
		unlink(flag);
		char *kill_argv[] = {"/usr/bin/wineserver", "-k", NULL};
		char prefix[640];
		wine_prefix_path(prefix, sizeof prefix);
		wine_environment(prefix);
		if (access("/usr/bin/wineserver", X_OK) == 0) {
			run_quiet(kill_argv);
		}
		printf("prewarm off\n");
		return 0;
	}
	int on = access(flag, F_OK) == 0;
	if (strcmp(what, "status") == 0) {
		printf("prewarm %s\n", on ? "on" : "off");
		return 0;
	}
	if (strcmp(what, "run") != 0) {
		return 2;
	}
	if (!on) {
		printf("prewarm off; nothing started\n");
		return 0;
	}
	if (access("/usr/bin/wine", X_OK) != 0) {
		printf("prewarm: Wine is not installed; nothing started\n");
		return 0;
	}
	char prefix[640];
	long ms = 0;
	wine_prefix_path(prefix, sizeof prefix);
	const char *state = wine_prepare(prefix, &ms);
	wine_environment(prefix);
	long a = mono_ms();
	wine_keep_server(env_seconds("OLYMPIUM_WINE_PREWARM_IDLE", 600));
	/* wineserver alone saves little. Most of a cold start is Wine's own
	 * services, so start them once with a program that exits at once. */
	char *warm[] = {"/usr/bin/wine", "cmd", "/c", "exit", NULL};
	run_quiet(warm);
	printf("prewarm wine_prefix %s\n", state);
	printf("prewarm wine_prefix_ms %ld\n", ms);
	printf("prewarm wine_server_ms %ld\n", mono_ms() - a);
	return 0;
}

static void ensure_runtime(void) {
	char root[512];
	char sock[512];
	desk_root(root, sizeof root);
	mkdir_p(root);
	snprintf(sock, sizeof sock, "%s/linux-runtime.sock", root);
	struct timespec a, b;
	clock_gettime(CLOCK_MONOTONIC, &a);
	if (runtime_up(sock)) {
		printf("runtime_state warm\n");
		printf("runtime_ms 0\n");
		fflush(stdout);
		return;
	}
	pid_t pid = fork();
	if (pid == 0) {
		runtime_daemon(sock);
		_exit(0);
	}
	for (int i = 0; i < 50 && !runtime_up(sock); i++) {
		usleep(20000);
	}
	clock_gettime(CLOCK_MONOTONIC, &b);
	printf("runtime_state %s\n", runtime_up(sock) ? "cold" : "down");
	printf("runtime_ms %ld\n", millis_between(&a, &b));
	fflush(stdout);
}

static void mac_progress(int step, int of, const char *what) {
	printf("progress %d/%d %s\n", step, of, what);
	fflush(stdout);
}

/* One library row, one wrapper and one Launchpad entry for each application
 * that came out of the image or package. Nothing is added to the dock: an
 * application that was just installed is found in Launchpad, as on a Mac. */
static int import_mac(const struct review *r) {
	char root[512];
	desk_root(root, sizeof root);
	mkdir_p(root);
	static struct mac_app_info apps[MAC_APPS_MAX];
	char err[400];
	printf("confinement=%s\n", dev_confine_name(dev_confine_probe()));
	int count = mac_install_file(r->file, root, apps, MAC_APPS_MAX, err, sizeof err, mac_progress);
	if (count < 0) {
		fprintf(stderr, "dev-install: not installed: %s\n", err);
		printf("download: none\n");
		return 1;
	}
	static struct rec recs[REC_MAX];
	int n = load_recs(recs, REC_MAX);
	for (int i = 0; i < count; i++) {
		char sha[80] = "";
		if (file_sha256(apps[i].program, sha, sizeof sha) != 0) {
			fprintf(stderr, "dev-install: could not checksum %s\n", apps[i].name);
			return 1;
		}
		int slot = n;
		for (int k = 0; k < n; k++) {
			if (strcmp(recs[k].path, apps[i].bundle) == 0) {
				slot = k;
				break;
			}
		}
		if (slot == n) {
			if (n >= REC_MAX) {
				fprintf(stderr, "dev-install: library is full\n");
				return 1;
			}
			n++;
		}
		struct rec *rec = &recs[slot];
		memset(rec, 0, sizeof *rec);
		/* Named after where it is installed, not after its program. Two
		 * applications can have the same program bytes, and each still
		 * needs its own entry, wrapper and icon. */
		unsigned long where = 5381;
		for (const unsigned char *p = (const unsigned char *)apps[i].bundle; *p != '\0'; p++) {
			where = where * 33 + *p;
		}
		snprintf(rec->id, sizeof rec->id, "mac-%08lx", where & 0xFFFFFFFFul);
		snprintf(rec->name, sizeof rec->name, "%s", apps[i].name);
		for (char *p = rec->name; *p != '\0'; p++) {
			if (*p == '\t') {
				*p = ' ';
			}
		}
		snprintf(rec->path, sizeof rec->path, "%s", apps[i].bundle);
		snprintf(rec->sha, sizeof rec->sha, "%s", sha);
		snprintf(rec->guest, sizeof rec->guest, "macos");
		snprintf(rec->kind, sizeof rec->kind, "mac_app");
		snprintf(rec->launch, sizeof rec->launch, "mac");
		snprintf(rec->program, sizeof rec->program, "%s", apps[i].program);
		if (save_recs(recs, n) != 0) {
			return 1;
		}
		printf("id=%s\n", rec->id);
		printf("app=%s\n", rec->name);
		printf("version=%s\n", apps[i].version);
		printf("installed=%s\n", rec->path);
		printf("archs=%s\n", apps[i].archs[0] != '\0' ? apps[i].archs : "none");
		printf("runs_on_this_cpu=%s\n", apps[i].runs_on_this_cpu ? "yes" : "no");
		printf("icon=%s\n", apps[i].icon[0] != '\0' ? apps[i].icon : "none");
		printf("launch=mac\n");
		if (write_launcher_for(rec, apps[i].icon, 0) != 0) {
			return 1;
		}
	}
	printf("installed_count=%d\n", count);
	printf("scripts_run=0\n");
	printf("download: none\n");
	return 0;
}

static int cmd_import_mode(const char *path, const char *ext, int consent, int update_only) {
	struct review r;
	fill_review(path, &r);
	if (update_only && strcmp(r.action, "android") != 0) {
		fprintf(stderr, "dev-install: managed update is currently available for Android APKs\n");
		return 3;
	}
	int openharmony = strcmp(r.action, "openharmony") == 0;
	if (openharmony || strcmp(r.action, "android") == 0) {
		if (!consent) {
			fprintf(stderr, "dev-install: refused to install without consent\n");
			return 2;
		}
		const char *helper = mobile_runtime(openharmony);
		char root[512], text[4096] = "";
		desk_root(root, sizeof root);
		struct rec recs[REC_MAX];
		int n = load_recs(recs, REC_MAX), slot = n;
		for (int i = 0; i < n; i++) {
			if (strcmp(recs[i].id, r.id) == 0) { slot = i; break; }
		}
		if (slot == n && n >= REC_MAX) {
			fprintf(stderr, "dev-install: library is full\n");
			return 1;
		}
		printf("progress 1/3 read\n");
		printf("progress 2/3 install\n");
		fflush(stdout);
		char *args[] = {(char *)helper, update_only ? "update" : "install", r.file, "--root", root, NULL};
		if (helper == NULL || capture(args, text, sizeof text) != 0 || !json_true(text, "ok")) {
			char reason[1024] = "";
			json_string(text, "message", reason, sizeof reason);
			fprintf(stderr, "dev-install: not installed: %s\n",
				reason[0] ? reason : "Application installation failed");
			return 3;
		}
		struct rec rec = {0};
		char icon[1024] = "";
		json_string(text, "id", rec.id, sizeof rec.id);
		json_string(text, "name", rec.name, sizeof rec.name);
		json_string(text, "path", rec.path, sizeof rec.path);
		json_string(text, "sha", rec.sha, sizeof rec.sha);
		json_string(text, "package", rec.program, sizeof rec.program);
		json_string(text, "icon", icon, sizeof icon);
		snprintf(rec.guest, sizeof rec.guest, "%s", r.guest);
		snprintf(rec.kind, sizeof rec.kind, "%s", r.kind);
		snprintf(rec.launch, sizeof rec.launch, "%s", r.action);
		recs[slot] = rec;
		if (slot == n) n++;
		if (save_recs(recs, n) != 0 || write_launcher_for(&rec, icon, 0) != 0) return 1;
		printf("id=%s\nlaunch=%s\nprogress 3/3 done\n", rec.id, rec.launch);
		return 0;
	}
	if (strcmp(r.action, "mac") == 0) {
		if (!consent) {
			fprintf(stderr, "dev-install: refused to install without consent\n");
			print_review(&r);
			return 2;
		}
		return import_mac(&r);
	}
	printf("progress 1/4 classify\n");
	printf("progress 2/4 plan\n");
	if (!consent) {
		fprintf(stderr, "dev-install: refused to import without consent\n");
		print_review(&r);
		return 2;
	}
	if (strcmp(r.action, "import") != 0 && strcmp(r.action, "keep") != 0 &&
			strcmp(r.action, "install") != 0 && strcmp(r.action, "rpm") != 0 &&
			strcmp(r.action, "wine") != 0) {
		fprintf(stderr, "dev-install: not installed: %s\n", r.message);
		print_review(&r);
		return 3;
	}
	char sha[80] = "";
	if (file_sha256(r.file, sha, sizeof sha) != 0) {
		fprintf(stderr, "dev-install: could not checksum the file\n");
		return 1;
	}
	printf("progress 3/4 %s\n",
		(strcmp(r.action, "install") == 0 || strcmp(r.action, "rpm") == 0) ? "extract" : "checksum");
	const char *bin = runtime_bin();
	char id[80];
	snprintf(id, sizeof id, "local-%.12s", sha);
	if (strcmp(r.action, "install") == 0) {
		char pkg[64];
		char text[80];
		char *parg[] = {"/usr/bin/dpkg-deb", "-f", r.file, "Package", NULL};
		text[0] = '\0';
		capture(parg, text, sizeof text);
		char *nl = strchr(text, '\n');
		if (nl != NULL) {
			*nl = '\0';
		}
		size_t j = 0;
		for (const char *p = text; *p && j + 1 < sizeof pkg; p++) {
			char c = *p;
			if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
				pkg[j++] = c;
			} else if (c >= 'A' && c <= 'Z') {
				pkg[j++] = (char)(c - 'A' + 'a');
			}
		}
		pkg[j] = '\0';
		if (pkg[0] == '\0') {
			snprintf(pkg, sizeof pkg, "package");
		}
		snprintf(id, sizeof id, "%s-%.8s", pkg, sha);
		printf("runtime_add=not-called\n");
	} else if (strcmp(r.action, "rpm") == 0) {
		char name[80];
		char *q[] = {"/usr/bin/rpm", "-qp", "--queryformat", "%{NAME}", r.file, NULL};
		name[0] = '\0';
		capture(q, name, sizeof name);
		char *nl = strchr(name, '\n');
		if (nl != NULL) {
			*nl = '\0';
		}
		char pkg[64];
		size_t j = 0;
		for (const char *p = name; *p && j + 1 < sizeof pkg; p++) {
			char c = *p;
			if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
				pkg[j++] = c;
			} else if (c >= 'A' && c <= 'Z') {
				pkg[j++] = (char)(c - 'A' + 'a');
			}
		}
		pkg[j] = '\0';
		if (pkg[0] == '\0') {
			snprintf(pkg, sizeof pkg, "rpm");
		}
		snprintf(id, sizeof id, "%s-%.8s", pkg, sha);
		printf("runtime_add=not-called\n");
	} else if (strcmp(r.action, "wine") == 0) {
		snprintf(id, sizeof id, "wine-%.8s", sha);
		printf("runtime_add=not-called\n");
		printf("engine=wine\n");
	} else if (bin != NULL && (strcmp(r.action, "import") == 0 || strcmp(r.action, "keep") == 0)) {
		char text[4096];
		char *argv[] = {(char *)bin, "--json", "add", "--file", r.file, "--name", r.name, NULL};
		capture(argv, text, sizeof text);
		if (!json_true(text, "ok")) {
			fprintf(stderr, "dev-install: the runtime did not accept the file\n");
			return 1;
		}
		json_string(text, "id", id, sizeof id);
	}
	struct rec recs[REC_MAX];
	int n = load_recs(recs, REC_MAX);
	int slot = n;
	for (int i = 0; i < n; i++) {
		if (strcmp(recs[i].path, r.file) == 0) {
			slot = i;
			break;
		}
	}
	if (slot == n) {
		if (n >= REC_MAX) {
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
	if (strcmp(r.action, "install") == 0) {
		int code = install_deb(r.file, rec->id, rec);
		if (code != 0) {
			return code;
		}
		rec->launch[0] = '\0';
		snprintf(rec->launch, sizeof rec->launch, "native");
	} else if (strcmp(r.action, "rpm") == 0) {
		int code = install_rpm(r.file, rec->id, rec);
		if (code != 0) {
			return code;
		}
		rec->launch[0] = '\0';
		snprintf(rec->launch, sizeof rec->launch, "native");
	} else if (strcmp(r.action, "wine") == 0) {
		snprintf(rec->program, sizeof rec->program, "%s", r.program);
		rec->launch[0] = '\0';
		snprintf(rec->launch, sizeof rec->launch, "wine");
	}
	if (save_recs(recs, n) != 0) {
		return 1;
	}
	printf("id=%s\n", rec->id);
	printf("launch=%s\n", rec->launch);
	if (r.launchable || strcmp(r.action, "install") == 0 || strcmp(r.action, "rpm") == 0 ||
			strcmp(r.action, "wine") == 0) {
		if (write_launcher(rec) != 0) {
			return 1;
		}
	}
	printf("progress 4/4 desktop\n");
	printf("download: none\n");
	return 0;
}

static int cmd_import(const char *path, const char *ext, int consent) {
	return cmd_import_mode(path, ext, consent, 0);
}

static int cmd_rollback(const char *id, int consent) {
	if (!consent) {
		fprintf(stderr, "dev-install: rollback requires consent\n");
		return 2;
	}
	static struct rec recs[REC_MAX];
	int n = load_recs(recs, REC_MAX), at = -1;
	for (int i = 0; i < n; i++) {
		if (strcmp(recs[i].id, id) == 0) { at = i; break; }
	}
	if (at < 0 || strcmp(recs[at].launch, "android") != 0) {
		fprintf(stderr, "dev-install: no managed Android app with this ID\n");
		return 3;
	}
	char root[512], text[4096] = "", path[1024] = "";
	desk_root(root, sizeof root);
	const char *helper = android_runtime();
	char *args[] = {(char *)helper, "rollback", recs[at].program, "--root", root, NULL};
	if (helper == NULL || capture(args, text, sizeof text) != 0 || !json_true(text, "ok")) {
		char reason[1024] = "";
		json_string(text, "message", reason, sizeof reason);
		fprintf(stderr, "dev-install: rollback failed: %s\n", reason[0] ? reason : "Android rollback failed");
		return 3;
	}
	json_string(text, "path", path, sizeof path);
	if (path[0] == '\0') return 3;
	/* The backend retains reviewed APKs and treats an identical import as a
	 * no-op. Reuse import to refresh the existing library row, icon and wrapper. */
	return cmd_import(path, NULL, 1);
}

/* Tells the person why nothing opened. A launch from Launchpad or the dock
 * has no terminal, so without this a refusal would look like nothing
 * happening at all. OLYMPIUM_NO_DIALOG keeps a script from waiting on it. */
static void show_notice(const char *title, const char *text, const char *icon) {
	fprintf(stderr, "dev-install: %s\n", text);
	if (getenv("OLYMPIUM_NO_DIALOG") != NULL ||
			(getenv("WAYLAND_DISPLAY") == NULL && getenv("DISPLAY") == NULL)) {
		return;
	}
	if (!gtk_init_check(NULL, NULL)) {
		return;
	}
	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), title);
	gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
	gtk_window_set_default_size(GTK_WINDOW(win), 440, 180);
	gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	gtk_container_set_border_width(GTK_CONTAINER(row), 20);
	gtk_container_add(GTK_CONTAINER(win), row);
	if (icon != NULL && icon[0] != '\0') {
		GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(icon, 64, 64, TRUE, NULL);
		if (pixbuf != NULL) {
			GtkWidget *image = gtk_image_new_from_pixbuf(pixbuf);
			gtk_widget_set_valign(image, GTK_ALIGN_START);
			gtk_box_pack_start(GTK_BOX(row), image, FALSE, FALSE, 0);
			g_object_unref(pixbuf);
		}
	}
	GtkWidget *column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_box_pack_start(GTK_BOX(row), column, TRUE, TRUE, 0);
	GtkWidget *heading = gtk_label_new(NULL);
	char *markup = g_markup_printf_escaped("<b>%s</b>", title);
	gtk_label_set_markup(GTK_LABEL(heading), markup);
	g_free(markup);
	gtk_label_set_xalign(GTK_LABEL(heading), 0);
	gtk_box_pack_start(GTK_BOX(column), heading, FALSE, FALSE, 0);
	GtkWidget *body = gtk_label_new(text);
	gtk_label_set_xalign(GTK_LABEL(body), 0);
	gtk_label_set_line_wrap(GTK_LABEL(body), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(body), 46);
	gtk_box_pack_start(GTK_BOX(column), body, FALSE, FALSE, 0);
	GtkWidget *ok = gtk_button_new_with_label("OK");
	gtk_widget_set_halign(ok, GTK_ALIGN_END);
	gtk_box_pack_end(GTK_BOX(column), ok, FALSE, FALSE, 0);
	g_signal_connect_swapped(ok, "clicked", G_CALLBACK(gtk_widget_destroy), win);
	g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	gtk_widget_show_all(win);
	gtk_main();
}

/* The picture written for this application when it was installed. */
static void mac_icon_of(const struct rec *rec, char *out, size_t n) {
	char apps[512];
	char desk[700];
	out[0] = '\0';
	apps_dir(apps, sizeof apps);
	snprintf(desk, sizeof desk, "%s/olympium-%s.desktop", apps, rec->id);
	FILE *f = fopen(desk, "r");
	if (f == NULL) {
		return;
	}
	char line[1100];
	while (fgets(line, sizeof line, f) != NULL) {
		if (strncmp(line, "Icon=", 5) == 0) {
			line[strcspn(line, "\r\n")] = '\0';
			snprintf(out, n, "%s", line + 5);
			break;
		}
	}
	fclose(f);
}

/* Starts a Mac application through the Mac runtime, when the application has
 * a program for this computer's CPU and a runtime is installed. This desktop
 * does not load Mach-O programs itself, and it never starts one any other
 * way. Each refusal is said in a window, not only on standard error. */
static int launch_mac(const struct rec *rec, int argc, char **argv) {
	char icon[1024];
	char text[640];
	mac_icon_of(rec, icon, sizeof icon);
	struct mac_bundle b;
	if (mac_bundle_read(rec->path, &b) != 0 || b.program_kind != MAC_PROGRAM_MACHO) {
		snprintf(text, sizeof text,
			"%s was not opened. Its program is not a Mac program this desktop can read.", rec->name);
		show_notice(rec->name, text, icon);
		return 3;
	}
	if (!mac_host_can_run(b.archs, b.arch_count)) {
		char archs[64];
		mac_archs_text(b.archs, b.arch_count, archs, sizeof archs);
		snprintf(text, sizeof text,
			"%s was not opened. It is built for %s, and this computer is %s. "
			"A program runs only on the kind of processor it was built for.",
			rec->name, archs, mac_host_arch_name());
		show_notice(rec->name, text, icon);
		return 3;
	}
	char runtime[512];
	if (!mac_runtime(runtime, sizeof runtime)) {
		snprintf(text, sizeof text,
			"%s was not opened. A Mac application runs here through the Darling runtime, "
			"and Darling is not installed on this system. The application stays in Applications.",
			rec->name);
		show_notice(rec->name, text, icon);
		return 3;
	}
	printf("engine mac\n");
	printf("mac_runtime %s\n", runtime);
	fflush(stdout);
	char *args[64];
	int k = 0;
	args[k++] = runtime;
	args[k++] = (char *)rec->path;
	args[k++] = (char *)rec->program;
	for (int i = 0; i < argc && k < 62; i++) {
		args[k++] = argv[i];
	}
	args[k] = NULL;
	int status = run_wait(args);
	if (status != 0) {
		snprintf(text, sizeof text,
			"%s could not run with the installed Mac runtime (exit %d). "
			"The application stays in Applications.", rec->name, status);
		show_notice(rec->name, text, icon);
	}
	return status < 0 ? 127 : status;
}

/* Takes an installed Mac application away again: the application itself, its
 * row in the library, its wrapper, its Launchpad entry and its icon. Only an
 * application this program installed under its own Applications folder. */
static int cmd_remove(const char *id) {
	static struct rec recs[REC_MAX];
	int n = load_recs(recs, REC_MAX);
	int at = -1;
	for (int i = 0; i < n; i++) {
		if (strcmp(recs[i].id, id) == 0) {
			at = i;
			break;
		}
	}
	if (at < 0) {
		fprintf(stderr, "dev-install: unknown id %s\n", id);
		return 1;
	}
	int openharmony = strcmp(recs[at].launch, "openharmony") == 0;
	int android = strcmp(recs[at].launch, "android") == 0;
	if (!openharmony && !android && strcmp(recs[at].launch, "mac") != 0) {
		fprintf(stderr, "dev-install: %s is not a Mac application; it was not removed\n", recs[at].name);
		return 3;
	}
	char root[512];
	char apps_root[640];
	desk_root(root, sizeof root);
	if (android || openharmony) {
		const char *helper = mobile_runtime(openharmony);
		char *args[] = {(char *)helper, "remove", recs[at].program, "--root", root, NULL};
		if (helper == NULL || run_wait(args) != 0) return 3;
	} else {
	snprintf(apps_root, sizeof apps_root, "%s/mac/Applications/", root);
	size_t prefix = strlen(apps_root);
	if (strncmp(recs[at].path, apps_root, prefix) != 0 || strchr(recs[at].path + prefix, '/') != NULL ||
			strstr(recs[at].path, "/../") != NULL || recs[at].path[prefix] == '\0') {
		/* The row was edited to point somewhere else. Nothing is deleted. */
		fprintf(stderr, "dev-install: %s is not under %s; it was not removed\n", recs[at].path, apps_root);
		return 3;
	}
	char *rm[] = {"/bin/rm", "-rf", "--", recs[at].path, NULL};
	if (run_quiet(rm) != 0) {
		fprintf(stderr, "dev-install: %s could not be removed\n", recs[at].path);
		return 1;
	}
	}
	char icon[1024];
	mac_icon_of(&recs[at], icon, sizeof icon);
	char path[900];
	char apps[512];
	apps_dir(apps, sizeof apps);
	snprintf(path, sizeof path, "%s/olympium-%s.desktop", apps, recs[at].id);
	unlink(path);
	snprintf(path, sizeof path, "%s/launch/%s", root, recs[at].id);
	unlink(path);
	char icons_root[640];
	snprintf(icons_root, sizeof icons_root, "%s/icons/", root);
	if (icon[0] != '\0' && strncmp(icon, icons_root, strlen(icons_root)) == 0) {
		unlink(icon);
	}
	printf("removed=%s\n", recs[at].path);
	for (int i = at; i + 1 < n; i++) {
		recs[i] = recs[i + 1];
	}
	return save_recs(recs, n - 1) == 0 ? 0 : 1;
}

static int cmd_launch(const char *id, int argc, char **argv) {
	struct rec recs[REC_MAX];
	int n = load_recs(recs, REC_MAX);
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
	int is_mac = strcmp(rec->launch, "mac") == 0;
	char sha[80];
	/* A Mac application is a folder. Its program is what was checksummed. */
	if (file_sha256(is_mac ? rec->program : rec->path, sha, sizeof sha) != 0 ||
			strcmp(sha, rec->sha) != 0) {
		fprintf(stderr, "dev-install: checksum changed; not started\n");
		return 4;
	}
	if (is_mac) {
		return launch_mac(rec, argc, argv);
	}
	int openharmony = strcmp(rec->launch, "openharmony") == 0;
	if (openharmony || strcmp(rec->launch, "android") == 0) {
		const char *helper = mobile_runtime(openharmony);
		if (helper == NULL) {
			show_notice(rec->name, openharmony ? "The OpenHarmony runtime is not installed." : "The Android runtime is not installed.", NULL);
			return 3;
		}
		char root[512];
		desk_root(root, sizeof root);
		char *args[] = {(char *)helper, "launch", rec->program, "--root", root, NULL};
		char *oh_args[] = {(char *)helper, "launch", rec->program, "--root", root, "--title", rec->name, NULL};
		int status = run_wait(openharmony ? oh_args : args);
		if (status != 0) show_notice(rec->name, openharmony ? "OpenHarmony could not open this app. Check the SDK session."
			: "Android could not open this app. Check the Android runtime and try again.", NULL);
		return status;
	}
	if (strcmp(rec->launch, "wine") == 0) {
		struct review again;
		fill_review(rec->path, &again);
		if (strcmp(again.action, "wine") != 0 || again.program[0] == '\0') {
			fprintf(stderr, "dev-install: %s was not started. %s\n", rec->name,
				again.message[0] != '\0' ? again.message : "Wine is not available.");
			return 3;
		}
		char xdisplay[64];
		if (!find_x_display(xdisplay, sizeof xdisplay)) {
			/* Wine draws through X. Without Xwayland it would start, fail to
			 * create its window, and take the program down with it. */
			fprintf(stderr, "dev-install: %s was not started. This desktop has no X display "
				"for Wine (Xwayland is not running).\n", rec->name);
			return 3;
		}
		setenv("DISPLAY", xdisplay, 1);
		char prefix[640];
		long prefix_ms = 0;
		long t0 = mono_ms();
		wine_prefix_path(prefix, sizeof prefix);
		const char *state = wine_prepare(prefix, &prefix_ms);
		wine_environment(prefix);
		long t1 = mono_ms();
		wine_keep_server(env_seconds("OLYMPIUM_WINE_IDLE", 60));
		long t2 = mono_ms();
		/* The phases of one launch. launch_start_ms is CLOCK_MONOTONIC, the
		 * same clock as /proc/uptime, so a test can subtract it from the
		 * moment the compositor reports the window. */
		printf("engine wine\n");
		printf("wine_prefix %s\n", state);
		printf("wine_prefix_ms %ld\n", prefix_ms);
		printf("wine_server_ms %ld\n", t2 - t1);
		printf("launch_start_ms %ld\n", t0);
		fflush(stdout);
		char mark[700];
		char root[512];
		desk_root(root, sizeof root);
		snprintf(mark, sizeof mark, "%s/wine-launch-marks", root);
		FILE *mf = fopen(mark, "w");
		if (mf != NULL) {
			fprintf(mf, "launch_start_ms %ld\nwine_prefix %s\nwine_prefix_ms %ld\nwine_server_ms %ld\n",
				t0, state, prefix_ms, t2 - t1);
			fclose(mf);
		}
		/* The folder Linux programs share is drive S: for Windows programs. */
		char shared[640];
		char drive[700];
		snprintf(shared, sizeof shared, "%s/exchange", root);
		mkdir_p(shared);
		snprintf(drive, sizeof drive, "%s/dosdevices/s:", prefix);
		/* Only in a prefix Wine has finished building. A directory made
		 * here first would stop Wine from building the rest. A prefix that
		 * is built on this launch gets the drive on the next one. */
		if (strcmp(state, "fresh") != 0 && access(drive, F_OK) != 0) {
			if (symlink(shared, drive) != 0 && errno != EEXIST) {
				perror(drive);
			}
		}
		char *args[16];
		static char winpaths[14][600];
		int k = 0;
		args[k++] = again.program;
		args[k++] = rec->path;
		for (int i = 0; i < argc && k < 14; i++) {
			/* A document handed over by Files is a Linux path. A Windows
			 * program is given the same file under drive Z:. */
			if (argv[i][0] == '/' && access(argv[i], F_OK) == 0 &&
					strlen(argv[i]) + 3 < sizeof winpaths[0]) {
				char *w = winpaths[k];
				snprintf(w, sizeof winpaths[0], "Z:%s", argv[i]);
				for (char *c = w; *c != '\0'; c++) {
					if (*c == '/') {
						*c = '\\';
					}
				}
				args[k++] = w;
				continue;
			}
			args[k++] = argv[i];
		}
		args[k] = NULL;
		execv(args[0], args);
		perror(args[0]);
		return 127;
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
	const char *target = rec->program[0] != '\0' ? rec->program : rec->path;
	int elf = 0;
	FILE *f = fopen(target, "rb");
	if (f != NULL) {
		unsigned char mag[4];
		if (fread(mag, 1, 4, f) == 4 && mag[0] == 0x7f && mag[1] == 'E' && mag[2] == 'L' && mag[3] == 'F') {
			elf = 1;
		}
		fclose(f);
	}
	ensure_runtime();
	if (elf && access(runner, X_OK) == 0) {
		args[k++] = (char *)runner;
	}
	args[k++] = (char *)target;
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
static GtkWidget *g_bar;
static GtkWidget *g_check;
static char g_ext[32];

/* The install of a Mac file runs as a second copy of this program, so the
 * window keeps answering while a large image unpacks, shows how far it is,
 * and can stop it. */
static GPid g_child;
static int g_child_running;
static int g_child_stopped;
static char g_installed_id[80];
static char g_child_error[400];

static int is_mac_review(void) {
	return strcmp(g_review.action, "mac") == 0;
}

static int is_async_review(void) {
	return is_mac_review() || strcmp(g_review.action, "android") == 0 ||
		strcmp(g_review.action, "openharmony") == 0;
}

static void on_consent(GtkToggleButton *button, gpointer data) {
	(void)data;
	int on = gtk_toggle_button_get_active(button);
	int allow = on && (strcmp(g_review.action, "import") == 0 ||
		strcmp(g_review.action, "keep") == 0 || strcmp(g_review.action, "install") == 0 ||
		strcmp(g_review.action, "rpm") == 0 || strcmp(g_review.action, "wine") == 0 ||
		is_async_review());
	gtk_widget_set_sensitive(g_import, g_child_running ? is_mac_review() : allow);
}

static gboolean on_child_line(GIOChannel *channel, GIOCondition condition, gpointer data) {
	int is_error = GPOINTER_TO_INT(data);
	if (condition & G_IO_IN) {
		gchar *line = NULL;
		while (g_io_channel_read_line(channel, &line, NULL, NULL, NULL) == G_IO_STATUS_NORMAL) {
			g_strchomp(line);
			int step = 0;
			int of = 0;
			char what[32] = "";
			if (is_error) {
				const char *text = strstr(line, "not installed: ");
				snprintf(g_child_error, sizeof g_child_error, "%s", text != NULL ? text + 15 : line);
			} else if (sscanf(line, "progress %d/%d %31s", &step, &of, what) == 3 && of > 0) {
				gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_bar), (double)step / of);
				const char *label = strcmp(what, "read") == 0 ? "Reading"
					: strcmp(what, "unpack") == 0 ? "Unpacking"
					: strcmp(what, "copy") == 0 ? "Copying"
					: strcmp(what, "check") == 0 ? "Checking"
					: strcmp(what, "install") == 0 ? "Moving into Applications" : "Done";
				gtk_progress_bar_set_text(GTK_PROGRESS_BAR(g_bar), label);
			} else if (strncmp(line, "id=", 3) == 0 && g_installed_id[0] == '\0') {
				snprintf(g_installed_id, sizeof g_installed_id, "%s", line + 3);
			}
			g_free(line);
			line = NULL;
		}
		g_free(line);
	}
	if (condition & (G_IO_HUP | G_IO_ERR)) {
		return FALSE;
	}
	return TRUE;
}

static void on_child_done(GPid pid, gint status, gpointer data) {
	(void)data;
	g_spawn_close_pid(pid);
	g_child_running = 0;
	gtk_button_set_label(GTK_BUTTON(g_import), "Install");
	gtk_widget_set_sensitive(g_check, TRUE);
	int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (g_child_stopped) {
		/* The unpacker was stopped. What it had staged is removed here, so
		 * nothing half installed is left behind. */
		char root[512];
		desk_root(root, sizeof root);
		mac_clear_stale(root);
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_bar), 0);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(g_bar), "Stopped");
		gtk_label_set_text(GTK_LABEL(g_status), "Stopped. Nothing was installed.");
	} else if (ok) {
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_bar), 1);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(g_bar), "Installed");
		gtk_label_set_text(GTK_LABEL(g_status),
			is_mac_review() ? "Installed in Applications. It is in Launchpad. Open starts it."
			: "Installed. It is in Launchpad. Open starts it.");
		gtk_widget_set_sensitive(g_launch, g_installed_id[0] != '\0');
	} else {
		char text[480];
		snprintf(text, sizeof text, "Not installed%s%s", g_child_error[0] != '\0' ? ": " : ".",
			g_child_error);
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_bar), 0);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(g_bar), "Not installed");
		gtk_label_set_text(GTK_LABEL(g_status), text);
	}
	on_consent(GTK_TOGGLE_BUTTON(g_check), NULL);
}

static void child_own_group(gpointer data) {
	(void)data;
	/* Its own group, so stopping it also stops the unpacker it started. */
	setpgid(0, 0);
}

static void start_async_install(void) {
	char self[512];
	ssize_t len = readlink("/proc/self/exe", self, sizeof self - 1);
	if (len < 0) {
		snprintf(self, sizeof self, "/usr/bin/dev-install");
	} else {
		self[len] = '\0';
	}
	gchar *argv[] = {self, "import", "--consent", g_review.file, NULL};
	gint out_fd = -1;
	gint err_fd = -1;
	GError *error = NULL;
	g_installed_id[0] = '\0';
	g_child_error[0] = '\0';
	g_child_stopped = 0;
	if (!g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, child_own_group,
			NULL, &g_child, NULL, &out_fd, &err_fd, &error)) {
		gtk_label_set_text(GTK_LABEL(g_status), "Not installed. The installer could not be started.");
		g_clear_error(&error);
		return;
	}
	g_child_running = 1;
	GIOChannel *out = g_io_channel_unix_new(out_fd);
	GIOChannel *err = g_io_channel_unix_new(err_fd);
	g_io_channel_set_flags(out, g_io_channel_get_flags(out) | G_IO_FLAG_NONBLOCK, NULL);
	g_io_channel_set_flags(err, g_io_channel_get_flags(err) | G_IO_FLAG_NONBLOCK, NULL);
	g_io_channel_set_close_on_unref(out, TRUE);
	g_io_channel_set_close_on_unref(err, TRUE);
	g_io_add_watch(out, G_IO_IN | G_IO_HUP | G_IO_ERR, on_child_line, GINT_TO_POINTER(0));
	g_io_add_watch(err, G_IO_IN | G_IO_HUP | G_IO_ERR, on_child_line, GINT_TO_POINTER(1));
	g_io_channel_unref(out);
	g_io_channel_unref(err);
	g_child_watch_add(g_child, on_child_done, NULL);
	gtk_button_set_label(GTK_BUTTON(g_import), is_mac_review() ? "Stop" : "Installing…");
	gtk_widget_set_sensitive(g_import, is_mac_review());
	gtk_widget_set_sensitive(g_check, FALSE);
	gtk_widget_set_sensitive(g_launch, FALSE);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_bar), 0.02);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(g_bar), "Reading");
	gtk_label_set_text(GTK_LABEL(g_status), is_mac_review()
		? "Installing. Nothing from the file is run." : "Installing in Android. This may take a moment.");
}

static void on_import(GtkButton *button, gpointer data) {
	(void)button;
	(void)data;
	if (is_async_review()) {
		if (g_child_running) {
			if (!is_mac_review()) return;
			g_child_stopped = 1;
			kill(-g_child, SIGTERM);
			return;
		}
		start_async_install();
		return;
	}
	int code = cmd_import(g_review.file, g_ext[0] ? g_ext : NULL, 1);
	if (code == 0 && (g_review.launchable || strcmp(g_review.action, "install") == 0 ||
			strcmp(g_review.action, "rpm") == 0 || strcmp(g_review.action, "wine") == 0)) {
		gtk_label_set_text(GTK_LABEL(g_status), "Installed. Launch opens this program in its own window.");
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
	if (is_async_review()) {
		if (g_installed_id[0] == '\0') {
			gtk_label_set_text(GTK_LABEL(g_status), "Nothing to open.");
			return;
		}
		pid_t pid = fork();
		if (pid == 0) {
			execl("/proc/self/exe", "dev-install", "launch", g_installed_id, (char *)NULL);
			_exit(127);
		}
		gtk_label_set_text(GTK_LABEL(g_status), "Opening.");
		return;
	}
	struct rec recs[REC_MAX];
	int n = load_recs(recs, REC_MAX);
	for (int i = 0; i < n; i++) {
		if (strcmp(recs[i].path, g_review.file) == 0 &&
				(strcmp(recs[i].launch, "native") == 0 || strcmp(recs[i].launch, "wine") == 0)) {
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
	int openharmony = strcmp(r->kind, "openharmony_hap") == 0;
	int mac = strncmp(r->kind, "mac_", 4) == 0 || strcmp(r->kind, "android_apk") == 0 || openharmony;
	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), "Install");
	/* The compositor draws the title bar and the window controls. Without
	 * this GTK draws a second bar of its own underneath. */
	gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
	gtk_window_set_default_size(GTK_WINDOW(win), 560, mac ? 300 : 420);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(box), 18);
	gtk_container_add(GTK_CONTAINER(win), box);
	char line[900];
	if (mac) {
		/* The application's own picture and name first, the way a disk
		 * image shows what is in it when it is opened on a Mac. */
		GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
		GtkWidget *picture = NULL;
		if (r->mac_icon[0] != '\0') {
			GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(r->mac_icon, 96, 96, TRUE, NULL);
			if (pixbuf != NULL) {
				picture = gtk_image_new_from_pixbuf(pixbuf);
				g_object_unref(pixbuf);
			}
		}
		if (picture == NULL) {
			picture = gtk_image_new_from_icon_name("package-x-generic", GTK_ICON_SIZE_DIALOG);
			gtk_image_set_pixel_size(GTK_IMAGE(picture), 96);
		}
		gtk_widget_set_valign(picture, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(head), picture, FALSE, FALSE, 0);
		GtkWidget *column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
		gtk_box_pack_start(GTK_BOX(head), column, TRUE, TRUE, 0);
		GtkWidget *title = gtk_label_new(NULL);
		const char *shown = r->mac_first[0] != '\0' ? r->mac_first
			: r->mac_title[0] != '\0' ? r->mac_title : r->name;
		char *markup = g_markup_printf_escaped("<span size='x-large' weight='bold'>%s</span>", shown);
		gtk_label_set_markup(GTK_LABEL(title), markup);
		g_free(markup);
		gtk_label_set_xalign(GTK_LABEL(title), 0);
		gtk_box_pack_start(GTK_BOX(column), title, FALSE, FALSE, 0);
		snprintf(line, sizeof line, "%s · %s", r->detail, r->name);
		GtkWidget *kind = gtk_label_new(line);
		gtk_label_set_xalign(GTK_LABEL(kind), 0);
		gtk_label_set_ellipsize(GTK_LABEL(kind), PANGO_ELLIPSIZE_MIDDLE);
		gtk_box_pack_start(GTK_BOX(column), kind, FALSE, FALSE, 0);
		/* The line above already says what kind of file this is. */
		const char *text = r->message;
		size_t kind_len = strlen(r->detail);
		if (kind_len > 0 && strncmp(text, r->detail, kind_len) == 0 && text[kind_len] == '.' &&
				text[kind_len + 1] == ' ') {
			text += kind_len + 2;
		}
		GtkWidget *body = gtk_label_new(text);
		gtk_label_set_xalign(GTK_LABEL(body), 0);
		gtk_label_set_line_wrap(GTK_LABEL(body), TRUE);
		gtk_label_set_max_width_chars(GTK_LABEL(body), 52);
		gtk_box_pack_start(GTK_BOX(column), body, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(box), head, FALSE, FALSE, 0);
	} else {
		GtkWidget *title = gtk_label_new(NULL);
		gtk_label_set_markup(GTK_LABEL(title), "<span size='large' weight='bold'>Install</span>");
		gtk_widget_set_halign(title, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);
		snprintf(line, sizeof line, "%s\n%s · %s\n%s", r->name, r->kind[0] ? r->kind : "unknown",
			r->guest[0] ? r->guest : "unknown", r->message);
		GtkWidget *body = gtk_label_new(line);
		gtk_label_set_xalign(GTK_LABEL(body), 0);
		gtk_label_set_line_wrap(GTK_LABEL(body), TRUE);
		gtk_box_pack_start(GTK_BOX(box), body, FALSE, FALSE, 0);
	}
	int can_import = strcmp(r->action, "import") == 0 || strcmp(r->action, "keep") == 0 ||
		strcmp(r->action, "install") == 0 || strcmp(r->action, "rpm") == 0 ||
		strcmp(r->action, "wine") == 0 || strcmp(r->action, "mac") == 0 || strcmp(r->action, "android") == 0 ||
		strcmp(r->action, "openharmony") == 0;
	g_bar = gtk_progress_bar_new();
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_bar), 0);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(g_bar),
		"No download. This desktop does not fetch guest images.");
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(g_bar), TRUE);
	gtk_box_pack_start(GTK_BOX(box), g_bar, FALSE, FALSE, 0);
	if (mac) {
		g_check = gtk_check_button_new_with_label(can_import
			? (strcmp(r->kind, "android_apk") == 0 ? "Install Android support and this app. Add it to Launchpad."
			: openharmony ? "Install in the configured OpenHarmony SDK and add to Launchpad."
			: "Install into Applications. It will not run until it is opened.")
			: "Install stays off. There is no application in this file.");
	} else {
		g_check = gtk_check_button_new_with_label(
			can_import ? "Import this file. It will not run until Launch."
				: "Import stays off. This format has not been installed here.");
	}
	gtk_widget_set_sensitive(g_check, can_import);
	gtk_box_pack_start(GTK_BOX(box), g_check, FALSE, FALSE, 0);
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	g_import = gtk_button_new_with_label(mac ? "Install" : "Import");
	g_launch = gtk_button_new_with_label(mac ? "Open" : "Launch");
	gtk_widget_set_sensitive(g_import, FALSE);
	gtk_widget_set_sensitive(g_launch, FALSE);
	gtk_box_pack_start(GTK_BOX(row), g_import, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), g_launch, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
	g_status = gtk_label_new(can_import
		? "Nothing has been run."
		: "Recognized only. Nothing was installed or run.");
	gtk_widget_set_halign(g_status, GTK_ALIGN_START);
	gtk_label_set_line_wrap(GTK_LABEL(g_status), TRUE);
	gtk_label_set_xalign(GTK_LABEL(g_status), 0);
	gtk_box_pack_start(GTK_BOX(box), g_status, FALSE, FALSE, 0);
	g_signal_connect(g_check, "toggled", G_CALLBACK(on_consent), NULL);
	g_signal_connect(g_import, "clicked", G_CALLBACK(on_import), NULL);
	g_signal_connect(g_launch, "clicked", G_CALLBACK(on_launch), NULL);
	g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	return win;
}

/* OLYMPIUM_INSTALL_AUTO is for tests, which have no pointer to click with.
 * "install" ticks the box and presses Install. "stop" does the same and then
 * presses Stop at once. The window reports what happened and closes. It does
 * nothing that "dev-install import --consent" does not already do. */
static gboolean auto_report(gpointer data) {
	(void)data;
	if (g_child_running) {
		return TRUE;
	}
	printf("auto: %s\n", gtk_label_get_text(GTK_LABEL(g_status)));
	printf("auto_id: %s\n", g_installed_id[0] != '\0' ? g_installed_id : "none");
	fflush(stdout);
	gtk_main_quit();
	return FALSE;
}

static gboolean auto_stop(gpointer data) {
	(void)data;
	if (g_child_running) {
		gtk_button_clicked(GTK_BUTTON(g_import));
	}
	return FALSE;
}

static gboolean auto_press(gpointer data) {
	const char *mode = data;
	if (!is_async_review()) {
		printf("auto: not a Mac file with an application\n");
		gtk_main_quit();
		return FALSE;
	}
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_check), TRUE);
	gtk_button_clicked(GTK_BUTTON(g_import));
	if (strcmp(mode, "stop") == 0) {
		g_timeout_add(atoi(getenv("OLYMPIUM_INSTALL_AUTO_STOP_MS") != NULL
			? getenv("OLYMPIUM_INSTALL_AUTO_STOP_MS") : "30"), auto_stop, NULL);
	}
	g_timeout_add(100, auto_report, NULL);
	return FALSE;
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
	struct rec recs[REC_MAX];
	int n = load_recs(recs, REC_MAX);
	/* An application that is already in Applications is opened, not offered
	 * for install again. That is what a double-click on it means. */
	char resolved[PATH_MAX];
	if (realpath(path, resolved) != NULL) {
		for (int i = 0; i < n; i++) {
			if (strcmp(recs[i].launch, "mac") == 0 && strcmp(recs[i].path, resolved) == 0) {
				return cmd_launch(recs[i].id, 0, NULL);
			}
		}
	}
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
	if (!no_ui && (getenv("DISPLAY") != NULL || getenv("WAYLAND_DISPLAY") != NULL)) {
		g_review = r;
		gtk_init(NULL, NULL);
		GtkWidget *win = build_window(&r);
		gtk_widget_show_all(win);
		const char *automatic = getenv("OLYMPIUM_INSTALL_AUTO");
		if (automatic != NULL && automatic[0] != '\0') {
			g_timeout_add(400, auto_press, (gpointer)automatic);
		}
		gtk_main();
	}
	return 0;
}

static void usage(void) {
	fprintf(stderr,
		"usage: dev-install inspect FILE\n"
		"       dev-install import --consent FILE [--mime EXT]\n"
		"       dev-install update --consent APK\n"
		"       dev-install rollback --consent ID\n"
		"       dev-install launch ID [ARG...]\n"
		"       dev-install remove ID\n"
		"       dev-install open [--no-ui] FILE\n"
		"       dev-install preview FILE PNG\n"
		"       dev-install timings\n"
		"       dev-install prewarm on|off|status|run\n"
		"       dev-install confinement\n"
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
	if (strcmp(argv[1], "import") == 0 || strcmp(argv[1], "update") == 0) {
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
		return cmd_import_mode(file, ext, consent, strcmp(argv[1], "update") == 0);
	}
	if (strcmp(argv[1], "rollback") == 0) {
		int consent = 0;
		const char *id = NULL;
		for (int i = 2; i < argc; i++) {
			if (strcmp(argv[i], "--consent") == 0) consent = 1;
			else if (id == NULL) id = argv[i];
		}
		if (id == NULL) { usage(); return 2; }
		return cmd_rollback(id, consent);
	}
	if (strcmp(argv[1], "launch") == 0 && argc >= 3) {
		return cmd_launch(argv[2], argc - 3, argv + 3);
	}
	if (strcmp(argv[1], "remove") == 0 && argc == 3) {
		return cmd_remove(argv[2]);
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
	if (strcmp(argv[1], "confinement") == 0) {
		/* How an unvetted disk image or package would be unpacked here. */
		printf("%s\n", dev_confine_name(dev_confine_probe()));
		return 0;
	}
	if (strcmp(argv[1], "prewarm") == 0 && argc == 3) {
		int rc = cmd_prewarm(argv[2]);
		if (rc != 2) {
			return rc;
		}
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
