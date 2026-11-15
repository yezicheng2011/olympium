/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * Sample Linux application. The desktop installs the .deb that contains it
 * and opens this window. It is not an Android, Windows, or Mac program.
 */
#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static long since(const struct timespec *t0) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - t0->tv_sec) * 1000 + (now.tv_nsec - t0->tv_nsec) / 1000000;
}

static void record_ready(long ms) {
	const char *home = getenv("OLYMPIUM_DESK_HOME");
	if (home == NULL || home[0] == '\0') {
		home = "/tmp";
	}
	char cold[512];
	char warm[512];
	snprintf(cold, sizeof cold, "%s/cold-app-ms", home);
	snprintf(warm, sizeof warm, "%s/warm-app-ms", home);
	const char *path = cold;
	if (access(cold, F_OK) == 0) {
		path = warm;
	}
	FILE *f = fopen(path, "w");
	if (f != NULL) {
		fprintf(f, "%ld\n", ms);
		fclose(f);
	}
	printf("app_ready_ms %ld\n", ms);
	fflush(stdout);
}

static struct timespec g_t0;
static int g_recorded;

static void claim_clipboard(void) {
	GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text(clip, "Desk Note ready", -1);
}

static gboolean claim_when_running(gpointer data) {
	(void)data;
	claim_clipboard();
	if (!g_recorded) {
		record_ready(since(&g_t0));
		g_recorded = 1;
	}
	return FALSE;
}

static gboolean claim_on_focus_out(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
	(void)widget;
	(void)event;
	(void)data;
	claim_clipboard();
	return FALSE;
}

static gboolean quit_soon(gpointer data) {
	(void)data;
	gtk_main_quit();
	return FALSE;
}

int main(int argc, char **argv) {
	clock_gettime(CLOCK_MONOTONIC, &g_t0);
	int once = 0;
	const char *file = NULL;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--once") == 0) {
			once = 1;
		} else if (file == NULL) {
			file = argv[i];
		}
	}
	const char *home = getenv("OLYMPIUM_DESK_HOME");
	if (home == NULL || home[0] == '\0') {
		home = "/tmp";
	}
	if (file != NULL) {
		char opened[512];
		snprintf(opened, sizeof opened, "%s/opened-path", home);
		FILE *mark = fopen(opened, "w");
		if (mark != NULL) {
			fprintf(mark, "%s\n", file);
			fclose(mark);
		}
	}
	char exchange[512];
	snprintf(exchange, sizeof exchange, "%s/exchange", home);
	mkdir(exchange, 0755);
	char note[512];
	snprintf(note, sizeof note, "%s/desk-note.txt", exchange);
	FILE *out = fopen(note, "w");
	if (out != NULL) {
		fputs("Desk Note ready\n", out);
		fclose(out);
	}

	gtk_init(&argc, &argv);
	GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text(clip, "Desk Note ready", -1);

	char body[240] = "This window is Desk Note. It was installed from a .deb.";
	char title[160] = "Desk Note";
	if (file != NULL) {
		FILE *in = fopen(file, "r");
		if (in != NULL) {
			if (fgets(body, sizeof body, in) == NULL) {
				snprintf(body, sizeof body, "(empty file)");
			}
			body[strcspn(body, "\r\n")] = '\0';
			fclose(in);
		}
		snprintf(title, sizeof title, "Desk Note: %.80s", body);
	}
	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), title);
	gtk_window_set_default_size(GTK_WINDOW(win), 480, 280);
	GtkWidget *label = gtk_label_new(body);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_container_add(GTK_CONTAINER(win), label);
	g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	g_signal_connect(win, "focus-out-event", G_CALLBACK(claim_on_focus_out), NULL);
	gtk_widget_show_all(win);
	gtk_window_present(GTK_WINDOW(win));
	claim_clipboard();
	/* The ready file is written from the event loop, so a second client can
	 * receive the clipboard only after this window is able to answer. */
	g_idle_add(claim_when_running, NULL);
	if (once) {
		g_timeout_add(400, quit_soon, NULL);
	}
	gtk_main();
	return 0;
}
