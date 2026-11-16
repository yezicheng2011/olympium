/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * File browser with a sidebar, a toolbar, and icon and list views.
 * The compositor draws the glass title bar, the left window controls,
 * the shadow, and the rounded corners. This window is the content.
 */
#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "macbundle.h"

enum { STACK_MAX = 32 };

static GtkWidget *window;
static GtkWidget *title_label;
static GtkWidget *status_label;
static GtkWidget *icon_flow;
static GtkWidget *list_scroll;
static GtkWidget *icon_scroll;
static GtkWidget *back_button;
static GtkWidget *forward_button;
static GtkListStore *store;
static int icon_mode = 1;
static char current[4096];
static char filter[128];
static char back_stack[STACK_MAX][4096];
static int back_n;
static char forward_stack[STACK_MAX][4096];
static int forward_n;

static void rrect(cairo_t *cr, double x, double y, double w, double h, double r) {
	cairo_new_path(cr);
	cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
	cairo_arc(cr, x + w - r, y + r, r, 1.5 * M_PI, 2 * M_PI);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, 0.5 * M_PI);
	cairo_arc(cr, x + r, y + h - r, r, 0.5 * M_PI, M_PI);
	cairo_close_path(cr);
}

/* Folder: a tab behind a gradient body. Scale 1.0 is 64 by 52 pixels. */
static void paint_folder(cairo_t *cr, double scale) {
	cairo_save(cr);
	cairo_scale(cr, scale, scale);
	cairo_set_source_rgb(cr, 0.22, 0.56, 0.92);
	rrect(cr, 4, 4, 26, 14, 4);
	cairo_fill(cr);
	rrect(cr, 4, 10, 56, 38, 6);
	cairo_pattern_t *body = cairo_pattern_create_linear(0, 10, 0, 48);
	cairo_pattern_add_color_stop_rgb(body, 0, 0.50, 0.80, 1.00);
	cairo_pattern_add_color_stop_rgb(body, 1, 0.26, 0.62, 0.97);
	cairo_set_source(cr, body);
	cairo_fill(cr);
	cairo_pattern_destroy(body);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.45);
	rrect(cr, 5, 11, 54, 2, 1);
	cairo_fill(cr);
	cairo_restore(cr);
}

/* Document: a white sheet with a folded corner and faint text lines. */
static void paint_document(cairo_t *cr, double scale) {
	cairo_save(cr);
	cairo_scale(cr, scale, scale);
	cairo_move_to(cr, 16, 3);
	cairo_line_to(cr, 38, 3);
	cairo_line_to(cr, 48, 13);
	cairo_line_to(cr, 48, 49);
	cairo_line_to(cr, 16, 49);
	cairo_close_path(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_fill_preserve(cr);
	cairo_set_source_rgb(cr, 0.78, 0.79, 0.82);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	cairo_move_to(cr, 38, 3);
	cairo_line_to(cr, 38, 13);
	cairo_line_to(cr, 48, 13);
	cairo_close_path(cr);
	cairo_set_source_rgb(cr, 0.86, 0.87, 0.90);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 0.84, 0.85, 0.88);
	for (int i = 0; i < 4; i++) {
		rrect(cr, 21, 21 + i * 6, i == 3 ? 14 : 22, 2, 1);
		cairo_fill(cr);
	}
	cairo_restore(cr);
}

/* Disk image: a drive, drawn here, for a .dmg. */
static void paint_disk(cairo_t *cr) {
	rrect(cr, 8, 14, 48, 30, 7);
	cairo_pattern_t *body = cairo_pattern_create_linear(0, 14, 0, 44);
	cairo_pattern_add_color_stop_rgb(body, 0, 0.97, 0.97, 0.98);
	cairo_pattern_add_color_stop_rgb(body, 1, 0.82, 0.83, 0.87);
	cairo_set_source(cr, body);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(body);
	cairo_set_source_rgb(cr, 0.66, 0.67, 0.72);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	cairo_set_source_rgb(cr, 0.60, 0.62, 0.68);
	rrect(cr, 14, 34, 26, 3, 1.5);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 0.22, 0.72, 0.36);
	cairo_arc(cr, 48, 35.5, 2.2, 0, 2 * M_PI);
	cairo_fill(cr);
}

/* Package: an open box, for a .pkg. */
static void paint_package(cairo_t *cr) {
	cairo_set_source_rgb(cr, 0.80, 0.60, 0.36);
	cairo_move_to(cr, 12, 20);
	cairo_line_to(cr, 32, 12);
	cairo_line_to(cr, 52, 20);
	cairo_line_to(cr, 32, 28);
	cairo_close_path(cr);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 0.70, 0.50, 0.28);
	cairo_move_to(cr, 12, 20);
	cairo_line_to(cr, 32, 28);
	cairo_line_to(cr, 32, 48);
	cairo_line_to(cr, 12, 40);
	cairo_close_path(cr);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 0.62, 0.43, 0.23);
	cairo_move_to(cr, 52, 20);
	cairo_line_to(cr, 32, 28);
	cairo_line_to(cr, 32, 48);
	cairo_line_to(cr, 52, 40);
	cairo_close_path(cr);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.55);
	cairo_move_to(cr, 21, 16.5);
	cairo_line_to(cr, 41, 24.5);
	cairo_line_to(cr, 43, 23.7);
	cairo_line_to(cr, 23, 15.7);
	cairo_close_path(cr);
	cairo_fill(cr);
}

/* What an item in the grid is drawn as. */
enum { ITEM_DOCUMENT = 0, ITEM_FOLDER = 1, ITEM_DISK = 2, ITEM_PACKAGE = 3 };

static gboolean draw_item_icon(GtkWidget *widget, cairo_t *cr, gpointer data) {
	(void)widget;
	switch (GPOINTER_TO_INT(data)) {
	case ITEM_FOLDER:
		paint_folder(cr, 1.0);
		break;
	case ITEM_DISK:
		paint_disk(cr);
		break;
	case ITEM_PACKAGE:
		paint_package(cr);
		break;
	default:
		paint_document(cr, 1.0);
		break;
	}
	return FALSE;
}

/* A Mac application is a folder whose name ends in .app. It is shown as one
 * thing, with its own picture, and opening it does not show its insides. */
static int is_app_bundle(const char *path) {
	size_t len = strlen(path);
	return len > 4 && g_ascii_strcasecmp(path + len - 4, ".app") == 0 && mac_is_bundle_dir(path);
}

static GdkPixbuf *app_bundle_picture(const char *path) {
	struct mac_bundle b;
	if (mac_bundle_read(path, &b) != 0 || b.icon_path[0] == '\0') {
		return NULL;
	}
	gchar *data = NULL;
	gsize length = 0;
	if (!g_file_get_contents(b.icon_path, &data, &length, NULL) || length > 64 * 1024 * 1024) {
		g_free(data);
		return NULL;
	}
	GdkPixbuf *picture = NULL;
	size_t at = 0;
	size_t png = 0;
	GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
	gdk_pixbuf_loader_set_size(loader, 52, 52);
	/* The PNG inside the .icns when there is one, otherwise the .icns as it
	 * is, which the loader reads in its older forms. */
	gboolean fed = mac_icns_png((const unsigned char *)data, length, &at, &png) == 0
		? gdk_pixbuf_loader_write(loader, (const guchar *)data + at, png, NULL)
		: gdk_pixbuf_loader_write(loader, (const guchar *)data, length, NULL);
	if (gdk_pixbuf_loader_close(loader, NULL) && fed) {
		picture = gdk_pixbuf_loader_get_pixbuf(loader);
		if (picture != NULL) {
			g_object_ref(picture);
		}
	}
	g_object_unref(loader);
	g_free(data);
	return picture;
}

/* Small sidebar glyphs. kind: 0 house, 1 sheet, 2 arrow, 3 grid, 4 drive. */
static gboolean draw_side_icon(GtkWidget *widget, cairo_t *cr, gpointer data) {
	(void)widget;
	int kind = GPOINTER_TO_INT(data);
	cairo_set_source_rgb(cr, 0.16, 0.47, 0.96);
	cairo_set_line_width(cr, 1.6);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	if (kind == 0) {
		cairo_move_to(cr, 2, 9);
		cairo_line_to(cr, 9, 3);
		cairo_line_to(cr, 16, 9);
		cairo_move_to(cr, 4, 8);
		cairo_line_to(cr, 4, 15);
		cairo_line_to(cr, 14, 15);
		cairo_line_to(cr, 14, 8);
		cairo_stroke(cr);
	} else if (kind == 1) {
		rrect(cr, 4, 2, 10, 14, 2);
		cairo_stroke(cr);
		cairo_move_to(cr, 7, 7);
		cairo_line_to(cr, 11, 7);
		cairo_move_to(cr, 7, 11);
		cairo_line_to(cr, 11, 11);
		cairo_stroke(cr);
	} else if (kind == 2) {
		cairo_arc(cr, 9, 9, 7, 0, 2 * M_PI);
		cairo_stroke(cr);
		cairo_move_to(cr, 9, 5);
		cairo_line_to(cr, 9, 13);
		cairo_move_to(cr, 6, 10);
		cairo_line_to(cr, 9, 13);
		cairo_line_to(cr, 12, 10);
		cairo_stroke(cr);
	} else if (kind == 3) {
		for (int i = 0; i < 4; i++) {
			rrect(cr, 3 + (i % 2) * 7, 3 + (i / 2) * 7, 5, 5, 1.2);
			cairo_stroke(cr);
		}
	} else {
		rrect(cr, 2, 5, 14, 9, 2);
		cairo_stroke(cr);
		cairo_arc(cr, 13, 11, 0.9, 0, 2 * M_PI);
		cairo_fill(cr);
	}
	return FALSE;
}

/* Toolbar glyphs. kind: 0 back, 1 forward, 2 icon view, 3 list view. */
static gboolean draw_tool_icon(GtkWidget *widget, cairo_t *cr, gpointer data) {
	int kind = GPOINTER_TO_INT(data);
	gboolean sensitive = gtk_widget_is_sensitive(widget);
	cairo_set_source_rgba(cr, 0.24, 0.24, 0.27, sensitive ? 1.0 : 0.35);
	cairo_set_line_width(cr, 1.9);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	if (kind == 0) {
		cairo_move_to(cr, 11, 3);
		cairo_line_to(cr, 5, 9);
		cairo_line_to(cr, 11, 15);
		cairo_stroke(cr);
	} else if (kind == 1) {
		cairo_move_to(cr, 7, 3);
		cairo_line_to(cr, 13, 9);
		cairo_line_to(cr, 7, 15);
		cairo_stroke(cr);
	} else if (kind == 2) {
		for (int i = 0; i < 4; i++) {
			rrect(cr, 3 + (i % 2) * 7, 3 + (i / 2) * 7, 5, 5, 1.2);
			cairo_fill(cr);
		}
	} else {
		for (int i = 0; i < 3; i++) {
			cairo_move_to(cr, 3, 4.5 + i * 4.5);
			cairo_line_to(cr, 15, 4.5 + i * 4.5);
		}
		cairo_stroke(cr);
	}
	return FALSE;
}

static void set_path(const char *path) {
	if (realpath(path, current) == NULL) {
		snprintf(current, sizeof current, "%s", path);
	}
}

static void go(const char *path);

static void open_entry(const char *name) {
	char next[8192];
	snprintf(next, sizeof next, "%s/%s", strcmp(current, "/") == 0 ? "" : current, name);
	struct stat st;
	if (stat(next, &st) != 0) {
		return;
	}
	if (S_ISDIR(st.st_mode) && !is_app_bundle(next)) {
		go(next);
		return;
	}
	if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) {
		return;
	}
	/* Packages and programs open in the install window. Nothing runs here. */
	if (access("/usr/bin/dev-install", X_OK) != 0) {
		gtk_label_set_text(GTK_LABEL(status_label),
			"Install is not on this image. The file was not run.");
		return;
	}
	char *quoted = g_shell_quote(next);
	char *cmd = g_strdup_printf("/usr/bin/dev-install open %s", quoted);
	g_spawn_command_line_async(cmd, NULL);
	gtk_label_set_text(GTK_LABEL(status_label), "Opened the install window. Nothing ran yet.");
	g_free(cmd);
	g_free(quoted);
}

static void on_icon(GtkButton *button, gpointer data) {
	(void)button;
	open_entry(data);
}

static void free_name(gpointer data, GClosure *closure) {
	(void)closure;
	g_free(data);
}

static void add_icon(const char *name, int folder, const char *full) {
	GtkWidget *button = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_style_context_add_class(gtk_widget_get_style_context(button), "item");
	gtk_widget_set_size_request(button, 112, 96);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	GtkWidget *art = NULL;
	char shown[256];
	snprintf(shown, sizeof shown, "%s", name);
	if (folder == ITEM_FOLDER && is_app_bundle(full)) {
		/* Its own picture, and its name without the ".app". */
		GdkPixbuf *picture = app_bundle_picture(full);
		if (picture != NULL) {
			art = gtk_image_new_from_pixbuf(picture);
			g_object_unref(picture);
		}
		size_t len = strlen(shown);
		if (len > 4) {
			shown[len - 4] = '\0';
		}
	}
	if (art == NULL) {
		art = gtk_drawing_area_new();
		g_signal_connect(art, "draw", G_CALLBACK(draw_item_icon), GINT_TO_POINTER(folder));
	}
	gtk_widget_set_size_request(art, 64, 52);
	gtk_widget_set_halign(art, GTK_ALIGN_CENTER);
	GtkWidget *label = gtk_label_new(shown);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
	gtk_label_set_max_width_chars(GTK_LABEL(label), 13);
	gtk_box_pack_start(GTK_BOX(box), art, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(button), box);
	g_signal_connect_data(button, "clicked", G_CALLBACK(on_icon), g_strdup(name), free_name, 0);
	gtk_container_add(GTK_CONTAINER(icon_flow), button);
	gtk_widget_show_all(button);
}

static int by_name(gconstpointer a, gconstpointer b) {
	return g_ascii_strcasecmp(*(const char *const *)a, *(const char *const *)b);
}

static void reload(void) {
	gtk_list_store_clear(store);
	GList *children = gtk_container_get_children(GTK_CONTAINER(icon_flow));
	for (GList *item = children; item != NULL; item = item->next) {
		gtk_widget_destroy(GTK_WIDGET(item->data));
	}
	g_list_free(children);

	const char *leaf = strrchr(current, '/');
	const char *shown = (leaf != NULL && leaf[1] != '\0') ? leaf + 1 : "Computer";
	gtk_label_set_text(GTK_LABEL(title_label), shown);
	gtk_widget_set_sensitive(back_button, back_n > 0);
	gtk_widget_set_sensitive(forward_button, forward_n > 0);

	GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
	GDir *dir = g_dir_open(current, 0, NULL);
	if (dir == NULL) {
		gtk_label_set_text(GTK_LABEL(status_label), "This folder cannot be opened.");
		g_ptr_array_free(names, TRUE);
		return;
	}
	const char *name;
	while ((name = g_dir_read_name(dir)) != NULL) {
		if (name[0] == '.' && getenv("OLYMPIUM_SHOW_HIDDEN") == NULL) {
			continue;
		}
		if (filter[0] != '\0' && strcasestr(name, filter) == NULL) {
			continue;
		}
		g_ptr_array_add(names, g_strdup(name));
	}
	g_dir_close(dir);
	g_ptr_array_sort(names, by_name);
	for (guint i = 0; i < names->len; i++) {
		name = g_ptr_array_index(names, i);
		char full[8192];
		snprintf(full, sizeof full, "%s/%s", strcmp(current, "/") == 0 ? "" : current, name);
		struct stat st;
		const char *kind = "Document";
		char size[32] = "--";
		int folder = 0;
		if (stat(full, &st) == 0) {
			if (S_ISDIR(st.st_mode)) {
				kind = is_app_bundle(full) ? "Application" : "Folder";
				folder = ITEM_FOLDER;
			} else {
				size_t name_len = strlen(name);
				if (name_len > 4 && g_ascii_strcasecmp(name + name_len - 4, ".dmg") == 0) {
					kind = "Disk Image";
					folder = ITEM_DISK;
				} else if (name_len > 4 && g_ascii_strcasecmp(name + name_len - 4, ".pkg") == 0) {
					kind = "Installer Package";
					folder = ITEM_PACKAGE;
				}
				if (folder == ITEM_DOCUMENT && (st.st_mode & 0111)) {
					kind = "Program";
				}
				if (st.st_size >= 1024 * 1024) {
					snprintf(size, sizeof size, "%.1f MB", st.st_size / 1048576.0);
				} else if (st.st_size >= 1024) {
					snprintf(size, sizeof size, "%lld KB", (long long)(st.st_size / 1024));
				} else {
					snprintf(size, sizeof size, "%lld bytes", (long long)st.st_size);
				}
			}
		}
		GtkTreeIter iter;
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, name, 1, kind, 2, size, -1);
		/* The icon grid is capped so a very large folder still opens at once. */
		if (i < 400) {
			add_icon(name, folder, full);
		}
	}
	char status[96];
	if (names->len == 0) {
		snprintf(status, sizeof status, filter[0] != '\0' ? "No matches" : "This folder is empty");
	} else {
		snprintf(status, sizeof status, "%u %s", names->len, names->len == 1 ? "item" : "items");
	}
	gtk_label_set_text(GTK_LABEL(status_label), status);
	g_ptr_array_free(names, TRUE);
}

static void push(char stack[][4096], int *count, const char *path) {
	if (*count == STACK_MAX) {
		memmove(stack[0], stack[1], sizeof(stack[0]) * (STACK_MAX - 1));
		(*count)--;
	}
	snprintf(stack[*count], sizeof stack[*count], "%s", path);
	(*count)++;
}

static void go(const char *path) {
	if (current[0] != '\0') {
		push(back_stack, &back_n, current);
	}
	forward_n = 0;
	set_path(path);
	reload();
}

static void on_place(GtkButton *button, gpointer data) {
	(void)button;
	go((const char *)data);
}

static void on_back(GtkButton *button, gpointer data) {
	(void)button;
	(void)data;
	if (back_n <= 0) {
		return;
	}
	push(forward_stack, &forward_n, current);
	back_n--;
	set_path(back_stack[back_n]);
	reload();
}

static void on_forward(GtkButton *button, gpointer data) {
	(void)button;
	(void)data;
	if (forward_n <= 0) {
		return;
	}
	push(back_stack, &back_n, current);
	forward_n--;
	set_path(forward_stack[forward_n]);
	reload();
}

static void on_view(GtkButton *button, gpointer data) {
	(void)button;
	icon_mode = GPOINTER_TO_INT(data);
	gtk_widget_set_visible(icon_scroll, icon_mode);
	gtk_widget_set_visible(list_scroll, !icon_mode);
}

static void on_search(GtkSearchEntry *entry, gpointer data) {
	(void)data;
	snprintf(filter, sizeof filter, "%s", gtk_entry_get_text(GTK_ENTRY(entry)));
	reload();
}

static void on_activate(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data) {
	(void)column;
	(void)data;
	GtkTreeIter iter;
	GtkTreeModel *model = gtk_tree_view_get_model(view);
	if (!gtk_tree_model_get_iter(model, &iter, path)) {
		return;
	}
	char *name = NULL;
	char *kind = NULL;
	gtk_tree_model_get(model, &iter, 0, &name, 1, &kind, -1);
	if (name != NULL) {
		open_entry(name);
	}
	g_free(name);
	g_free(kind);
}

static GtkWidget *section_label(const char *text) {
	GtkWidget *label = gtk_label_new(text);
	gtk_style_context_add_class(gtk_widget_get_style_context(label), "section");
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_widget_set_margin_start(label, 10);
	gtk_widget_set_margin_top(label, 10);
	gtk_widget_set_margin_bottom(label, 2);
	return label;
}

static GtkWidget *place_button(const char *label, const char *path, int glyph) {
	GtkWidget *button = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_style_context_add_class(gtk_widget_get_style_context(button), "place");
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *art = gtk_drawing_area_new();
	gtk_widget_set_size_request(art, 18, 18);
	gtk_widget_set_valign(art, GTK_ALIGN_CENTER);
	g_signal_connect(art, "draw", G_CALLBACK(draw_side_icon), GINT_TO_POINTER(glyph));
	GtkWidget *text = gtk_label_new(label);
	gtk_box_pack_start(GTK_BOX(row), art, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), text, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(button), row);
	g_signal_connect(button, "clicked", G_CALLBACK(on_place), (gpointer)path);
	return button;
}

static GtkWidget *tool_button(int glyph, GCallback callback, gpointer data) {
	GtkWidget *button = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_style_context_add_class(gtk_widget_get_style_context(button), "tool");
	GtkWidget *art = gtk_drawing_area_new();
	gtk_widget_set_size_request(art, 18, 18);
	g_signal_connect(art, "draw", G_CALLBACK(draw_tool_icon), GINT_TO_POINTER(glyph));
	gtk_container_add(GTK_CONTAINER(button), art);
	g_signal_connect(button, "clicked", callback, data);
	return button;
}

int main(int argc, char **argv) {
	gtk_init(&argc, &argv);
	g_set_prgname("dev-files");
	g_object_set(gtk_settings_get_default(), "gtk-font-name", "Ubuntu 12", NULL);
	GtkCssProvider *css = gtk_css_provider_new();
	gtk_css_provider_load_from_data(css,
		"window { background: #ffffff; }"
		"label { color: #1d1d1f; }"
		".sidebar { background: #ecedf0; border-right: 1px solid #d9d9de; }"
		".section { color: #8a8a8f; font-weight: bold; font-size: 11px; }"
		"button.place { padding: 4px 8px; margin: 0 6px; border-radius: 7px;"
		"  border: none; background: none; box-shadow: none; }"
		"button.place:hover { background: rgba(0,0,0,0.07); }"
		".toolbar { background: #f6f6f8; border-bottom: 1px solid #dedee2; }"
		".title { font-weight: bold; font-size: 14px; }"
		"button.tool { padding: 5px 7px; border-radius: 7px; border: none;"
		"  background: none; box-shadow: none; }"
		"button.tool:hover { background: rgba(0,0,0,0.08); }"
		"button.item { padding: 6px 2px; border-radius: 9px; border: none;"
		"  background: none; box-shadow: none; }"
		"button.item:hover { background: rgba(41,120,245,0.14); }"
		"button.item label { font-size: 12px; }"
		"entry { border-radius: 8px; border: 1px solid #d2d2d7; background: #ffffff;"
		"  color: #1d1d1f; padding: 2px 6px; min-height: 24px; }"
		".status { color: #6e6e73; font-size: 11px; background: #f6f6f8;"
		"  border-top: 1px solid #dedee2; padding: 3px; }"
		"treeview { background: #ffffff; color: #1d1d1f; }"
		"treeview:selected { background: #2978f5; color: #ffffff; }"
		"treeview header button { background: #f6f6f8; color: #6e6e73; border: none;"
		"  border-bottom: 1px solid #dedee2; padding: 3px 8px; }"
		, -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
		GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
	gtk_window_set_default_size(GTK_WINDOW(window), 920, 540);
	gtk_window_set_title(GTK_WINDOW(window), "Files");
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add(GTK_CONTAINER(window), root);

	GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
	gtk_style_context_add_class(gtk_widget_get_style_context(side), "sidebar");
	gtk_widget_set_size_request(side, 190, -1);
	gtk_box_pack_start(GTK_BOX(root), side, FALSE, FALSE, 0);
	const char *home = g_get_home_dir();
	if (home == NULL) {
		home = "/";
	}
	gtk_box_pack_start(GTK_BOX(side), section_label("Favorites"), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(side), place_button("Home", home, 0), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(side), place_button("Documents", "/usr/share/doc", 1), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(side), place_button("Downloads", "/tmp", 2), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(side), place_button("Applications", "/usr/bin", 3), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(side), section_label("Locations"), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(side), place_button("Computer", "/", 4), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(side), place_button("System", "/usr", 4), FALSE, FALSE, 0);

	GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(root), main_box, TRUE, TRUE, 0);

	GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_style_context_add_class(gtk_widget_get_style_context(toolbar), "toolbar");
	GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_widget_set_margin_start(inner, 10);
	gtk_widget_set_margin_end(inner, 10);
	gtk_widget_set_margin_top(inner, 7);
	gtk_widget_set_margin_bottom(inner, 7);
	gtk_box_pack_start(GTK_BOX(toolbar), inner, TRUE, TRUE, 0);
	back_button = tool_button(0, G_CALLBACK(on_back), NULL);
	forward_button = tool_button(1, G_CALLBACK(on_forward), NULL);
	title_label = gtk_label_new("");
	gtk_style_context_add_class(gtk_widget_get_style_context(title_label), "title");
	gtk_widget_set_margin_start(title_label, 8);
	gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
	GtkWidget *search = gtk_search_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(search), "Search");
	gtk_widget_set_size_request(search, 180, -1);
	g_signal_connect(search, "search-changed", G_CALLBACK(on_search), NULL);
	gtk_box_pack_start(GTK_BOX(inner), back_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(inner), forward_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(inner), title_label, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(inner), search, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(inner), tool_button(3, G_CALLBACK(on_view), GINT_TO_POINTER(0)),
		FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(inner), tool_button(2, G_CALLBACK(on_view), GINT_TO_POINTER(1)),
		FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(main_box), toolbar, FALSE, FALSE, 0);

	store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	GtkWidget *list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list), -1,
		"Name", gtk_cell_renderer_text_new(), "text", 0, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list), -1,
		"Kind", gtk_cell_renderer_text_new(), "text", 1, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list), -1,
		"Size", gtk_cell_renderer_text_new(), "text", 2, NULL);
	gtk_tree_view_column_set_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(list), 0), TRUE);
	g_signal_connect(list, "row-activated", G_CALLBACK(on_activate), NULL);
	list_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(list_scroll), list);

	icon_flow = gtk_flow_box_new();
	gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(icon_flow), GTK_SELECTION_NONE);
	gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(icon_flow), 12);
	gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(icon_flow), TRUE);
	gtk_widget_set_valign(icon_flow, GTK_ALIGN_START);
	gtk_widget_set_margin_start(icon_flow, 14);
	gtk_widget_set_margin_end(icon_flow, 14);
	gtk_widget_set_margin_top(icon_flow, 12);
	icon_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(icon_scroll),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(icon_scroll), icon_flow);
	gtk_box_pack_start(GTK_BOX(main_box), icon_scroll, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(main_box), list_scroll, TRUE, TRUE, 0);

	status_label = gtk_label_new("");
	gtk_style_context_add_class(gtk_widget_get_style_context(status_label), "status");
	gtk_box_pack_start(GTK_BOX(main_box), status_label, FALSE, FALSE, 0);

	set_path(argc > 1 ? argv[1] : home);
	gtk_widget_show_all(window);
	gtk_widget_hide(list_scroll);
	reload();
	gtk_main();
	return 0;
}
