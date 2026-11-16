/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 */
#define _GNU_SOURCE
#include "macbundle.h"

#include "macho.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

enum {
	PLIST_MAX = 4 * 1024 * 1024,
	ICNS_MAX = 64 * 1024 * 1024
};

/* Reads a whole file that is not a link, up to limit bytes. */
static unsigned char *read_file(const char *path, size_t limit, size_t *length) {
	int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) {
		return NULL;
	}
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
			(size_t)st.st_size > limit) {
		close(fd);
		return NULL;
	}
	unsigned char *data = malloc((size_t)st.st_size + 1);
	if (data == NULL) {
		close(fd);
		return NULL;
	}
	size_t got = 0;
	while (got < (size_t)st.st_size) {
		ssize_t n = read(fd, data + got, (size_t)st.st_size - got);
		if (n <= 0) {
			break;
		}
		got += (size_t)n;
	}
	close(fd);
	data[got] = '\0';
	*length = got;
	return data;
}

int mac_is_bundle_dir(const char *path) {
	char plist[4200];
	struct stat st;
	if (lstat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		return 0;
	}
	snprintf(plist, sizeof plist, "%s/Contents/Info.plist", path);
	return lstat(plist, &st) == 0 && S_ISREG(st.st_mode);
}

/* A value becomes one line of plain text: no control characters. */
static void clean_text(char *text) {
	char *out = text;
	for (unsigned char *p = (unsigned char *)text; *p != '\0'; p++) {
		if (*p < 0x20 || *p == 0x7f) {
			if (out != text && out[-1] != ' ') {
				*out++ = ' ';
			}
		} else {
			*out++ = (char)*p;
		}
	}
	while (out != text && out[-1] == ' ') {
		out--;
	}
	*out = '\0';
}

static size_t put_utf8(uint32_t c, char *out, size_t room) {
	if (c < 0x80) {
		if (room < 1) {
			return 0;
		}
		out[0] = (char)c;
		return 1;
	}
	if (c < 0x800) {
		if (room < 2) {
			return 0;
		}
		out[0] = (char)(0xC0 | (c >> 6));
		out[1] = (char)(0x80 | (c & 0x3F));
		return 2;
	}
	if (c < 0x10000) {
		if (room < 3) {
			return 0;
		}
		out[0] = (char)(0xE0 | (c >> 12));
		out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
		out[2] = (char)(0x80 | (c & 0x3F));
		return 3;
	}
	if (room < 4) {
		return 0;
	}
	out[0] = (char)(0xF0 | (c >> 18));
	out[1] = (char)(0x80 | ((c >> 12) & 0x3F));
	out[2] = (char)(0x80 | ((c >> 6) & 0x3F));
	out[3] = (char)(0x80 | (c & 0x3F));
	return 4;
}

/* ---- XML property list ---- */

static int xml_string(const char *text, size_t length, const char *key, char *out, size_t n) {
	char pattern[200];
	int plen = snprintf(pattern, sizeof pattern, "<key>%s</key>", key);
	if (plen <= 0 || (size_t)plen >= sizeof pattern) {
		return -1;
	}
	const char *end = text + length;
	const char *at = memmem(text, length, pattern, (size_t)plen);
	if (at == NULL) {
		return -1;
	}
	at += plen;
	while (at < end && (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n')) {
		at++;
	}
	if ((size_t)(end - at) < 8 || strncmp(at, "<string>", 8) != 0) {
		return -1;
	}
	at += 8;
	const char *close = memmem(at, (size_t)(end - at), "</string>", 9);
	if (close == NULL) {
		return -1;
	}
	size_t k = 0;
	while (at < close && k + 5 < n) {
		if (*at != '&') {
			out[k++] = *at++;
			continue;
		}
		const char *semi = memchr(at, ';', (size_t)(close - at));
		if (semi == NULL || semi - at > 10) {
			out[k++] = *at++;
			continue;
		}
		size_t name_len = (size_t)(semi - at - 1);
		const char *name = at + 1;
		uint32_t c = 0;
		if (name_len == 3 && strncmp(name, "amp", 3) == 0) {
			c = '&';
		} else if (name_len == 2 && strncmp(name, "lt", 2) == 0) {
			c = '<';
		} else if (name_len == 2 && strncmp(name, "gt", 2) == 0) {
			c = '>';
		} else if (name_len == 4 && strncmp(name, "quot", 4) == 0) {
			c = '"';
		} else if (name_len == 4 && strncmp(name, "apos", 4) == 0) {
			c = '\'';
		} else if (name_len >= 2 && name[0] == '#') {
			char number[12];
			size_t digits = name_len - 1 < sizeof number - 1 ? name_len - 1 : sizeof number - 1;
			memcpy(number, name + 1, digits);
			number[digits] = '\0';
			c = number[0] == 'x' ? (uint32_t)strtoul(number + 1, NULL, 16)
				: (uint32_t)strtoul(number, NULL, 10);
			if (c == 0 || c > 0x10FFFF) {
				c = '?';
			}
		} else {
			out[k++] = *at++;
			continue;
		}
		k += put_utf8(c, out + k, n - 1 - k);
		at = semi + 1;
	}
	out[k] = '\0';
	clean_text(out);
	return 0;
}

/* ---- binary property list ----
 *
 * "bplist00", the objects, a table of offsets to them, and a 32-byte trailer
 * that says how wide the offsets and object numbers are, how many objects
 * there are, which one is the top, and where the table starts. Every read
 * below is checked against the length: the file is whatever its author
 * wrote. */

struct bplist {
	const unsigned char *data;
	size_t length;
	unsigned offset_size;
	unsigned ref_size;
	uint64_t objects;
	uint64_t top;
	uint64_t table;
};

static int be_uint(const unsigned char *p, size_t available, unsigned size, uint64_t *value) {
	if (size == 0 || size > 8 || available < size) {
		return -1;
	}
	uint64_t v = 0;
	for (unsigned i = 0; i < size; i++) {
		v = (v << 8) | p[i];
	}
	*value = v;
	return 0;
}

static int bplist_offset(const struct bplist *b, uint64_t index, size_t *offset) {
	if (index >= b->objects) {
		return -1;
	}
	uint64_t at = b->table + index * b->offset_size;
	uint64_t value;
	if (at >= b->length || be_uint(b->data + at, b->length - (size_t)at, b->offset_size, &value) != 0 ||
			value >= b->table) {
		return -1;
	}
	*offset = (size_t)value;
	return 0;
}

/* The marker byte's low half is a count, or 15 when a longer count follows. */
static int bplist_count(const struct bplist *b, size_t *at, uint64_t *count) {
	if (*at >= b->length) {
		return -1;
	}
	unsigned low = b->data[*at] & 0x0F;
	(*at)++;
	if (low != 0x0F) {
		*count = low;
		return 0;
	}
	if (*at >= b->length || (b->data[*at] & 0xF0) != 0x10) {
		return -1;
	}
	unsigned size = 1u << (b->data[*at] & 0x0F);
	(*at)++;
	if (be_uint(b->data + *at, b->length - *at, size, count) != 0) {
		return -1;
	}
	*at += size;
	return 0;
}

/* Copies the string object at index into out as UTF-8. */
static int bplist_text(const struct bplist *b, uint64_t index, char *out, size_t n) {
	size_t at;
	if (bplist_offset(b, index, &at) != 0 || n == 0) {
		return -1;
	}
	unsigned kind = b->data[at] & 0xF0;
	uint64_t count;
	if ((kind != 0x50 && kind != 0x60 && kind != 0x70) || bplist_count(b, &at, &count) != 0) {
		return -1;
	}
	size_t k = 0;
	if (kind == 0x60) {
		if (count > (b->length - at) / 2) {
			return -1;
		}
		for (uint64_t i = 0; i < count; i++) {
			uint32_t c = ((uint32_t)b->data[at + 2 * i] << 8) | b->data[at + 2 * i + 1];
			if (c >= 0xD800 && c < 0xDC00 && i + 1 < count) {
				uint32_t low = ((uint32_t)b->data[at + 2 * i + 2] << 8) | b->data[at + 2 * i + 3];
				if (low >= 0xDC00 && low < 0xE000) {
					c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
					i++;
				}
			}
			size_t wrote = put_utf8(c, out + k, n - 1 - k);
			if (wrote == 0) {
				break;
			}
			k += wrote;
		}
	} else {
		if (count > b->length - at) {
			return -1;
		}
		for (uint64_t i = 0; i < count && k + 1 < n; i++) {
			out[k++] = (char)b->data[at + i];
		}
	}
	out[k] = '\0';
	clean_text(out);
	return 0;
}

static int binary_string(const unsigned char *data, size_t length, const char *key,
		char *out, size_t n) {
	if (length < 8 + 32) {
		return -1;
	}
	struct bplist b = {.data = data, .length = length};
	const unsigned char *trailer = data + length - 32;
	b.offset_size = trailer[6];
	b.ref_size = trailer[7];
	if (be_uint(trailer + 8, 8, 8, &b.objects) != 0 || be_uint(trailer + 16, 8, 8, &b.top) != 0 ||
			be_uint(trailer + 24, 8, 8, &b.table) != 0) {
		return -1;
	}
	if (b.offset_size == 0 || b.offset_size > 8 || b.ref_size == 0 || b.ref_size > 8 ||
			b.objects == 0 || b.objects > length || b.table < 8 || b.table > length - 32 ||
			b.objects > (length - 32 - b.table) / b.offset_size) {
		return -1;
	}
	size_t at;
	if (bplist_offset(&b, b.top, &at) != 0 || (data[at] & 0xF0) != 0xD0) {
		return -1;
	}
	uint64_t pairs;
	if (bplist_count(&b, &at, &pairs) != 0 || pairs > (length - at) / (2 * b.ref_size)) {
		return -1;
	}
	for (uint64_t i = 0; i < pairs; i++) {
		uint64_t key_ref;
		uint64_t value_ref;
		if (be_uint(data + at + i * b.ref_size, length - at - i * b.ref_size, b.ref_size, &key_ref) != 0 ||
				be_uint(data + at + (pairs + i) * b.ref_size,
					length - at - (pairs + i) * b.ref_size, b.ref_size, &value_ref) != 0) {
			return -1;
		}
		char name[200];
		if (bplist_text(&b, key_ref, name, sizeof name) == 0 && strcmp(name, key) == 0) {
			return bplist_text(&b, value_ref, out, n);
		}
	}
	return -1;
}

int mac_plist_string(const unsigned char *data, size_t length, const char *key,
		char *out, size_t n) {
	if (data == NULL || n == 0) {
		return -1;
	}
	out[0] = '\0';
	if (length >= 8 && memcmp(data, "bplist00", 8) == 0) {
		return binary_string(data, length, key, out, n);
	}
	return xml_string((const char *)data, length, key, out, n);
}

/* ---- icons ---- */

static uint32_t be32(const unsigned char *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int mac_icns_png(const unsigned char *data, size_t length, size_t *offset, size_t *png_length) {
	static const unsigned char png_magic[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	/* Sizes near what a launcher draws first, then larger, then smaller. */
	static const char *const wanted[] = {
		"ic08", "ic13", "ic14", "ic09", "ic07", "ic10", "ic12", "icp6", "ic11", "icp5", "icp4",
	};
	if (length < 16 || memcmp(data, "icns", 4) != 0) {
		return -1;
	}
	size_t total = be32(data + 4);
	if (total > length) {
		total = length;
	}
	int best = -1;
	size_t best_at = 0;
	size_t best_len = 0;
	size_t at = 8;
	while (at + 8 <= total) {
		size_t size = be32(data + at + 4);
		if (size < 8 || size > total - at) {
			break;
		}
		if (size >= 16 && memcmp(data + at + 8, png_magic, 8) == 0) {
			for (int i = 0; i < (int)(sizeof wanted / sizeof wanted[0]); i++) {
				if (memcmp(data + at, wanted[i], 4) == 0 && (best < 0 || i < best)) {
					best = i;
					best_at = at + 8;
					best_len = size - 8;
				}
			}
		}
		at += size;
	}
	if (best < 0) {
		return -1;
	}
	*offset = best_at;
	*png_length = best_len;
	return 0;
}

/* ---- the bundle ---- */

static int safe_leaf(const char *name) {
	return name[0] != '\0' && strchr(name, '/') == NULL &&
		strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

static void bundle_base(const char *bundle_dir, char *out, size_t n) {
	const char *slash = strrchr(bundle_dir, '/');
	snprintf(out, n, "%s", slash != NULL ? slash + 1 : bundle_dir);
	size_t len = strlen(out);
	if (len > 4 && strcasecmp(out + len - 4, ".app") == 0) {
		out[len - 4] = '\0';
	}
	clean_text(out);
}

int mac_bundle_read(const char *bundle_dir, struct mac_bundle *out) {
	memset(out, 0, sizeof *out);
	char path[4200];
	snprintf(path, sizeof path, "%s/Contents/Info.plist", bundle_dir);
	size_t length = 0;
	unsigned char *plist = read_file(path, PLIST_MAX, &length);
	if (plist == NULL) {
		return -1;
	}
	char base[128];
	bundle_base(bundle_dir, base, sizeof base);
	if (mac_plist_string(plist, length, "CFBundleDisplayName", out->name, sizeof out->name) != 0 ||
			out->name[0] == '\0') {
		if (mac_plist_string(plist, length, "CFBundleName", out->name, sizeof out->name) != 0 ||
				out->name[0] == '\0') {
			snprintf(out->name, sizeof out->name, "%s", base);
		}
	}
	if (mac_plist_string(plist, length, "CFBundleExecutable", out->executable,
			sizeof out->executable) != 0 || !safe_leaf(out->executable)) {
		/* No name, or one with a slash in it, which could point anywhere. */
		snprintf(out->executable, sizeof out->executable, "%s", base);
	}
	mac_plist_string(plist, length, "CFBundleIdentifier", out->identifier, sizeof out->identifier);
	if (mac_plist_string(plist, length, "CFBundleShortVersionString", out->version,
			sizeof out->version) != 0 || out->version[0] == '\0') {
		mac_plist_string(plist, length, "CFBundleVersion", out->version, sizeof out->version);
	}
	mac_plist_string(plist, length, "LSMinimumSystemVersion", out->min_system, sizeof out->min_system);
	char icon[160] = "";
	mac_plist_string(plist, length, "CFBundleIconFile", icon, sizeof icon);
	free(plist);

	if (safe_leaf(out->executable)) {
		snprintf(out->program, sizeof out->program, "%s/Contents/MacOS/%s", bundle_dir, out->executable);
		int fd = open(out->program, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
		struct stat st;
		if (fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
			unsigned char head[4096];
			ssize_t got = read(fd, head, sizeof head);
			if (got > 0) {
				out->arch_count = dev_macho_archs(head, (size_t)got, out->archs, 8);
			}
			if (out->arch_count > 0) {
				out->program_kind = MAC_PROGRAM_MACHO;
			} else if (got >= 2 && head[0] == '#' && head[1] == '!') {
				out->program_kind = MAC_PROGRAM_SCRIPT;
			} else {
				out->program_kind = MAC_PROGRAM_OTHER;
			}
		} else {
			out->program[0] = '\0';
		}
		if (fd >= 0) {
			close(fd);
		}
	}

	struct stat st;
	if (icon[0] != '\0' && safe_leaf(icon)) {
		snprintf(out->icon_path, sizeof out->icon_path, "%s/Contents/Resources/%s", bundle_dir, icon);
		if (lstat(out->icon_path, &st) != 0 || !S_ISREG(st.st_mode)) {
			/* The key often leaves the extension off. */
			snprintf(out->icon_path, sizeof out->icon_path, "%s/Contents/Resources/%s.icns",
				bundle_dir, icon);
			if (lstat(out->icon_path, &st) != 0 || !S_ISREG(st.st_mode)) {
				out->icon_path[0] = '\0';
			}
		}
	}
	if (out->icon_path[0] == '\0') {
		/* No key, or a missing file: take the first .icns there is. */
		char dir_path[4200];
		snprintf(dir_path, sizeof dir_path, "%s/Contents/Resources", bundle_dir);
		DIR *dir = opendir(dir_path);
		if (dir != NULL) {
			struct dirent *ent;
			char first[256] = "";
			while ((ent = readdir(dir)) != NULL) {
				size_t len = strlen(ent->d_name);
				if (len > 5 && strcasecmp(ent->d_name + len - 5, ".icns") == 0 &&
						(first[0] == '\0' || strcmp(ent->d_name, first) < 0) && len < sizeof first) {
					snprintf(first, sizeof first, "%s", ent->d_name);
				}
			}
			closedir(dir);
			if (first[0] != '\0') {
				snprintf(out->icon_path, sizeof out->icon_path, "%s/%s", dir_path, first);
				if (lstat(out->icon_path, &st) != 0 || !S_ISREG(st.st_mode)) {
					out->icon_path[0] = '\0';
				}
			}
		}
	}
	return 0;
}

void mac_archs_text(const uint32_t *archs, int count, char *out, size_t n) {
	out[0] = '\0';
	for (int i = 0; i < count; i++) {
		size_t used = strlen(out);
		snprintf(out + used, n - used, "%s%s", i > 0 ? ", " : "", dev_macho_cputype_name(archs[i]));
	}
}

const char *mac_host_arch_name(void) {
	static char machine[64];
	const char *forced = getenv("OLYMPIUM_HOST_MACHINE");
	struct utsname u;
	if (forced != NULL && forced[0] != '\0') {
		snprintf(machine, sizeof machine, "%s", forced);
	} else if (uname(&u) == 0) {
		snprintf(machine, sizeof machine, "%s", u.machine);
	} else {
		machine[0] = '\0';
	}
	if (strcmp(machine, "aarch64") == 0 || strcmp(machine, "arm64") == 0) {
		return "arm64";
	}
	if (strcmp(machine, "x86_64") == 0) {
		return "x86_64";
	}
	if (machine[0] == 'i' && strcmp(machine + 2, "86") == 0) {
		return "i386";
	}
	return machine;
}

int mac_host_can_run(const uint32_t *archs, int count) {
	const char *host = mac_host_arch_name();
	for (int i = 0; i < count; i++) {
		if (strcmp(dev_macho_cputype_name(archs[i]), host) == 0) {
			return 1;
		}
	}
	return 0;
}
