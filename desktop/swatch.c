/*
 * Copyright 2026 Daniel Ye
 * Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Moving color bars. The dock blur should track this window when it overlaps.
 */
#include <gtk/gtk.h>

static int offset;

static gboolean tick(gpointer data) {
	gtk_widget_queue_draw(GTK_WIDGET(data));
	offset = (offset + 6) % 240;
	return G_SOURCE_CONTINUE;
}

static gboolean draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
	(void)data;
	int width = gtk_widget_get_allocated_width(widget);
	int height = gtk_widget_get_allocated_height(widget);
	const double colors[][3] = {
		{0.85, 0.18, 0.22},
		{0.15, 0.45, 0.82},
		{0.16, 0.62, 0.34},
		{0.92, 0.72, 0.18},
		{0.45, 0.22, 0.62}
	};
	int stripe = 48;
	for (int x = -stripe; x < width + stripe; x += stripe) {
		int index = ((x + offset) / stripe) % 5;
		if (index < 0) {
			index += 5;
		}
		cairo_set_source_rgb(cr, colors[index][0], colors[index][1], colors[index][2]);
		cairo_rectangle(cr, x - offset % stripe, 0, stripe, height);
		cairo_fill(cr);
	}
	cairo_set_source_rgba(cr, 1, 1, 1, 0.92);
	cairo_select_font_face(cr, "Ubuntu", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 22);
	cairo_move_to(cr, 16, 36);
	cairo_show_text(cr, "Live color field");
	return FALSE;
}

int main(int argc, char **argv) {
	gtk_init(&argc, &argv);
	g_set_prgname("dev-swatch");
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "Color field");
	gtk_window_set_default_size(GTK_WINDOW(window), 980, 260);
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	GtkWidget *area = gtk_drawing_area_new();
	g_signal_connect(area, "draw", G_CALLBACK(draw), NULL);
	gtk_container_add(GTK_CONTAINER(window), area);
	g_timeout_add(80, tick, area);
	gtk_widget_show_all(window);
	gtk_main();
	return 0;
}
