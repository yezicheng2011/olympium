/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 */
#define _GNU_SOURCE
#include "macicon.h"

#include "macbundle.h"
#include "sandbox.h"

#include <errno.h>
#include <fcntl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

enum {
	ICON_FILE_MAX = 64 * 1024 * 1024,
	PIXELS = MAC_ICON_SIZE * MAC_ICON_SIZE * 4
};

static unsigned char *read_icon(const char *path, size_t *length) {
	int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) {
		return NULL;
	}
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 16 || st.st_size > ICON_FILE_MAX) {
		close(fd);
		return NULL;
	}
	unsigned char *data = malloc((size_t)st.st_size);
	size_t got = 0;
	while (data != NULL && got < (size_t)st.st_size) {
		ssize_t n = read(fd, data + got, (size_t)st.st_size - got);
		if (n <= 0) {
			break;
		}
		got += (size_t)n;
	}
	close(fd);
	if (data == NULL || got != (size_t)st.st_size) {
		free(data);
		return NULL;
	}
	*length = got;
	return data;
}

/* Runs in the child. Decodes, fits the picture into the square, and writes
 * exactly PIXELS bytes of red, green, blue, alpha. */
static int decode_to_pixels(const unsigned char *data, size_t length, int out_fd) {
	GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
	size_t at = 0;
	size_t png = 0;
	gboolean fed;
	if (mac_icns_png(data, length, &at, &png) == 0) {
		fed = gdk_pixbuf_loader_write(loader, data + at, png, NULL);
	} else {
		/* An older .icns with no PNG inside. The loader reads those. */
		fed = gdk_pixbuf_loader_write(loader, data, length, NULL);
	}
	if (!gdk_pixbuf_loader_close(loader, NULL) || !fed) {
		return 1;
	}
	GdkPixbuf *picture = gdk_pixbuf_loader_get_pixbuf(loader);
	if (picture == NULL) {
		return 1;
	}
	int w = gdk_pixbuf_get_width(picture);
	int h = gdk_pixbuf_get_height(picture);
	if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
		return 1;
	}
	GdkPixbuf *square = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, MAC_ICON_SIZE, MAC_ICON_SIZE);
	if (square == NULL) {
		return 1;
	}
	gdk_pixbuf_fill(square, 0);
	double scale = (double)MAC_ICON_SIZE / (w > h ? w : h);
	int dw = (int)(w * scale + 0.5);
	int dh = (int)(h * scale + 0.5);
	if (dw < 1) {
		dw = 1;
	}
	if (dh < 1) {
		dh = 1;
	}
	int ox = (MAC_ICON_SIZE - dw) / 2;
	int oy = (MAC_ICON_SIZE - dh) / 2;
	gdk_pixbuf_scale(picture, square, ox, oy, dw, dh, ox, oy, scale, scale, GDK_INTERP_BILINEAR);
	const guchar *pixels = gdk_pixbuf_get_pixels(square);
	int stride = gdk_pixbuf_get_rowstride(square);
	for (int y = 0; y < MAC_ICON_SIZE; y++) {
		const guchar *row = pixels + (size_t)y * stride;
		size_t done = 0;
		while (done < MAC_ICON_SIZE * 4) {
			ssize_t n = write(out_fd, row + done, MAC_ICON_SIZE * 4 - done);
			if (n <= 0) {
				return 1;
			}
			done += (size_t)n;
		}
	}
	return 0;
}

int mac_icon_render(const char *icns_path, const char *png_out) {
	size_t length = 0;
	unsigned char *data = read_icon(icns_path, &length);
	if (data == NULL) {
		return -1;
	}
	int fds[2];
	if (pipe2(fds, O_CLOEXEC) != 0) {
		free(data);
		return -1;
	}
	pid_t pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		free(data);
		return -1;
	}
	if (pid == 0) {
		/* The bytes are already in memory, so this child needs no file. It
		 * gets one pipe to write pixels to, a memory limit, and ten seconds. */
		int out = dup(fds[1]);
		int nul = open("/dev/null", O_RDWR);
		dup2(nul, 0);
		dup2(nul, 1);
		dup2(nul, 2);
		if (out != 3) {
			dup2(out, 3);
		}
		/* Nothing else stays open: not the display, not a socket. */
		int closed = -1;
#ifdef SYS_close_range
		closed = (int)syscall(SYS_close_range, 4, ~0U, 0);
#endif
		if (closed != 0) {
			for (int fd = 4; fd < 4096; fd++) {
				close(fd);
			}
		}
		signal(SIGPIPE, SIG_DFL);
#if !defined(__SANITIZE_ADDRESS__)
		/* A build with the address sanitizer reserves terabytes of address
		 * space for itself and cannot start under this limit, so that build
		 * goes without it. It still has the ten seconds and the confinement. */
		struct rlimit memory = {.rlim_cur = 768u * 1024 * 1024, .rlim_max = 768u * 1024 * 1024};
		setrlimit(RLIMIT_AS, &memory);
#endif
		alarm(10);
		if (dev_confine_self(NULL, NULL) < 0) {
			_exit(125);
		}
		_exit(decode_to_pixels(data, length, 3));
	}
	close(fds[1]);
	free(data);
	unsigned char *pixels = malloc(PIXELS);
	size_t got = 0;
	while (pixels != NULL && got < PIXELS) {
		ssize_t n = read(fds[0], pixels + got, PIXELS - got);
		if (n < 0 && errno == EINTR) {
			continue;
		}
		if (n <= 0) {
			break;
		}
		got += (size_t)n;
	}
	/* Anything more than the square is not a picture and is not read. */
	close(fds[0]);
	int status = 0;
	waitpid(pid, &status, 0);
	if (pixels == NULL || got != PIXELS || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		free(pixels);
		return -1;
	}
	/* From here on the bytes are only colours. This process draws the file. */
	GdkPixbuf *picture = gdk_pixbuf_new_from_data(pixels, GDK_COLORSPACE_RGB, TRUE, 8,
		MAC_ICON_SIZE, MAC_ICON_SIZE, MAC_ICON_SIZE * 4, NULL, NULL);
	int result = -1;
	if (picture != NULL) {
		char tmp[1100];
		snprintf(tmp, sizeof tmp, "%s.tmp%ld", png_out, (long)getpid());
		if (gdk_pixbuf_save(picture, tmp, "png", NULL, NULL) && rename(tmp, png_out) == 0) {
			result = 0;
		} else {
			unlink(tmp);
		}
		g_object_unref(picture);
	}
	free(pixels);
	return result;
}
