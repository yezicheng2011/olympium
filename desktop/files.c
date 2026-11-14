/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye 源码许可证（第 1.0 版）。全文见 LICENSE。
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static GtkWidget *list;
static GtkListStore *store;
static char current[4096];

static void set_path(const char *path) {
	if (realpath(path, current) == NULL) {
		snprintf(current, sizeof current, "%s", path);
	}
}

static void reload(void) {
	gtk_list_store_clear(store);
	GDir *dir = g_dir_open(current, 0, NULL);
	if (dir == NULL) {
		return;
	}
	const char *name;
	while ((name = g_dir_read_name(dir)) != NULL) {
		char full[8192];
		snprintf(full, sizeof full, "%s/%s", current, name);
		struct stat st;
		const char *kind = "file";
		if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
			kind = "directory";
		}
		GtkTreeIter iter;
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, name, 1, kind, -1);
	}
	g_dir_close(dir);
}

static void go(const char *path) {
	set_path(path);
	gtk_window_set_title(GTK_WINDOW(gtk_widget_get_toplevel(list)), current);
	reload();
}

static void on_up(GtkButton *button, gpointer data) {
	(void)button;
	(void)data;
	char copy[4096];
	snprintf(copy, sizeof copy, "%s", current);
	char *slash = strrchr(copy, '/');
	if (slash == NULL || slash == copy) {
		go("/");
		return;
	}
	*slash = '\0';
	go(copy);
}

static void on_home(GtkButton *button, gpointer data) {
	(void)button;
	(void)data;
	const char *home = g_get_home_dir();
	go(home != NULL ? home : "/");
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
	if (name != NULL && kind != NULL && strcmp(kind, "directory") == 0) {
		char next[8192];
		snprintf(next, sizeof next, "%s/%s", current, name);
		go(next);
	}
	g_free(name);
	g_free(kind);
}

int main(int argc, char **argv) {
	gtk_init(&argc, &argv);
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(window), 720, 480);
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_add(GTK_CONTAINER(window), box);
	GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
	GtkWidget *up = gtk_button_new_with_label("Up");
	GtkWidget *home = gtk_button_new_with_label("Home");
	gtk_box_pack_start(GTK_BOX(bar), up, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bar), home, FALSE, FALSE, 0);
	g_signal_connect(up, "clicked", G_CALLBACK(on_up), NULL);
	g_signal_connect(home, "clicked", G_CALLBACK(on_home), NULL);

	store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list), -1,
		"Name", gtk_cell_renderer_text_new(), "text", 0, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list), -1,
		"Kind", gtk_cell_renderer_text_new(), "text", 1, NULL);
	g_signal_connect(list, "row-activated", G_CALLBACK(on_activate), NULL);
	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(scroll), list);
	gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

	const char *start = argc > 1 ? argv[1] : g_get_home_dir();
	go(start != NULL ? start : "/");
	gtk_widget_show_all(window);
	gtk_main();
	return 0;
}
