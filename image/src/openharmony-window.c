/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Render an OpenHarmony SDK app region in a native GTK3/Wayland window.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

struct app_state {
	const char *qmp_path;
	const char *hdc_path;
	const char *target;
	const char *reference_path;
	int x, y, width, height;
	int display_w, display_h;
	int qmp_fd;
	char frame_path[512];
	char tmp_dir[512];
	GtkWidget *window;
	GtkWidget *picture;
	guchar *last_pixels;
	size_t last_len;
	int close_requested;
	int failed;
	char error_msg[256];
	pthread_mutex_t qmp_lock;
	int click_busy;
};

static int qmp_write_all(int fd, const char *buf, size_t len) {
	size_t done = 0;
	while (done < len) {
		ssize_t n = send(fd, buf + done, len - done, MSG_NOSIGNAL);
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

static int qmp_readline(int fd, char *out, size_t n) {
	size_t len = 0;
	while (len + 1 < n) {
		char c = 0;
		ssize_t r = recv(fd, &c, 1, 0);
		if (r < 0 && errno == EINTR) {
			continue;
		}
		if (r <= 0) {
			return -1;
		}
		out[len++] = c;
		if (c == '\n') {
			break;
		}
	}
	out[len] = '\0';
	return 0;
}

static int qmp_command(int fd, const char *json_line, char *err, size_t err_n) {
	if (qmp_write_all(fd, json_line, strlen(json_line)) != 0) {
		snprintf(err, err_n, "QEMU monitor write failed");
		return -1;
	}
	char line[4096];
	for (;;) {
		if (qmp_readline(fd, line, sizeof line) != 0) {
			snprintf(err, err_n, "QEMU monitor disconnected");
			return -1;
		}
		if (strstr(line, "\"error\"") != NULL) {
			snprintf(err, err_n, "%.240s", line);
			return -1;
		}
		if (strstr(line, "\"return\"") != NULL) {
			return 0;
		}
	}
}

struct click_job {
	struct app_state *st;
	int gx;
	int gy;
};

static void *click_thread(void *arg) {
	struct click_job *job = arg;
	struct app_state *st = job->st;
	char xs[32], ys[32];
	snprintf(xs, sizeof xs, "%d", job->gx);
	snprintf(ys, sizeof ys, "%d", job->gy);
	int fds[2];
	int ok = 0;
	char out[512] = "";
	if (pipe2(fds, O_CLOEXEC) == 0) {
		pid_t pid = fork();
		if (pid == 0) {
			dup2(fds[1], 1);
			dup2(fds[1], 2);
			close(fds[0]);
			close(fds[1]);
			execl(st->hdc_path, st->hdc_path, "-t", st->target,
				"shell", "uitest", "uiInput", "click", xs, ys, (char *)NULL);
			_exit(127);
		}
		close(fds[1]);
		if (pid > 0) {
			size_t got = 0;
			while (got + 1 < sizeof out) {
				ssize_t n = read(fds[0], out + got, sizeof out - 1 - got);
				if (n < 0 && errno == EINTR) {
					continue;
				}
				if (n <= 0) {
					break;
				}
				got += (size_t)n;
			}
			out[got] = '\0';
			int status = 0;
			while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
			}
			if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && strstr(out, "No Error") != NULL) {
				ok = 1;
			}
		}
		close(fds[0]);
	}
	if (ok) {
		printf("Guest pointer accepted\n");
		fflush(stdout);
	} else {
		st->failed = 1;
		snprintf(st->error_msg, sizeof st->error_msg, "Guest rejected pointer input: %.180s", out);
	}
	st->click_busy = 0;
	free(job);
	return NULL;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
	(void)widget;
	struct app_state *st = data;
	if (event->button == GDK_BUTTON_PRIMARY &&
			event->x >= 0 && event->x < st->display_w &&
			event->y >= 0 && event->y < st->display_h &&
			!st->click_busy) {
		struct click_job *job = calloc(1, sizeof *job);
		if (job != NULL) {
			job->st = st;
			job->gx = st->x + (int)(event->x * st->width / st->display_w);
			job->gy = st->y + (int)(event->y * st->height / st->display_h);
			printf("Wayland pointer -> guest (%d, %d)\n", job->gx, job->gy);
			fflush(stdout);
			st->click_busy = 1;
			pthread_t tid;
			if (pthread_create(&tid, NULL, click_thread, job) == 0) {
				pthread_detach(tid);
			} else {
				st->click_busy = 0;
				free(job);
			}
		}
	}
	return TRUE;
}

static gboolean on_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
	(void)widget;
	(void)event;
	struct app_state *st = data;
	st->close_requested = 1;
	return FALSE;
}

static gboolean refresh_frame(gpointer data) {
	struct app_state *st = data;
	if (st->failed) {
		gtk_main_quit();
		return G_SOURCE_REMOVE;
	}
	if (!gtk_widget_get_mapped(st->window)) {
		return G_SOURCE_CONTINUE;
	}
	char cmd[700];
	snprintf(cmd, sizeof cmd,
		"{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"%s\"}}\n",
		st->frame_path);
	pthread_mutex_lock(&st->qmp_lock);
	int rc = qmp_command(st->qmp_fd, cmd, st->error_msg, sizeof st->error_msg);
	pthread_mutex_unlock(&st->qmp_lock);
	if (rc != 0) {
		st->failed = 1;
		gtk_main_quit();
		return G_SOURCE_REMOVE;
	}
	GError *gerr = NULL;
	GdkPixbuf *full = gdk_pixbuf_new_from_file(st->frame_path, &gerr);
	if (full == NULL) {
		if (gerr != NULL) {
			snprintf(st->error_msg, sizeof st->error_msg, "%.240s", gerr->message);
			g_error_free(gerr);
		}
		st->failed = 1;
		gtk_main_quit();
		return G_SOURCE_REMOVE;
	}
	if (gdk_pixbuf_get_width(full) != 360 || gdk_pixbuf_get_height(full) != 720) {
		g_object_unref(full);
		snprintf(st->error_msg, sizeof st->error_msg, "Guest display size changed");
		st->failed = 1;
		gtk_main_quit();
		return G_SOURCE_REMOVE;
	}
	GdkPixbuf *cropped = gdk_pixbuf_new_subpixbuf(full, st->x, st->y, st->width, st->height);
	GdkPixbuf *scaled = gdk_pixbuf_scale_simple(cropped, st->display_w, st->display_h, GDK_INTERP_BILINEAR);
	g_object_unref(cropped);
	g_object_unref(full);
	if (scaled == NULL) {
		snprintf(st->error_msg, sizeof st->error_msg, "Failed to scale guest frame");
		st->failed = 1;
		gtk_main_quit();
		return G_SOURCE_REMOVE;
	}
	size_t len = (size_t)gdk_pixbuf_get_rowstride(scaled) * (size_t)gdk_pixbuf_get_height(scaled);
	const guchar *px = gdk_pixbuf_get_pixels(scaled);
	if (st->last_pixels == NULL || st->last_len != len || memcmp(st->last_pixels, px, len) != 0) {
		if (st->last_pixels == NULL && st->reference_path != NULL) {
			gdk_pixbuf_save(scaled, st->reference_path, "png", NULL, NULL);
		}
		gtk_image_set_from_pixbuf(GTK_IMAGE(st->picture), scaled);
		guchar *copy = realloc(st->last_pixels, len);
		if (copy != NULL) {
			memcpy(copy, px, len);
			st->last_pixels = copy;
			st->last_len = len;
		}
	}
	g_object_unref(scaled);
	return G_SOURCE_CONTINUE;
}

int main(int argc, char **argv) {
	signal(SIGPIPE, SIG_IGN);
	struct app_state st;
	memset(&st, 0, sizeof st);
	st.qmp_fd = -1;
	pthread_mutex_init(&st.qmp_lock, NULL);
	const char *title = "Calculator";
	int right = 0, bottom = 0, have_bounds = 0;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--qmp") == 0 && i + 1 < argc) {
			st.qmp_path = argv[++i];
		} else if (strcmp(argv[i], "--hdc") == 0 && i + 1 < argc) {
			st.hdc_path = argv[++i];
		} else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
			st.target = argv[++i];
		} else if (strcmp(argv[i], "--reference") == 0 && i + 1 < argc) {
			st.reference_path = argv[++i];
		} else if (strcmp(argv[i], "--title") == 0 && i + 1 < argc) {
			title = argv[++i];
		} else if (strcmp(argv[i], "--bounds") == 0 && i + 4 < argc) {
			st.x = atoi(argv[++i]);
			st.y = atoi(argv[++i]);
			right = atoi(argv[++i]);
			bottom = atoi(argv[++i]);
			have_bounds = 1;
		}
	}
	if (st.qmp_path == NULL || st.hdc_path == NULL || st.target == NULL || !have_bounds ||
			!(0 <= st.x && st.x < right && right <= 360 && 0 <= st.y && st.y < bottom && bottom <= 720)) {
		fprintf(stderr, "Usage: openharmony-window --qmp SOCK --hdc BIN --target HOST:PORT --bounds X Y R B [--title T] [--reference PNG]\n");
		return 2;
	}
	st.width = right - st.x;
	st.height = bottom - st.y;
	st.display_w = (st.width * 4) / 5;
	st.display_h = (st.height * 4) / 5;

	snprintf(st.tmp_dir, sizeof st.tmp_dir, "/tmp/olympium-oh-frame-XXXXXX");
	if (mkdtemp(st.tmp_dir) == NULL) {
		perror("mkdtemp");
		return 1;
	}
	snprintf(st.frame_path, sizeof st.frame_path, "%s/frame.ppm", st.tmp_dir);

	st.qmp_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (st.qmp_fd < 0) {
		perror("socket");
		return 1;
	}
	struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
	setsockopt(st.qmp_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
	setsockopt(st.qmp_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof addr.sun_path, "%s", st.qmp_path);
	if (connect(st.qmp_fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
		perror("connect");
		close(st.qmp_fd);
		return 1;
	}
	char greeting[4096];
	if (qmp_readline(st.qmp_fd, greeting, sizeof greeting) != 0 || strstr(greeting, "QMP") == NULL) {
		fprintf(stderr, "Missing QEMU monitor greeting\n");
		close(st.qmp_fd);
		return 1;
	}
	if (qmp_command(st.qmp_fd, "{\"execute\":\"qmp_capabilities\"}\n",
			st.error_msg, sizeof st.error_msg) != 0) {
		fprintf(stderr, "%s\n", st.error_msg);
		close(st.qmp_fd);
		return 1;
	}

	gtk_init(&argc, &argv);
	st.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(st.window), title);
	gtk_window_set_decorated(GTK_WINDOW(st.window), FALSE);
	gtk_window_set_resizable(GTK_WINDOW(st.window), FALSE);
	GtkWidget *surface = gtk_event_box_new();
	st.picture = gtk_image_new();
	gtk_widget_set_size_request(surface, st.display_w, st.display_h);
	gtk_container_add(GTK_CONTAINER(surface), st.picture);
	gtk_container_add(GTK_CONTAINER(st.window), surface);
	gtk_widget_add_events(surface, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
	g_signal_connect(surface, "button-press-event", G_CALLBACK(on_button_press), &st);
	g_signal_connect(st.window, "delete-event", G_CALLBACK(on_delete), &st);
	g_signal_connect(st.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	gtk_widget_show_all(st.window);

	guint timer = g_timeout_add(160, refresh_frame, &st);
	gtk_main();
	if (!st.failed) {
		g_source_remove(timer);
	}
	close(st.qmp_fd);
	unlink(st.frame_path);
	rmdir(st.tmp_dir);
	free(st.last_pixels);
	if (st.failed) {
		fprintf(stderr, "%s\n", st.error_msg);
		return 1;
	}
	if (!st.close_requested) {
		fprintf(stderr, "Window exited without a close request\n");
		return 1;
	}
	return 0;
}
