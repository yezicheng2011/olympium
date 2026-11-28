/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Thin C ABI bridge (libolympium-native.so) exposing low-level POSIX/kernel
 * operations (Landlock/nobody confinement, libcrypt account helpers, Mach-O /
 * .dmg / .pkg / .icns native decoders, Cairo dragon mark, compositor control
 * socket) for the VB.NET (.NET 8) Olympium desktop applications and tools.
 */
#define _GNU_SOURCE
#include "accounts.h"
#include "dragon.h"
#include "macbundle.h"
#include "macho.h"
#include "macicon.h"
#include "macpkg.h"
#include "sandbox.h"

#include <cairo.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static void json_append_escaped(char *buf, size_t cap, size_t *pos, const char *s) {
	if (s == NULL) {
		s = "";
	}
	for (; *s != '\0' && *pos + 6 < cap; s++) {
		unsigned char c = (unsigned char)*s;
		if (c == '"' || c == '\\') {
			buf[(*pos)++] = '\\';
			buf[(*pos)++] = (char)c;
		} else if (c == '\n') {
			buf[(*pos)++] = '\\';
			buf[(*pos)++] = 'n';
		} else if (c == '\r') {
			buf[(*pos)++] = '\\';
			buf[(*pos)++] = 'r';
		} else if (c == '\t') {
			buf[(*pos)++] = '\\';
			buf[(*pos)++] = 't';
		} else if (c < 0x20) {
			*pos += (size_t)snprintf(buf + *pos, cap - *pos, "\\u%04x", c);
		} else {
			buf[(*pos)++] = (char)c;
		}
	}
	if (*pos < cap) {
		buf[*pos] = '\0';
	}
}

void olympium_draw_dragon(cairo_t *cr, double x, double y, double size,
		double r, double g, double b) {
	if (cr != NULL) {
		draw_dragon_mark(cr, x, y, size, r, g, b);
	}
}

int olympium_send_compositor_cmd(const char *cmd) {
	if (cmd == NULL || cmd[0] == '\0') {
		return -1;
	}
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		return -1;
	}
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof addr.sun_path, "/run/dev-desktop/control");
	int rc = -1;
	if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0) {
		ssize_t n = send(fd, cmd, strlen(cmd), MSG_NOSIGNAL);
		if (n >= 0) {
			rc = 0;
		}
	}
	close(fd);
	return rc;
}

/* ---- Confinement & package extraction helpers ---- */

int olympium_confine_probe(void) {
	return (int)dev_confine_probe();
}

const char *olympium_confine_name(int how) {
	return dev_confine_name((enum dev_confinement)how);
}

int olympium_confined_run(char *const argv[], const char *read_path, const char *write_dir) {
	return dev_confined_run(argv, read_path, write_dir);
}

int olympium_confined_pipe(char *const producer[], const char *read_path,
		char *const consumer[], const char *write_dir) {
	return dev_confined_pipe(producer, read_path, consumer, write_dir);
}

void olympium_tree_settle(const char *root) {
	dev_tree_settle(root);
}

/* ---- Mach-O / Mac bundle / DMG / PKG helpers ---- */

int olympium_macho_probe(const uint8_t *data, size_t length, char *desc_out, size_t desc_cap) {
	struct dev_macho_info info;
	if (dev_macho_identify(data, length, &info) != 0) {
		if (desc_out != NULL && desc_cap > 0) {
			desc_out[0] = '\0';
		}
		return -1;
	}
	if (desc_out != NULL && desc_cap > 0) {
		snprintf(desc_out, desc_cap, "%s", info.description);
	}
	return 0;
}

const char *olympium_mac_detect(const char *path) {
	return mac_detect(path);
}

int olympium_mac_is_bundle_dir(const char *path) {
	return mac_is_bundle_dir(path);
}

int olympium_mac_bundle_icon_path(const char *bundle_dir, char *out, size_t cap) {
	struct mac_bundle b;
	if (mac_bundle_read(bundle_dir, &b) != 0 || b.icon_path[0] == '\0') {
		if (out != NULL && cap > 0) {
			out[0] = '\0';
		}
		return -1;
	}
	snprintf(out, cap, "%s", b.icon_path);
	return 0;
}

int olympium_mac_icon_render(const char *src_icon, const char *dest_png) {
	return mac_icon_render(src_icon, dest_png);
}

void olympium_mac_clear_stale(const char *desk_root) {
	mac_clear_stale(desk_root);
}

int olympium_mac_bundle_check_launch(const char *bundle_dir, int *is_macho, int *can_run,
		char *archs_out, size_t archs_cap, char *host_out, size_t host_cap) {
	struct mac_bundle b;
	if (mac_bundle_read(bundle_dir, &b) != 0) {
		return -1;
	}
	if (is_macho != NULL) {
		*is_macho = (b.program_kind == MAC_PROGRAM_MACHO) ? 1 : 0;
	}
	if (can_run != NULL) {
		*can_run = mac_host_can_run(b.archs, b.arch_count);
	}
	if (archs_out != NULL && archs_cap > 0) {
		mac_archs_text(b.archs, b.arch_count, archs_out, archs_cap);
	}
	if (host_out != NULL && host_cap > 0) {
		snprintf(host_out, host_cap, "%s", mac_host_arch_name());
	}
	return 0;
}

int olympium_mac_review_json(const char *path, const char *cache_dir, char *json_out, size_t cap) {
	struct mac_review m;
	if (mac_review_file(path, cache_dir, &m) != 0) {
		return -1;
	}
	size_t pos = 0;
	pos += (size_t)snprintf(json_out + pos, cap - pos, "{\"kind\":\"");
	json_append_escaped(json_out, cap, &pos, m.kind);
	pos += (size_t)snprintf(json_out + pos, cap - pos, "\",\"title\":\"");
	json_append_escaped(json_out, cap, &pos, m.title);
	pos += (size_t)snprintf(json_out + pos, cap - pos, "\",\"has_scripts\":%s,\"note\":\"",
		m.has_scripts ? "true" : "false");
	json_append_escaped(json_out, cap, &pos, m.note);
	pos += (size_t)snprintf(json_out + pos, cap - pos, "\",\"apps\":[");
	for (int i = 0; i < m.app_count && pos + 64 < cap; i++) {
		if (i > 0) {
			json_out[pos++] = ',';
		}
		pos += (size_t)snprintf(json_out + pos, cap - pos, "{\"name\":\"");
		json_append_escaped(json_out, cap, &pos, m.apps[i].name);
		pos += (size_t)snprintf(json_out + pos, cap - pos, "\",\"version\":\"");
		json_append_escaped(json_out, cap, &pos, m.apps[i].version);
		pos += (size_t)snprintf(json_out + pos, cap - pos, "\",\"identifier\":\"");
		json_append_escaped(json_out, cap, &pos, m.apps[i].identifier);
		pos += (size_t)snprintf(json_out + pos, cap - pos, "\",\"archs\":\"");
		json_append_escaped(json_out, cap, &pos, m.apps[i].archs);
		pos += (size_t)snprintf(json_out + pos, cap - pos, "\",\"bundle\":\"");
		json_append_escaped(json_out, cap, &pos, m.apps[i].bundle);
		pos += (size_t)snprintf(json_out + pos, cap - pos, "\",\"program\":\"");
		json_append_escaped(json_out, cap, &pos, m.apps[i].program);
		pos += (size_t)snprintf(json_out + pos, cap - pos, "\",\"icon\":\"");
		json_append_escaped(json_out, cap, &pos, m.apps[i].icon);
		pos += (size_t)snprintf(json_out + pos, cap - pos,
			"\",\"program_kind\":%d,\"runs_on_this_cpu\":%s}",
			m.apps[i].program_kind, m.apps[i].runs_on_this_cpu ? "true" : "false");
	}
	pos += (size_t)snprintf(json_out + pos, cap - pos, "]}");
	return 0;
}

static void stdout_mac_progress(int step, int of, const char *what) {
	printf("progress %d/%d %s\n", step, of, what);
	fflush(stdout);
}

int olympium_mac_install_json(const char *path, const char *desk_root,
		char *json_out, size_t cap, char *err_out, size_t err_cap) {
	struct mac_app_info apps[MAC_APPS_MAX];
	char err[400] = "";
	int count = mac_install_file(path, desk_root, apps, MAC_APPS_MAX, err, sizeof err, stdout_mac_progress);
	if (count < 0) {
		if (err_out != NULL && err_cap > 0) {
			snprintf(err_out, err_cap, "%s", err);
		}
		return -1;
	}
	size_t pos = 0;
	pos += (size_t)snprintf(json_out + pos, cap - pos, "{\"apps\":[");
	for (int i = 0; i < count && pos + 64 < cap; i++) {
		if (i > 0) {
			json_out[pos++] = ',';
		}
		pos += (size_t)snprintf(json_out + pos, cap - pos, "{\"name\":\"");
		json_append_escaped(json_out, cap, &pos, apps[i].name);
		pos += (size_t)snprintf(json_out + pos, cap - pos, "\",\"version\":\"");
		json_append_escaped(json_out, cap, &pos, apps[i].version);
		pos += (size_t)snprintf(json_out + pos, cap - pos, "\",\"archs\":\"");
		json_append_escaped(json_out, cap, &pos, apps[i].archs);
		pos += (size_t)snprintf(json_out + pos, cap - pos, "\",\"bundle\":\"");
		json_append_escaped(json_out, cap, &pos, apps[i].bundle);
		pos += (size_t)snprintf(json_out + pos, cap - pos, "\",\"program\":\"");
		json_append_escaped(json_out, cap, &pos, apps[i].program);
		pos += (size_t)snprintf(json_out + pos, cap - pos, "\",\"icon\":\"");
		json_append_escaped(json_out, cap, &pos, apps[i].icon);
		pos += (size_t)snprintf(json_out + pos, cap - pos, "\",\"runs_on_this_cpu\":%s}",
			apps[i].runs_on_this_cpu ? "true" : "false");
	}
	pos += (size_t)snprintf(json_out + pos, cap - pos, "]}");
	return count;
}

/* ---- Accounts helpers (re-exported for VB.NET P/Invoke) ---- */

int olympium_acct_name_ok(const char *name) { return acct_name_ok(name); }
int olympium_acct_password_ok(const char *pw, char *why, size_t n) { return acct_password_ok(pw, why, n); }
int olympium_acct_exists(const char *name) { return acct_exists(name); }
int olympium_acct_has_password(const char *name) { return acct_has_password(name); }
int olympium_acct_check_password(const char *name, const char *pw) { return acct_check_password(name, pw); }
int olympium_acct_set_password(const char *name, const char *pw) { return acct_set_password(name, pw); }
int olympium_acct_create(const char *name, const char *full, const char *pw) { return acct_create(name, full, pw); }
int olympium_acct_setup_done(void) { return acct_setup_done(); }
int olympium_acct_mark_setup_done(const char *name) { return acct_mark_setup_done(name); }
int olympium_acct_primary(char *out, size_t n) { return acct_primary(out, n); }
