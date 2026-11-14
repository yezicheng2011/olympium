/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * Finder-like browser. Window chrome (frost, traffic lights, shadow, rounded
 * corners) is drawn by the compositor. This window is the content.
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static GtkWidget *window;
static GtkWidget *list;
static GtkListStore *store;
static GtkWidget *path_label;
static char current[4096];

static void set_path(const char *path) {
	if (realpath(path, current) == NULL) {
		snprintf(current, sizeof current, "%s", path);
	}
}

static void reload(void) {
	gtk_list_store_clear(store);
	gtk_label_set_text(GTK_LABEL(path_label), current);
	GDir *dir = g_dir_open(current, 0, NULL);
	if (dir == NULL) {
		return;
	}
	const char *name;
	while ((name = g_dir_read_name(dir)) != NULL) {
		if (name[0] == '.' && getenv("OLYMPIUM_SHOW_HIDDEN") == NULL) {
			continue;
		}
		char full[8192];
		snprintf(full, sizeof full, "%s/%s", current, name);
		struct stat st;
		const char *kind = "File";
		char size[32] = "—";
		if (stat(full, &st) == 0) {
			if (S_ISDIR(st.st_mode)) {
				kind = "Folder";
			} else {
				snprintf(size, sizeof size, "%lld", (long long)st.st_size);
			}
		}
		GtkTreeIter iter;
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, name, 1, kind, 2, size, -1);
	}
	g_dir_close(dir);
}

static void go(const char *path) {
	set_path(path);
	gtk_window_set_title(GTK_WINDOW(window), "Files");
	reload();
}

static void on_place(GtkButton *button, gpointer data) {
	(void)button;
	go((const char *)data);
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
	if (name != NULL && kind != NULL && strcmp(kind, "Folder") == 0) {
		char next[8192];
		snprintf(next, sizeof next, "%s/%s", current, name);
		go(next);
	}
	g_free(name);
	g_free(kind);
}

static GtkWidget *place_button(const char *label, const char *path) {
	GtkWidget *button = gtk_button_new_with_label(label);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_widget_set_halign(button, GTK_ALIGN_START);
	g_signal_connect(button, "clicked", G_CALLBACK(on_place), (gpointer)path);
	return button;
}

int main(int argc, char **argv) {
	gtk_init(&argc, &argv);
	g_set_prgname("dev-files");
	g_object_set(gtk_settings_get_default(), "gtk-font-name", "Ubuntu 11", NULL);
	GtkCssProvider *css = gtk_css_provider_new();
	gtk_css_provider_load_from_data(css,
		"window { background: #f4f4f6; }"
		"label { color: #1c1c1e; }"
		"button { color: #1c1c1e; }"
		".sidebar { background: rgba(236,236,240,0.96); }"
		"treeview { background: white; }"
		, -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
		GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
	gtk_window_set_default_size(GTK_WINDOW(window), 860, 520);
	gtk_window_set_title(GTK_WINDOW(window), "Files");
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add(GTK_CONTAINER(window), root);

	GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_style_context_add_class(gtk_widget_get_style_context(side), "sidebar");
	gtk_widget_set_size_request(side, 168, -1);
	gtk_widget_set_margin_top(side, 12);
	gtk_widget_set_margin_start(side, 8);
	gtk_widget_set_margin_end(side, 8);
	gtk_box_pack_start(GTK_BOX(root), side, FALSE, FALSE, 0);
	GtkWidget *places = gtk_label_new("Places");
	gtk_widget_set_halign(places, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(side), places, FALSE, FALSE, 4);
	const char *home = g_get_home_dir();
	if (home == NULL) {
		home = "/";
	}
	gtk_box_pack_start(GTK_BOX(side), place_button("Home", home), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(side), place_button("Computer", "/"), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(side), place_button("Applications", "/usr/bin"), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(side), place_button("Documents", "/usr/share/doc"), FALSE, FALSE, 0);

	GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(root), main_box, TRUE, TRUE, 0);
	path_label = gtk_label_new("");
	gtk_widget_set_halign(path_label, GTK_ALIGN_START);
	gtk_widget_set_margin_top(path_label, 10);
	gtk_widget_set_margin_start(path_label, 12);
	gtk_widget_set_margin_bottom(path_label, 6);
	gtk_box_pack_start(GTK_BOX(main_box), path_label, FALSE, FALSE, 0);

	store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list), -1,
		"Name", gtk_cell_renderer_text_new(), "text", 0, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list), -1,
		"Kind", gtk_cell_renderer_text_new(), "text", 1, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list), -1,
		"Size", gtk_cell_renderer_text_new(), "text", 2, NULL);
	g_signal_connect(list, "row-activated", G_CALLBACK(on_activate), NULL);
	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(scroll), list);
	gtk_box_pack_start(GTK_BOX(main_box), scroll, TRUE, TRUE, 0);

	const char *start = argc > 1 ? argv[1] : home;
	go(start);
	gtk_widget_show_all(window);
	gtk_main();
	return 0;
}
