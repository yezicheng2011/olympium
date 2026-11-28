/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * A Linux window the desktop can open. It is not a Mac application.
 */
#include <gtk/gtk.h>

static const char *BODY =
	"This window is a Linux program written for Olympium. "
	"It is not a Mac application. It was not built with an Apple SDK, "
	"and it does not contain Apple software.\n\n"
	"Launchpad, the dock, and Files open this program directly. "
	"On the 64-bit images the Olympium runtime lists it as a Linux program "
	"and can start it that way. That runtime does not start macOS software. "
	"No Apple operating system, framework, font, or artwork is included. "
	"Code signatures and DRM are not bypassed.\n\n"
	"iOS is not part of this system.";

int main(int argc, char **argv) {
	gtk_init(&argc, &argv);
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "Lawful test app");
	gtk_window_set_default_size(GTK_WINDOW(window), 680, 460);
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
	gtk_container_set_border_width(GTK_CONTAINER(box), 28);
	gtk_container_add(GTK_CONTAINER(window), box);

	GtkWidget *title = gtk_label_new("Lawful test app");
	gtk_widget_set_halign(title, GTK_ALIGN_START);
	PangoAttrList *attrs = pango_attr_list_new();
	pango_attr_list_insert(attrs, pango_attr_scale_new(1.7));
	pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
	gtk_label_set_attributes(GTK_LABEL(title), attrs);
	pango_attr_list_unref(attrs);
	gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);

	GtkWidget *body = gtk_label_new(BODY);
	gtk_label_set_line_wrap(GTK_LABEL(body), TRUE);
	gtk_label_set_xalign(GTK_LABEL(body), 0);
	gtk_label_set_max_width_chars(GTK_LABEL(body), 58);
	gtk_label_set_selectable(GTK_LABEL(body), TRUE);
	gtk_box_pack_start(GTK_BOX(box), body, TRUE, TRUE, 0);

	GtkWidget *close = gtk_button_new_with_label("Close");
	gtk_widget_set_halign(close, GTK_ALIGN_END);
	g_signal_connect(close, "clicked", G_CALLBACK(gtk_main_quit), NULL);
	gtk_box_pack_start(GTK_BOX(box), close, FALSE, FALSE, 0);

	gtk_widget_show_all(window);
	gtk_main();
	return 0;
}
