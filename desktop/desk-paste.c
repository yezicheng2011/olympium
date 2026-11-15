/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * Second window. It reads the clipboard and the exchange file written by
 * Desk Note. Both are ordinary clients of this desktop.
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GtkWidget *g_win;
static GtkWidget *g_label;
static char g_file_text[120] = "missing";
static int g_tries;
static int g_announced;

static void publish(const char *clip_text) {
	if (g_announced) {
		return;
	}
	g_announced = 1;
	char title[240];
	char body[300];
	snprintf(title, sizeof title, "Pasted: %.60s | file %.40s", clip_text, g_file_text);
	snprintf(body, sizeof body, "Clipboard: %s\nFile: %s", clip_text, g_file_text);
	gtk_window_set_title(GTK_WINDOW(g_win), title);
	gtk_label_set_text(GTK_LABEL(g_label), body);
	printf("clipboard: %s\n", clip_text);
	printf("exchange: %s\n", g_file_text);
	fflush(stdout);
}

static gboolean ask_clipboard(gpointer data);

static void got_text(GtkClipboard *clipboard, const gchar *text, gpointer data) {
	(void)clipboard;
	(void)data;
	if (text != NULL && text[0] != '\0') {
		publish(text);
		return;
	}
	if (g_tries < 8) {
		g_tries++;
		g_timeout_add(150, ask_clipboard, NULL);
		return;
	}
	publish("missing");
}

static gboolean ask_clipboard(gpointer data) {
	(void)data;
	gtk_clipboard_request_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), got_text, NULL);
	return FALSE;
}

static gboolean quit_soon(gpointer data) {
	(void)data;
	gtk_main_quit();
	return FALSE;
}

int main(int argc, char **argv) {
	int once = 0;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--once") == 0) {
			once = 1;
		}
	}
	gtk_init(&argc, &argv);
	const char *home = getenv("OLYMPIUM_DESK_HOME");
	if (home == NULL || home[0] == '\0') {
		home = "/tmp";
	}
	char path[512];
	snprintf(path, sizeof path, "%s/exchange/desk-note.txt", home);
	FILE *f = fopen(path, "r");
	if (f != NULL) {
		if (fgets(g_file_text, sizeof g_file_text, f) != NULL) {
			g_file_text[strcspn(g_file_text, "\r\n")] = '\0';
		}
		fclose(f);
	}
	g_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(g_win), "Desk Paste");
	gtk_window_set_default_size(GTK_WINDOW(g_win), 420, 180);
	g_label = gtk_label_new("Clipboard: reading\nFile: reading");
	gtk_container_add(GTK_CONTAINER(g_win), g_label);
	g_signal_connect(g_win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	gtk_widget_show_all(g_win);
	ask_clipboard(NULL);
	if (once) {
		g_timeout_add(1600, quit_soon, NULL);
	}
	gtk_main();
	if (!g_announced) {
		publish("missing");
	}
	return 0;
}
