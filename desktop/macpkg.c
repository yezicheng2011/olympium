/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 */
#define _GNU_SOURCE
#include "macpkg.h"

#include "macbundle.h"
#include "sandbox.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <lzma.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

enum {
	PATH_ROOM = 2048,
	NESTED_MAX = 4,
	PAYLOAD_MAX = 8
};

/* ------------------------------------------------------------- tools */

static const char *first_tool(const char *const *candidates, size_t count) {
	for (size_t i = 0; i < count; i++) {
		if (access(candidates[i], X_OK) == 0) {
			return candidates[i];
		}
	}
	return NULL;
}

const char *mac_archiver(void) {
	static const char *const names[] = {"/usr/bin/7zz", "/usr/bin/7z"};
	const char *forced = getenv("OLYMPIUM_7Z");
	if (forced != NULL && forced[0] != '\0') {
		return access(forced, X_OK) == 0 ? forced : NULL;
	}
	return first_tool(names, 2);
}

static const char *cpio_tool(void) {
	static const char *const names[] = {"/usr/bin/cpio", "/bin/cpio"};
	return first_tool(names, 2);
}

static const char *cp_tool(void) {
	static const char *const names[] = {"/usr/bin/cp", "/bin/cp"};
	return first_tool(names, 2);
}

/* ---------------------------------------------------------- detection */

static int ends_with(const char *text, const char *suffix) {
	size_t a = strlen(text);
	size_t b = strlen(suffix);
	return a >= b && strcasecmp(text + a - b, suffix) == 0;
}

const char *mac_detect(const char *path) {
	struct stat st;
	if (lstat(path, &st) != 0) {
		return NULL;
	}
	if (S_ISDIR(st.st_mode)) {
		return mac_is_bundle_dir(path) ? "mac_app" : NULL;
	}
	if (!S_ISREG(st.st_mode) || st.st_size < 28) {
		return NULL;
	}
	int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) {
		return NULL;
	}
	unsigned char head[4];
	unsigned char tail[4] = {0, 0, 0, 0};
	const char *kind = NULL;
	if (read(fd, head, 4) == 4) {
		if (memcmp(head, "xar!", 4) == 0) {
			kind = "mac_pkg";
		} else if (st.st_size >= 512 && pread(fd, tail, 4, st.st_size - 512) == 4 &&
				memcmp(tail, "koly", 4) == 0) {
			/* Every compressed or read-only disk image ends with this block. */
			kind = "mac_dmg";
		} else if (ends_with(path, ".dmg") && st.st_size >= 2048) {
			/* Named as a disk image and without that block: a read/write
			 * image, which is the bare volume, or an encrypted one. The
			 * archiver reads the first and says so about the second. */
			kind = "mac_dmg";
		}
	}
	close(fd);
	return kind;
}

/* ---------------------------------------------------- child processes */

struct child {
	char *const *argv;            /* a program, or NULL to call fn */
	int (*fn)(int in_fd, int out_fd);
	const char *read_path;
	const char *write_dir;
	const char *cwd;
};

static void close_other_fds(void) {
#ifdef SYS_close_range
	if (syscall(SYS_close_range, 3, ~0U, 0) == 0) {
		return;
	}
#endif
	for (int fd = 3; fd < 1024; fd++) {
		close(fd);
	}
}

static pid_t spawn_confined(const struct child *spec, int in_fd, int out_fd, int err_fd) {
	pid_t pid = fork();
	if (pid != 0) {
		return pid;
	}
	int nul = open("/dev/null", O_RDWR);
	dup2(in_fd >= 0 ? in_fd : nul, 0);
	dup2(out_fd >= 0 ? out_fd : nul, 1);
	dup2(err_fd >= 0 ? err_fd : nul, 2);
	close_other_fds();
	signal(SIGPIPE, SIG_DFL);
	if (spec->cwd != NULL && chdir(spec->cwd) != 0) {
		_exit(124);
	}
	if (dev_confine_self(spec->read_path, spec->write_dir) < 0) {
		/* Not unpacked rather than unpacked without limits. */
		_exit(125);
	}
	if (spec->argv == NULL) {
		_exit(spec->fn(0, 1));
	}
	execv(spec->argv[0], spec->argv);
	_exit(127);
}

/* Why a child refused to start unpacking. */
static const char *confine_failure(void) {
	if (dev_confine_probe() == DEV_CONFINE_NOBODY) {
		return "this kernel has no Landlock, so the unpacker runs as the user nobody, "
			"and that user cannot read the file";
	}
	return "the unpacker could not be confined on this system";
}

static int wait_status(pid_t pid) {
	int st = 0;
	if (pid <= 0 || waitpid(pid, &st, 0) < 0) {
		return -1;
	}
	return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
}

/* What the tool said on standard error, cut to one or two lines. */
static void read_errors(FILE *errors, char *out, size_t n) {
	out[0] = '\0';
	if (errors == NULL || n == 0) {
		return;
	}
	rewind(errors);
	size_t got = fread(out, 1, n - 1, errors);
	out[got] = '\0';
	/* One line, single spaces. */
	char *to = out;
	for (char *p = out; *p != '\0'; p++) {
		char c = (*p == '\n' || *p == '\r' || *p == '\t') ? ' ' : *p;
		if (c == ' ' && (to == out || to[-1] == ' ')) {
			continue;
		}
		*to++ = c;
	}
	while (to != out && to[-1] == ' ') {
		to--;
	}
	*to = '\0';
}

/* Runs one program and hands its output to line, a line at a time. */
static int run_lines(const struct child *spec, void (*line)(void *ctx, char *text), void *ctx,
		char *errtext, size_t errlen) {
	int out[2];
	if (pipe2(out, O_CLOEXEC) != 0) {
		return -1;
	}
	FILE *errors = tmpfile();
	pid_t pid = spawn_confined(spec, -1, out[1], errors != NULL ? fileno(errors) : -1);
	close(out[1]);
	if (pid < 0) {
		close(out[0]);
		if (errors != NULL) {
			fclose(errors);
		}
		return -1;
	}
	FILE *in = fdopen(out[0], "r");
	char *text = NULL;
	size_t room = 0;
	ssize_t len;
	while (in != NULL && (len = getline(&text, &room, in)) >= 0) {
		while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
			text[--len] = '\0';
		}
		if (line != NULL) {
			line(ctx, text);
		}
	}
	free(text);
	if (in != NULL) {
		fclose(in);
	} else {
		close(out[0]);
	}
	int status = wait_status(pid);
	if (errtext != NULL) {
		read_errors(errors, errtext, errlen);
	}
	if (errors != NULL) {
		fclose(errors);
	}
	return status;
}

/* 7-Zip returns 2 when it skipped a link that points out of the archive,
 * which most disk images have: a shortcut to /Applications. That is the
 * archiver doing its job. Anything else it calls an error is one. */
static int archiver_ok(int status, const char *errtext) {
	if (status == 0) {
		return 1;
	}
	if (status != 2) {
		return 0;
	}
	const char *p = errtext;
	int skipped = 0;
	while ((p = strstr(p, "ERROR:")) != NULL) {
		p += 6;
		while (*p == ' ') {
			p++;
		}
		if (strncmp(p, "Dangerous link path was ignored", 31) != 0) {
			return 0;
		}
		skipped++;
	}
	return skipped > 0;
}

/* ------------------------------------------------- reading the listing */

struct entry {
	char path[PATH_ROOM];
	int is_dir;
	int is_link;
	unsigned long long size;
	int open;
};

struct found {
	struct entry cur;
	char bundles[MAC_APPS_MAX][1024];
	int bundle_count;
	unsigned long long bundle_bytes;
	char nested[NESTED_MAX][256];
	int nested_count;
	char payloads[PAYLOAD_MAX][512];
	int payload_count;
	char infos[PAYLOAD_MAX][512];
	int info_count;
	char distribution[64];
	int has_scripts;
	int entries;
};

/* The path up to and including the first folder whose name ends in .app. */
static int app_root(const char *path, char *root, size_t n) {
	const char *start = path;
	while (*start != '\0') {
		const char *slash = strchr(start, '/');
		size_t len = slash != NULL ? (size_t)(slash - start) : strlen(start);
		if (len > 4 && strncasecmp(start + len - 4, ".app", 4) == 0) {
			size_t total = (size_t)(start - path) + len;
			if (total >= n) {
				return 0;
			}
			memcpy(root, path, total);
			root[total] = '\0';
			return 1;
		}
		if (slash == NULL) {
			break;
		}
		start = slash + 1;
	}
	return 0;
}

static void add_text(char *list, int *count, int max, size_t width, const char *text) {
	if (strlen(text) >= width) {
		return;
	}
	for (int i = 0; i < *count; i++) {
		if (strcmp(list + (size_t)i * width, text) == 0) {
			return;
		}
	}
	if (*count < max) {
		snprintf(list + (size_t)(*count) * width, width, "%s", text);
		(*count)++;
	}
}

static void entry_done(struct found *f) {
	struct entry *e = &f->cur;
	if (!e->open) {
		return;
	}
	e->open = 0;
	f->entries++;
	char root[1024];
	if (app_root(e->path, root, sizeof root)) {
		size_t len = strlen(root);
		if (strcmp(e->path + len, "/Contents/Info.plist") == 0 && !e->is_dir) {
			add_text(f->bundles[0], &f->bundle_count, MAC_APPS_MAX, sizeof f->bundles[0], root);
		}
		if (!e->is_dir) {
			f->bundle_bytes += e->size;
		}
		return;
	}
	const char *leaf = strrchr(e->path, '/');
	leaf = leaf != NULL ? leaf + 1 : e->path;
	if (e->is_dir) {
		return;
	}
	if (strchr(e->path, '/') == NULL &&
			(ends_with(e->path, ".hfs") || ends_with(e->path, ".hfsx") ||
			ends_with(e->path, ".apfs") || ends_with(e->path, ".img") ||
			ends_with(e->path, ".iso") || ends_with(e->path, ".udf"))) {
		/* A volume the archiver did not open by itself. */
		add_text(f->nested[0], &f->nested_count, NESTED_MAX, sizeof f->nested[0], e->path);
	}
	if (strcmp(leaf, "Payload") == 0) {
		add_text(f->payloads[0], &f->payload_count, PAYLOAD_MAX, sizeof f->payloads[0], e->path);
	} else if (strcmp(leaf, "PackageInfo") == 0) {
		add_text(f->infos[0], &f->info_count, PAYLOAD_MAX, sizeof f->infos[0], e->path);
	} else if (strcmp(leaf, "Scripts") == 0) {
		f->has_scripts = 1;
	} else if (strcmp(e->path, "Distribution") == 0) {
		snprintf(f->distribution, sizeof f->distribution, "%s", e->path);
	}
}

/* One line of "7z l -slt -ba": blocks of "Name = value", a blank line between. */
static void list_line(void *ctx, char *text) {
	struct found *f = ctx;
	if (strncmp(text, "Path = ", 7) == 0) {
		entry_done(f);
		memset(&f->cur, 0, sizeof f->cur);
		f->cur.open = 1;
		snprintf(f->cur.path, sizeof f->cur.path, "%s", text + 7);
	} else if (text[0] == '\0') {
		entry_done(f);
	} else if (!f->cur.open) {
		return;
	} else if (strcmp(text, "Folder = +") == 0) {
		f->cur.is_dir = 1;
	} else if (strncmp(text, "Size = ", 7) == 0) {
		f->cur.size = strtoull(text + 7, NULL, 10);
	} else if (strncmp(text, "Mode = ", 7) == 0) {
		f->cur.is_link = text[7] == 'l';
		if (text[7] == 'd') {
			f->cur.is_dir = 1;
		}
	}
}

static int list_archive(const char *archive, struct found *f, char *errtext, size_t errlen) {
	const char *tool = mac_archiver();
	if (tool == NULL) {
		snprintf(errtext, errlen, "7-Zip is not installed");
		return -1;
	}
	memset(f, 0, sizeof *f);
	char *argv[] = {(char *)tool, "l", "-slt", "-ba", "-sccUTF-8", "--", (char *)archive, NULL};
	struct child spec = {.argv = argv, .read_path = archive};
	int status = run_lines(&spec, list_line, f, errtext, errlen);
	entry_done(f);
	if (status == 125) {
		snprintf(errtext, errlen, "%s", confine_failure());
		return -1;
	}
	return (status == 0 || f->entries > 0) ? 0 : -1;
}

/* Takes the named paths out of an archive into dest. */
static int extract_paths(const char *archive, const char *dest, char paths[][1024], int count,
		char *errtext, size_t errlen) {
	const char *tool = mac_archiver();
	if (tool == NULL || count > MAC_APPS_MAX * 2) {
		return -1;
	}
	char out_arg[PATH_ROOM + 4];
	char includes[MAC_APPS_MAX * 2][1040];
	char *argv[12 + MAC_APPS_MAX * 2];
	int k = 0;
	snprintf(out_arg, sizeof out_arg, "-o%s", dest);
	argv[k++] = (char *)tool;
	argv[k++] = "x";
	argv[k++] = "-y";
	argv[k++] = "-bd";
	argv[k++] = "-bso0";
	argv[k++] = "-bsp0";
	/* Names are taken as they are: no wildcards, no list files. */
	argv[k++] = "-spd";
	argv[k++] = out_arg;
	for (int i = 0; i < count; i++) {
		snprintf(includes[i], sizeof includes[i], "-i!%s", paths[i]);
		argv[k++] = includes[i];
	}
	argv[k++] = "--";
	argv[k++] = (char *)archive;
	argv[k] = NULL;
	struct child spec = {.argv = argv, .read_path = archive, .write_dir = dest};
	dev_confine_prepare_dir(dest);
	char local[600];
	int status = run_lines(&spec, NULL, NULL, local, sizeof local);
	if (errtext != NULL) {
		snprintf(errtext, errlen, "%s", local);
	}
	if (status == 125) {
		if (errtext != NULL) {
			snprintf(errtext, errlen, "%s", confine_failure());
		}
		return -1;
	}
	return archiver_ok(status, local) ? 0 : -1;
}

/* --------------------------------------------------------- the payload */

static int write_all(int fd, const unsigned char *data, size_t length) {
	size_t done = 0;
	while (done < length) {
		ssize_t n = write(fd, data + done, length - done);
		if (n < 0 && errno == EINTR) {
			continue;
		}
		if (n <= 0) {
			return -1;
		}
		done += (size_t)n;
	}
	return 0;
}

static ssize_t read_full(int fd, unsigned char *data, size_t length) {
	size_t got = 0;
	while (got < length) {
		ssize_t n = read(fd, data + got, length - got);
		if (n < 0 && errno == EINTR) {
			continue;
		}
		if (n < 0) {
			return -1;
		}
		if (n == 0) {
			break;
		}
		got += (size_t)n;
	}
	return (ssize_t)got;
}

static int inflate_gzip(int in_fd, int out_fd, const unsigned char *start, size_t have) {
	z_stream z;
	memset(&z, 0, sizeof z);
	/* 15 window bits, plus 16 to say the stream has a gzip header. */
	if (inflateInit2(&z, 15 + 16) != Z_OK) {
		return 1;
	}
	unsigned char in[65536];
	unsigned char out[65536];
	memcpy(in, start, have);
	z.next_in = in;
	z.avail_in = (uInt)have;
	int result = 0;
	for (;;) {
		if (z.avail_in == 0) {
			ssize_t n = read(in_fd, in, sizeof in);
			if (n < 0 && errno == EINTR) {
				continue;
			}
			if (n <= 0) {
				break;
			}
			z.next_in = in;
			z.avail_in = (uInt)n;
		}
		z.next_out = out;
		z.avail_out = sizeof out;
		int rc = inflate(&z, Z_NO_FLUSH);
		if (rc != Z_OK && rc != Z_STREAM_END) {
			result = 1;
			break;
		}
		if (write_all(out_fd, out, sizeof out - z.avail_out) != 0) {
			result = 1;
			break;
		}
		if (rc == Z_STREAM_END) {
			/* A second gzip member may follow the first. */
			if (z.avail_in == 0) {
				ssize_t n = read(in_fd, in, sizeof in);
				if (n <= 0) {
					break;
				}
				z.next_in = in;
				z.avail_in = (uInt)n;
			}
			if (inflateReset(&z) != Z_OK) {
				result = 1;
				break;
			}
		}
	}
	inflateEnd(&z);
	return result;
}

static int decode_xz(const unsigned char *data, size_t length, int out_fd) {
	lzma_stream xz = LZMA_STREAM_INIT;
	if (lzma_stream_decoder(&xz, UINT64_MAX, LZMA_CONCATENATED) != LZMA_OK) {
		return 1;
	}
	unsigned char out[65536];
	xz.next_in = data;
	xz.avail_in = length;
	int result = 0;
	for (;;) {
		xz.next_out = out;
		xz.avail_out = sizeof out;
		lzma_ret rc = lzma_code(&xz, xz.avail_in == 0 ? LZMA_FINISH : LZMA_RUN);
		if (rc != LZMA_OK && rc != LZMA_STREAM_END) {
			result = 1;
			break;
		}
		if (write_all(out_fd, out, sizeof out - xz.avail_out) != 0) {
			result = 1;
			break;
		}
		if (rc == LZMA_STREAM_END) {
			break;
		}
	}
	lzma_end(&xz);
	return result;
}

static uint64_t be64(const unsigned char *p) {
	uint64_t v = 0;
	for (int i = 0; i < 8; i++) {
		v = (v << 8) | p[i];
	}
	return v;
}

/* pbzx: the four letters, eight bytes, then chunks. Each chunk says how long
 * it is unpacked and how long it is stored, and is an xz stream unless it was
 * stored as it is. The caller has already read the name and the eight bytes.
 * Decoders that stop at the first chunk shorter than 16 MB lose the rest of
 * a package made with another chunk size; this one reads to the end. */
static int decode_pbzx_chunks(int in_fd, int out_fd) {
	static const unsigned char xz_magic[6] = {0xFD, '7', 'z', 'X', 'Z', 0x00};
	const uint64_t chunk_limit = 256u * 1024 * 1024;
	unsigned char head[16];
	for (;;) {
		ssize_t got = read_full(in_fd, head, 16);
		if (got == 0) {
			return 0;
		}
		if (got != 16) {
			return 1;
		}
		uint64_t stored = be64(head + 8);
		if (stored == 0 || stored > chunk_limit) {
			return 1;
		}
		unsigned char *chunk = malloc((size_t)stored);
		if (chunk == NULL) {
			return 1;
		}
		if (read_full(in_fd, chunk, (size_t)stored) != (ssize_t)stored) {
			free(chunk);
			return 1;
		}
		int bad;
		if (stored >= 6 && memcmp(chunk, xz_magic, 6) == 0) {
			bad = decode_xz(chunk, (size_t)stored, out_fd);
		} else {
			bad = write_all(out_fd, chunk, (size_t)stored) != 0;
		}
		free(chunk);
		if (bad) {
			return 1;
		}
	}
}

static int decode_xz_stream(int in_fd, int out_fd, const unsigned char *start, size_t have) {
	lzma_stream xz = LZMA_STREAM_INIT;
	if (lzma_stream_decoder(&xz, UINT64_MAX, LZMA_CONCATENATED) != LZMA_OK) {
		return 1;
	}
	unsigned char in[65536];
	unsigned char out[65536];
	memcpy(in, start, have);
	xz.next_in = in;
	xz.avail_in = have;
	int ended = 0;
	int result = 0;
	for (;;) {
		if (xz.avail_in == 0 && !ended) {
			ssize_t n = read(in_fd, in, sizeof in);
			if (n < 0 && errno == EINTR) {
				continue;
			}
			if (n <= 0) {
				ended = 1;
			} else {
				xz.next_in = in;
				xz.avail_in = (size_t)n;
			}
		}
		xz.next_out = out;
		xz.avail_out = sizeof out;
		lzma_ret rc = lzma_code(&xz, ended ? LZMA_FINISH : LZMA_RUN);
		if (rc != LZMA_OK && rc != LZMA_STREAM_END) {
			result = 1;
			break;
		}
		if (write_all(out_fd, out, sizeof out - xz.avail_out) != 0) {
			result = 1;
			break;
		}
		if (rc == LZMA_STREAM_END) {
			break;
		}
	}
	lzma_end(&xz);
	return result;
}

int mac_unpayload(int in_fd, int out_fd) {
	unsigned char head[8];
	ssize_t have = read_full(in_fd, head, 6);
	if (have < 4) {
		return 1;
	}
	if (head[0] == 0x1f && head[1] == 0x8b) {
		return inflate_gzip(in_fd, out_fd, head, (size_t)have);
	}
	if (memcmp(head, "pbzx", 4) == 0) {
		/* Two of the eight bytes after the name are already here. */
		unsigned char rest[6];
		if (have != 6 || read_full(in_fd, rest, 6) != 6) {
			return 1;
		}
		return decode_pbzx_chunks(in_fd, out_fd);
	}
	if (have == 6 && head[0] == 0xFD && memcmp(head + 1, "7zXZ", 4) == 0) {
		return decode_xz_stream(in_fd, out_fd, head, (size_t)have);
	}
	if (memcmp(head, "0707", 4) == 0) {
		/* Not compressed at all. */
		if (write_all(out_fd, head, (size_t)have) != 0) {
			return 1;
		}
		unsigned char buf[65536];
		for (;;) {
			ssize_t n = read(in_fd, buf, sizeof buf);
			if (n < 0 && errno == EINTR) {
				continue;
			}
			if (n <= 0) {
				return n < 0;
			}
			if (write_all(out_fd, buf, (size_t)n) != 0) {
				return 1;
			}
		}
	}
	return 3;
}

/* archiver | decoder | cpio. Each of the three is confined on its own. With
 * names set, cpio lists the payload and each name goes to line. Without, it
 * unpacks into dest. */
static int run_payload(const char *pkg, const char *entry, const char *dest,
		void (*line)(void *ctx, char *text), void *ctx, char *errtext, size_t errlen) {
	const char *tool = mac_archiver();
	const char *cpio = cpio_tool();
	if (tool == NULL || cpio == NULL) {
		snprintf(errtext, errlen, "%s is not installed", tool == NULL ? "7-Zip" : "cpio");
		return -1;
	}
	int a[2];
	int b[2];
	int c[2] = {-1, -1};
	if (pipe2(a, O_CLOEXEC) != 0) {
		return -1;
	}
	if (pipe2(b, O_CLOEXEC) != 0) {
		close(a[0]);
		close(a[1]);
		return -1;
	}
	if (line != NULL && pipe2(c, O_CLOEXEC) != 0) {
		close(a[0]);
		close(a[1]);
		close(b[0]);
		close(b[1]);
		return -1;
	}
	FILE *errors = tmpfile();
	int err_fd = errors != NULL ? fileno(errors) : -1;
	char *take[] = {(char *)tool, "x", "-so", "-bd", "-bso0", "-bsp0", "-spd", "--",
		(char *)pkg, (char *)entry, NULL};
	char *list[] = {(char *)cpio, "-it", "--quiet", NULL};
	char *unpack[] = {(char *)cpio, "-idm", "--quiet", "--no-absolute-filenames",
		"--no-preserve-owner", NULL};
	struct child first = {.argv = take, .read_path = pkg};
	struct child second = {.fn = mac_unpayload};
	struct child third = {.argv = line != NULL ? list : unpack, .write_dir = dest, .cwd = dest};
	if (dest != NULL) {
		dev_confine_prepare_dir(dest);
	}
	pid_t p1 = spawn_confined(&first, -1, a[1], err_fd);
	pid_t p2 = spawn_confined(&second, a[0], b[1], err_fd);
	pid_t p3 = spawn_confined(&third, b[0], line != NULL ? c[1] : -1, err_fd);
	close(a[0]);
	close(a[1]);
	close(b[0]);
	close(b[1]);
	if (line != NULL) {
		close(c[1]);
		FILE *in = fdopen(c[0], "r");
		char *text = NULL;
		size_t room = 0;
		ssize_t len;
		while (in != NULL && (len = getline(&text, &room, in)) >= 0) {
			while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
				text[--len] = '\0';
			}
			line(ctx, text);
		}
		free(text);
		if (in != NULL) {
			fclose(in);
		}
	}
	int s1 = wait_status(p1);
	int s2 = wait_status(p2);
	int s3 = wait_status(p3);
	read_errors(errors, errtext, errlen);
	if (errors != NULL) {
		fclose(errors);
	}
	if (s1 == 125 || s2 == 125 || s3 == 125) {
		snprintf(errtext, errlen, "%s", confine_failure());
		return -1;
	}
	if (s2 == 3) {
		snprintf(errtext, errlen, "the payload is packed in a way this desktop does not read");
		return -1;
	}
	if (s1 != 0 || s2 != 0) {
		if (errtext[0] == '\0') {
			snprintf(errtext, errlen, "the payload could not be decoded");
		}
		return -1;
	}
	/* cpio also has to finish cleanly. It reports a full disk and an entry
	 * it refused to write through a link the same way, and a package that
	 * was only partly unpacked must not be installed. */
	if (s3 != 0) {
		if (errtext[0] == '\0') {
			snprintf(errtext, errlen, "cpio stopped with status %d", s3);
		}
		return -1;
	}
	return 0;
}

/* ------------------------------------------------------- small helpers */

static void mkdirs(const char *path, mode_t mode) {
	char buf[PATH_ROOM];
	snprintf(buf, sizeof buf, "%s", path);
	for (char *p = buf + 1; *p != '\0'; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(buf, 0755);
			*p = '/';
		}
	}
	mkdir(buf, mode);
}

static void remove_tree(const char *path) {
	char *paths[] = {(char *)path, NULL};
	FTS *walk = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR | FTS_XDEV, NULL);
	if (walk == NULL) {
		return;
	}
	FTSENT *ent;
	while ((ent = fts_read(walk)) != NULL) {
		switch (ent->fts_info) {
		case FTS_D:
			/* Make sure it can be emptied, whatever mode it arrived with. */
			chmod(ent->fts_accpath, 0700);
			break;
		case FTS_DP:
			rmdir(ent->fts_accpath);
			break;
		default:
			unlink(ent->fts_accpath);
			break;
		}
	}
	fts_close(walk);
}

static unsigned long text_hash(const char *text) {
	unsigned long h = 5381;
	for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
		h = h * 33 + *p;
	}
	return h & 0xFFFFFFFFul;
}

static void leaf_of(const char *path, char *out, size_t n) {
	const char *slash = strrchr(path, '/');
	snprintf(out, n, "%s", slash != NULL ? slash + 1 : path);
}

static void first_component(const char *path, char *out, size_t n) {
	size_t len = strcspn(path, "/");
	if (len >= n) {
		len = n - 1;
	}
	memcpy(out, path, len);
	out[len] = '\0';
}

/* Copies an attribute value out of one XML tag. */
static int xml_attr(const char *tag, const char *tag_end, const char *name, char *out, size_t n) {
	char pattern[64];
	int plen = snprintf(pattern, sizeof pattern, " %s=\"", name);
	const char *at = memmem(tag, (size_t)(tag_end - tag), pattern, (size_t)plen);
	if (at == NULL) {
		return -1;
	}
	at += plen;
	const char *quote = memchr(at, '"', (size_t)(tag_end - at));
	if (quote == NULL) {
		return -1;
	}
	size_t k = 0;
	while (at < quote && k + 1 < n) {
		if (strncmp(at, "&amp;", 5) == 0) {
			out[k++] = '&';
			at += 5;
		} else if ((unsigned char)*at < 0x20) {
			at++;
		} else {
			out[k++] = *at++;
		}
	}
	out[k] = '\0';
	return 0;
}

static char *read_small(const char *path, size_t *length) {
	int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) {
		return NULL;
	}
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > 2 * 1024 * 1024) {
		close(fd);
		return NULL;
	}
	char *data = malloc((size_t)st.st_size + 1);
	if (data == NULL) {
		close(fd);
		return NULL;
	}
	ssize_t got = read_full(fd, (unsigned char *)data, (size_t)st.st_size);
	close(fd);
	if (got < 0) {
		free(data);
		return NULL;
	}
	data[got] = '\0';
	*length = (size_t)got;
	return data;
}

static void fill_app(struct mac_app_info *app, const struct mac_bundle *b, const char *bundle) {
	memset(app, 0, sizeof *app);
	snprintf(app->name, sizeof app->name, "%s", b->name);
	snprintf(app->version, sizeof app->version, "%s", b->version);
	snprintf(app->identifier, sizeof app->identifier, "%s", b->identifier);
	snprintf(app->bundle, sizeof app->bundle, "%s", bundle);
	snprintf(app->program, sizeof app->program, "%s", b->program);
	mac_archs_text(b->archs, b->arch_count, app->archs, sizeof app->archs);
	app->program_kind = b->program_kind;
	app->runs_on_this_cpu = b->program_kind == MAC_PROGRAM_MACHO && mac_host_can_run(b->archs, b->arch_count);
}

/* ------------------------------------------------------------- review */

/* Finds the archive that really holds the applications: the file itself, or
 * a volume inside it that the archiver lists as a file. That volume is taken
 * out into work_dir, and inner says where. */
static int locate(const char *path, const char *work_dir, struct found *f, char *inner, size_t n,
		char *errtext, size_t errlen) {
	inner[0] = '\0';
	if (list_archive(path, f, errtext, errlen) != 0) {
		return -1;
	}
	if (f->bundle_count > 0 || f->nested_count == 0 || f->payload_count > 0) {
		return 0;
	}
	struct found outer = *f;
	for (int i = 0; i < outer.nested_count; i++) {
		char one[1][1024];
		snprintf(one[0], sizeof one[0], "%s", outer.nested[i]);
		if (extract_paths(path, work_dir, one, 1, errtext, errlen) != 0) {
			continue;
		}
		char candidate[PATH_ROOM];
		snprintf(candidate, sizeof candidate, "%s/%s", work_dir, outer.nested[i]);
		struct found deeper;
		if (list_archive(candidate, &deeper, errtext, errlen) == 0 && deeper.bundle_count > 0) {
			*f = deeper;
			snprintf(inner, n, "%s", candidate);
			return 0;
		}
		unlink(candidate);
	}
	*f = outer;
	return 0;
}

static void review_dmg(const char *path, const char *cache_dir, struct mac_review *out) {
	struct found f;
	char errtext[400];
	char inner[PATH_ROOM];
	if (mac_archiver() == NULL) {
		snprintf(out->note, sizeof out->note,
			"7-Zip is not installed on this system, so a Mac disk image cannot be opened here.");
		return;
	}
	if (locate(path, cache_dir, &f, inner, sizeof inner, errtext, sizeof errtext) != 0) {
		/* What the archiver printed goes to the log. The person gets a sentence. */
		if (errtext[0] != '\0') {
			fprintf(stderr, "dev-install: %s\n", errtext);
		}
		snprintf(out->note, sizeof out->note, strstr(errtext, "confined") != NULL || strstr(errtext, "nobody") != NULL
			? "This disk image was not read: %.200s."
			: "This disk image could not be read. It may be encrypted, damaged, or in a form this desktop does not open.%.0s",
			errtext);
		return;
	}
	const char *archive = inner[0] != '\0' ? inner : path;
	if (f.bundle_count == 0) {
		snprintf(out->note, sizeof out->note, f.entries > 0
			? "This disk image holds no application. Only applications can be installed from it."
			: "This disk image is empty or is not in a form this desktop reads.");
		if (inner[0] != '\0') {
			unlink(inner);
		}
		return;
	}
	first_component(f.bundles[0], out->title, sizeof out->title);
	if (ends_with(out->title, ".app")) {
		out->title[0] = '\0';
	}
	for (int i = 0; i < f.bundle_count; i++) {
		struct mac_app_info *app = &out->apps[out->app_count];
		char want[2][1024];
		char dir[PATH_ROOM];
		snprintf(dir, sizeof dir, "%s/%d", cache_dir, i);
		mkdirs(dir, 0700);
		snprintf(want[0], sizeof want[0], "%.1000s/Contents/Info.plist", f.bundles[i]);
		char bundle_dir[PATH_ROOM + 1100];
		snprintf(bundle_dir, sizeof bundle_dir, "%s/%s", dir, f.bundles[i]);
		struct mac_bundle b;
		if (extract_paths(archive, dir, want, 1, NULL, 0) != 0 || mac_bundle_read(bundle_dir, &b) != 0) {
			/* Listed, but its Info.plist would not come out. Name it by its folder. */
			memset(app, 0, sizeof *app);
			leaf_of(f.bundles[i], app->name, sizeof app->name);
			size_t len = strlen(app->name);
			if (len > 4) {
				app->name[len - 4] = '\0';
			}
			snprintf(app->bundle, sizeof app->bundle, "%s", f.bundles[i]);
			out->app_count++;
			continue;
		}
		/* The icon is named in the Info.plist. Take it out for the first one. */
		if (i == 0) {
			char plist_path[PATH_ROOM + 1200];
			snprintf(plist_path, sizeof plist_path, "%s/Contents/Info.plist", bundle_dir);
			size_t length = 0;
			char *plist = read_small(plist_path, &length);
			char icon[160] = "";
			if (plist != NULL) {
				mac_plist_string((unsigned char *)plist, length, "CFBundleIconFile", icon, sizeof icon);
				free(plist);
			}
			if (icon[0] != '\0' && strchr(icon, '/') == NULL) {
				snprintf(want[0], sizeof want[0], "%.800s/Contents/Resources/%.150s", f.bundles[i], icon);
				snprintf(want[1], sizeof want[1], "%.800s/Contents/Resources/%.150s.icns", f.bundles[i], icon);
				extract_paths(archive, dir, want, 2, NULL, 0);
				mac_bundle_read(bundle_dir, &b);
			}
		}
		fill_app(app, &b, f.bundles[i]);
		app->program[0] = '\0';
		if (b.icon_path[0] != '\0') {
			char png[PATH_ROOM];
			char icns[PATH_ROOM];
			snprintf(png, sizeof png, "%s/icon-%d.png", cache_dir, i);
			snprintf(icns, sizeof icns, "%s/icon-%d.icns", cache_dir, i);
			int made = mac_icon_export(b.icon_path, png, icns);
			if (made != 0) {
				snprintf(app->icon, sizeof app->icon, "%s", made == 1 ? png : icns);
			}
		}
		out->app_count++;
	}
	if (inner[0] != '\0') {
		/* The inner volume can be large. It is taken out again to install. */
		unlink(inner);
	}
}

struct name_scan {
	struct found *found;
};

static void payload_name(void *ctx, char *text) {
	struct name_scan *scan = ctx;
	const char *path = text;
	while (path[0] == '.' && path[1] == '/') {
		path += 2;
	}
	char root[1024];
	if (app_root(path, root, sizeof root) &&
			strcmp(path + strlen(root), "/Contents/Info.plist") == 0) {
		add_text(scan->found->bundles[0], &scan->found->bundle_count, MAC_APPS_MAX,
			sizeof scan->found->bundles[0], root);
	}
}

static void review_pkg(const char *path, const char *cache_dir, struct mac_review *out) {
	struct found f;
	char errtext[400];
	if (mac_archiver() == NULL) {
		snprintf(out->note, sizeof out->note,
			"7-Zip is not installed on this system, so a Mac package cannot be opened here.");
		return;
	}
	if (list_archive(path, &f, errtext, sizeof errtext) != 0) {
		if (errtext[0] != '\0') {
			fprintf(stderr, "dev-install: %s\n", errtext);
		}
		snprintf(out->note, sizeof out->note, strstr(errtext, "confined") != NULL || strstr(errtext, "nobody") != NULL
			? "This package was not read: %.200s."
			: "This package could not be read. It may be damaged or cut short.%.0s", errtext);
		return;
	}
	out->has_scripts = f.has_scripts;
	/* The small descriptions first: the title and what each part installs. */
	char want[MAC_APPS_MAX * 2][1024];
	int count = 0;
	if (f.distribution[0] != '\0') {
		snprintf(want[count++], sizeof want[0], "%s", f.distribution);
	}
	for (int i = 0; i < f.info_count && count < MAC_APPS_MAX * 2; i++) {
		snprintf(want[count++], sizeof want[0], "%s", f.infos[i]);
	}
	if (count > 0) {
		extract_paths(path, cache_dir, want, count, NULL, 0);
	}
	if (f.distribution[0] != '\0') {
		char file[PATH_ROOM];
		snprintf(file, sizeof file, "%s/%s", cache_dir, f.distribution);
		size_t length = 0;
		char *text = read_small(file, &length);
		if (text != NULL) {
			const char *open = strstr(text, "<title>");
			const char *close = open != NULL ? strstr(open, "</title>") : NULL;
			if (close != NULL && (size_t)(close - open - 7) < sizeof out->title) {
				memcpy(out->title, open + 7, (size_t)(close - open - 7));
				out->title[close - open - 7] = '\0';
			}
			free(text);
		}
	}
	struct found apps;
	memset(&apps, 0, sizeof apps);
	char versions[MAC_APPS_MAX][48];
	memset(versions, 0, sizeof versions);
	for (int i = 0; i < f.info_count; i++) {
		char file[PATH_ROOM];
		snprintf(file, sizeof file, "%s/%s", cache_dir, f.infos[i]);
		size_t length = 0;
		char *text = read_small(file, &length);
		if (text == NULL) {
			continue;
		}
		if (strstr(text, "<scripts") != NULL) {
			out->has_scripts = 1;
		}
		const char *at = text;
		while ((at = strstr(at, "<bundle ")) != NULL) {
			const char *end = strchr(at, '>');
			if (end == NULL) {
				break;
			}
			char bundle[1024];
			if (xml_attr(at, end, "path", bundle, sizeof bundle) == 0) {
				const char *p = bundle;
				while (p[0] == '.' && p[1] == '/') {
					p += 2;
				}
				char root[1024];
				/* The application itself, not a helper tucked inside one. */
				if (app_root(p, root, sizeof root) && strcmp(root, p) == 0) {
					int before = apps.bundle_count;
					add_text(apps.bundles[0], &apps.bundle_count, MAC_APPS_MAX, sizeof apps.bundles[0], root);
					if (apps.bundle_count > before) {
						xml_attr(at, end, "CFBundleShortVersionString", versions[before], sizeof versions[0]);
					}
				}
			}
			at = end;
		}
		free(text);
	}
	if (apps.bundle_count == 0) {
		/* An older package does not say. Read the names in each payload. */
		struct name_scan scan = {.found = &apps};
		for (int i = 0; i < f.payload_count; i++) {
			run_payload(path, f.payloads[i], NULL, payload_name, &scan, errtext, sizeof errtext);
		}
	}
	if (out->title[0] == '\0') {
		leaf_of(path, out->title, sizeof out->title);
	}
	if (apps.bundle_count == 0) {
		snprintf(out->note, sizeof out->note, f.payload_count > 0
			? "This package installs files and no application. A package like that changes a Mac system, and there is no Mac system here to change."
			: "This package has no payload this desktop can read.");
		return;
	}
	for (int i = 0; i < apps.bundle_count; i++) {
		struct mac_app_info *app = &out->apps[out->app_count++];
		memset(app, 0, sizeof *app);
		leaf_of(apps.bundles[i], app->name, sizeof app->name);
		size_t len = strlen(app->name);
		if (len > 4) {
			app->name[len - 4] = '\0';
		}
		snprintf(app->version, sizeof app->version, "%s", versions[i]);
		snprintf(app->bundle, sizeof app->bundle, "%s", apps.bundles[i]);
	}
}

static void review_app(const char *path, const char *cache_dir, struct mac_review *out) {
	struct mac_bundle b;
	if (mac_bundle_read(path, &b) != 0) {
		snprintf(out->note, sizeof out->note, "This folder is named like an application and has no readable Info.plist.");
		return;
	}
	struct mac_app_info *app = &out->apps[0];
	fill_app(app, &b, path);
	app->program[0] = '\0';
	if (b.icon_path[0] != '\0') {
		char png[PATH_ROOM];
		char icns[PATH_ROOM];
		snprintf(png, sizeof png, "%s/icon-0.png", cache_dir);
		snprintf(icns, sizeof icns, "%s/icon-0.icns", cache_dir);
		int made = mac_icon_export(b.icon_path, png, icns);
		if (made != 0) {
			snprintf(app->icon, sizeof app->icon, "%s", made == 1 ? png : icns);
		}
	}
	out->app_count = 1;
	snprintf(out->title, sizeof out->title, "%s", b.name);
}

int mac_review_file(const char *path, const char *cache_dir, struct mac_review *out) {
	memset(out, 0, sizeof *out);
	const char *kind = mac_detect(path);
	if (kind == NULL) {
		return -1;
	}
	snprintf(out->kind, sizeof out->kind, "%s", kind);
	remove_tree(cache_dir);
	/* Open to others to pass through: where the unpacker runs as the user
	 * "nobody" it has to reach the folder that was handed to it. */
	mkdirs(cache_dir, 0755);
	if (strcmp(kind, "mac_dmg") == 0) {
		review_dmg(path, cache_dir, out);
	} else if (strcmp(kind, "mac_pkg") == 0) {
		review_pkg(path, cache_dir, out);
	} else {
		review_app(path, cache_dir, out);
	}
	return 0;
}

/* ------------------------------------------------------------ install */

/* Makes one unpacked application safe to keep: its links stay inside it,
 * nothing in it is a device or a pipe, and nothing in it is set-user-ID. */
static void make_safe(const char *bundle, int *links_removed) {
	char *paths[] = {(char *)bundle, NULL};
	FTS *walk = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR | FTS_XDEV, NULL);
	if (walk == NULL) {
		return;
	}
	uid_t uid = getuid();
	gid_t gid = getgid();
	size_t root_len = strlen(bundle);
	FTSENT *ent;
	while ((ent = fts_read(walk)) != NULL) {
		if (geteuid() == 0 && ent->fts_info != FTS_DP) {
			if (lchown(ent->fts_accpath, uid, gid) != 0) {
				/* Left as it arrived. */
			}
		}
		if (ent->fts_info == FTS_SL || ent->fts_info == FTS_SLNONE) {
			char target[PATH_ROOM];
			ssize_t len = readlink(ent->fts_accpath, target, sizeof target - 1);
			int inside = len > 0 && target[0] != '/';
			if (inside) {
				target[len] = '\0';
				/* How many folders deep the link sits below the application. */
				int depth = 0;
				for (const char *p = ent->fts_accpath + root_len; *p != '\0'; p++) {
					if (*p == '/') {
						depth++;
					}
				}
				depth -= 1;
				char *save = NULL;
				for (char *part = strtok_r(target, "/", &save); part != NULL;
						part = strtok_r(NULL, "/", &save)) {
					if (strcmp(part, "..") == 0) {
						depth--;
						if (depth < 0) {
							inside = 0;
							break;
						}
					} else if (strcmp(part, ".") != 0) {
						depth++;
					}
				}
			}
			if (!inside) {
				unlink(ent->fts_accpath);
				(*links_removed)++;
			}
		} else if (ent->fts_info == FTS_F) {
			chmod(ent->fts_accpath, (ent->fts_statp->st_mode & 0777) | 0600);
		} else if (ent->fts_info == FTS_D) {
			chmod(ent->fts_accpath, (ent->fts_statp->st_mode & 0777) | 0700);
		} else if (ent->fts_info == FTS_DEFAULT) {
			unlink(ent->fts_accpath);
		}
	}
	fts_close(walk);
}

/* Applications under dir, not looking inside one for another. */
static void find_bundles(const char *dir, int depth, char found[][PATH_ROOM], int *count, int max) {
	if (depth > 8 || *count >= max) {
		return;
	}
	DIR *d = opendir(dir);
	if (d == NULL) {
		return;
	}
	struct dirent *ent;
	while ((ent = readdir(d)) != NULL && *count < max) {
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
			continue;
		}
		char path[PATH_ROOM];
		if (snprintf(path, sizeof path, "%s/%s", dir, ent->d_name) >= (int)sizeof path) {
			continue;
		}
		struct stat st;
		if (lstat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
			continue;
		}
		if (ends_with(ent->d_name, ".app")) {
			if (mac_is_bundle_dir(path)) {
				snprintf(found[*count], PATH_ROOM, "%s", path);
				(*count)++;
			}
			continue;
		}
		find_bundles(path, depth + 1, found, count, max);
	}
	closedir(d);
}

/* Stage folders left by an install that was stopped half way. */
static void clear_stale_stages(const char *mac_dir) {
	DIR *d = opendir(mac_dir);
	if (d == NULL) {
		return;
	}
	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		long pid = 0;
		if (sscanf(ent->d_name, ".stage-%ld", &pid) == 1 || sscanf(ent->d_name, ".old-%ld", &pid) == 1) {
			if (pid > 0 && (kill((pid_t)pid, 0) == 0 || errno == EPERM)) {
				continue;
			}
			char path[PATH_ROOM];
			snprintf(path, sizeof path, "%s/%s", mac_dir, ent->d_name);
			remove_tree(path);
		}
	}
	closedir(d);
}

void mac_clear_stale(const char *desk_root) {
	char mac_dir[PATH_ROOM];
	snprintf(mac_dir, sizeof mac_dir, "%s/mac", desk_root);
	clear_stale_stages(mac_dir);
}

static void no_progress(int step, int of, const char *what) {
	(void)step;
	(void)of;
	(void)what;
}

int mac_install_file(const char *path, const char *desk_root, struct mac_app_info *apps, int max,
		char *err, size_t errlen, void (*progress)(int step, int of, const char *what)) {
	if (progress == NULL) {
		progress = no_progress;
	}
	err[0] = '\0';
	const char *kind = mac_detect(path);
	if (kind == NULL) {
		snprintf(err, errlen, "not a Mac disk image, package, or application");
		return -1;
	}
	char mac_dir[PATH_ROOM];
	char apps_dir[PATH_ROOM];
	char icons_dir[PATH_ROOM];
	char stage[PATH_ROOM];
	snprintf(mac_dir, sizeof mac_dir, "%s/mac", desk_root);
	snprintf(apps_dir, sizeof apps_dir, "%s/Applications", mac_dir);
	snprintf(icons_dir, sizeof icons_dir, "%s/icons", desk_root);
	snprintf(stage, sizeof stage, "%s/.stage-%ld", mac_dir, (long)getpid());
	mkdirs(apps_dir, 0755);
	mkdirs(icons_dir, 0755);
	clear_stale_stages(mac_dir);
	remove_tree(stage);
	if (mkdir(stage, 0700) != 0) {
		snprintf(err, errlen, "could not create a staging folder under %s", mac_dir);
		return -1;
	}
	char errtext[400] = "";
	int failed = 0;
	progress(1, 5, "read");
	/* For the test of Stop: a pause here, with the staging folder already
	 * made, gives it a moment in which to stop the install. */
	const char *pause = getenv("OLYMPIUM_INSTALL_PAUSE_MS");
	if (pause != NULL && atoi(pause) > 0 && atoi(pause) <= 10000) {
		usleep((useconds_t)atoi(pause) * 1000);
	}
	if (strcmp(kind, "mac_dmg") == 0) {
		struct found f;
		char inner[PATH_ROOM];
		if (locate(path, stage, &f, inner, sizeof inner, errtext, sizeof errtext) != 0) {
			snprintf(err, errlen, "the disk image could not be read: %.200s", errtext);
			failed = 1;
		} else if (f.bundle_count == 0) {
			snprintf(err, errlen, "the disk image holds no application");
			failed = 1;
		} else {
			/* Room for it, with a little to spare, before anything is written. */
			struct statvfs vfs;
			if (statvfs(mac_dir, &vfs) == 0) {
				unsigned long long room = (unsigned long long)vfs.f_bavail * vfs.f_frsize;
				unsigned long long need = f.bundle_bytes + f.bundle_bytes / 10 + (4ull << 20);
				if (room < need) {
					snprintf(err, errlen, "not enough room: %llu MB are needed and %llu MB are free",
						need >> 20, room >> 20);
					failed = 1;
				}
			}
			if (!failed) {
				progress(2, 5, "unpack");
				if (extract_paths(inner[0] != '\0' ? inner : path, stage, f.bundles, f.bundle_count,
						errtext, sizeof errtext) != 0) {
					snprintf(err, errlen, "the disk image could not be unpacked: %.200s", errtext);
					failed = 1;
				}
			}
		}
		if (inner[0] != '\0') {
			unlink(inner);
		}
	} else if (strcmp(kind, "mac_pkg") == 0) {
		struct found f;
		if (list_archive(path, &f, errtext, sizeof errtext) != 0) {
			snprintf(err, errlen, "the package could not be read: %.200s", errtext);
			failed = 1;
		} else if (f.payload_count == 0) {
			snprintf(err, errlen, "the package has no payload");
			failed = 1;
		}
		progress(2, 5, "unpack");
		for (int i = 0; !failed && i < f.payload_count; i++) {
			char part[PATH_ROOM];
			snprintf(part, sizeof part, "%s/p%d", stage, i);
			mkdir(part, 0700);
			if (run_payload(path, f.payloads[i], part, NULL, NULL, errtext, sizeof errtext) < 0) {
				snprintf(err, errlen, "the package could not be unpacked: %.200s", errtext);
				failed = 1;
			}
		}
	} else {
		const char *cp = cp_tool();
		progress(2, 5, "copy");
		if (cp == NULL) {
			snprintf(err, errlen, "cp is not installed");
			failed = 1;
		} else {
			char *argv[] = {(char *)cp, "-a", "--", (char *)path, stage, NULL};
			struct child spec = {.argv = argv, .read_path = path, .write_dir = stage};
			dev_confine_prepare_dir(stage);
			int status = run_lines(&spec, NULL, NULL, errtext, sizeof errtext);
			if (status != 0) {
				if (status == 125) {
					snprintf(err, errlen, "%s", confine_failure());
				} else {
					snprintf(err, errlen, "the application could not be copied: %.200s", errtext);
				}
				failed = 1;
			}
		}
	}
	int installed = 0;
	if (!failed) {
		progress(3, 5, "check");
		static char found[MAC_APPS_MAX][PATH_ROOM];
		int count = 0;
		find_bundles(stage, 0, found, &count, MAC_APPS_MAX);
		if (count == 0) {
			snprintf(err, errlen, "nothing that was unpacked is an application");
			failed = 1;
		}
		progress(4, 5, "install");
		for (int i = 0; !failed && i < count && installed < max; i++) {
			int links_removed = 0;
			make_safe(found[i], &links_removed);
			struct mac_bundle b;
			if (mac_bundle_read(found[i], &b) != 0 || b.program_kind == MAC_PROGRAM_MISSING) {
				/* A folder with the name and no program in it. */
				continue;
			}
			char leaf[300];
			leaf_of(found[i], leaf, sizeof leaf);
			char dest[PATH_ROOM];
			char old[PATH_ROOM];
			snprintf(dest, sizeof dest, "%s/%s", apps_dir, leaf);
			snprintf(old, sizeof old, "%s/.old-%ld", mac_dir, (long)getpid());
			remove_tree(old);
			struct stat st;
			int had = lstat(dest, &st) == 0;
			if (had && rename(dest, old) != 0) {
				snprintf(err, errlen, "the copy already in Applications could not be replaced");
				failed = 1;
				break;
			}
			if (rename(found[i], dest) != 0) {
				/* Put the earlier copy back. Nothing is left half done. */
				if (had) {
					rename(old, dest);
				}
				snprintf(err, errlen, "the application could not be moved into Applications");
				failed = 1;
				break;
			}
			if (had) {
				remove_tree(old);
			}
			mac_bundle_read(dest, &b);
			struct mac_app_info *app = &apps[installed];
			fill_app(app, &b, dest);
			if (b.icon_path[0] != '\0') {
				char png[PATH_ROOM];
				char icns[PATH_ROOM];
				unsigned long key = text_hash(dest);
				snprintf(png, sizeof png, "%s/mac-%08lx.png", icons_dir, key);
				snprintf(icns, sizeof icns, "%s/mac-%08lx.icns", icons_dir, key);
				unlink(png);
				unlink(icns);
				int made = mac_icon_export(b.icon_path, png, icns);
				if (made != 0) {
					snprintf(app->icon, sizeof app->icon, "%s", made == 1 ? png : icns);
				}
			}
			if (links_removed > 0) {
				fprintf(stderr, "dev-install: %d link%s that pointed out of %s %s removed\n",
					links_removed, links_removed == 1 ? "" : "s", app->name,
					links_removed == 1 ? "was" : "were");
			}
			installed++;
		}
		if (!failed && installed == 0) {
			snprintf(err, errlen, "the application has no program in it");
			failed = 1;
		}
	}
	remove_tree(stage);
	progress(5, 5, "done");
	return failed ? -1 : installed;
}
