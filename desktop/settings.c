/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * Settings, laid out like a system-settings window: a search field, an icon
 * sidebar, and grouped rows. The glyphs are original. The compositor draws
 * the glass title bar and the left window controls.
 */
#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <unistd.h>

static GtkWidget *stack;
static GtkWidget *nav_buttons[8];
static GtkWidget *empty_label;
static int nav_count;

static void send_contrast(void) {
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		return;
	}
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof addr.sun_path, "/run/dev-desktop/control");
	if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0) {
		const char *msg = "contrast toggle\n";
		if (write(fd, msg, strlen(msg)) < 0) {
			/* The compositor may have closed the socket. */
		}
	}
	close(fd);
}

static char *read_file(const char *path, const char *missing) {
	char *text = NULL;
	if (!g_file_get_contents(path, &text, NULL, NULL)) {
		return g_strdup(missing);
	}
	return text;
}

static void rounded(cairo_t *cr, double x, double y, double w, double h, double r) {
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2.0, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2.0);
	cairo_arc(cr, x + r, y + h - r, r, G_PI / 2.0, G_PI);
	cairo_arc(cr, x + r, y + r, r, G_PI, 3.0 * G_PI / 2.0);
	cairo_close_path(cr);
}

static void icon_fill(cairo_t *cr, double r1, double g1, double b1, double r2, double g2, double b2) {
	rounded(cr, 1, 1, 26, 26, 6.5);
	cairo_pattern_t *bg = cairo_pattern_create_linear(0, 0, 0, 28);
	cairo_pattern_add_color_stop_rgb(bg, 0, r1, g1, b1);
	cairo_pattern_add_color_stop_rgb(bg, 1, r2, g2, b2);
	cairo_set_source(cr, bg);
	cairo_fill(cr);
	cairo_pattern_destroy(bg);
}

static gboolean draw_nav_icon(GtkWidget *widget, cairo_t *cr, gpointer data) {
	(void)widget;
	const char *id = data;
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	if (strcmp(id, "appearance") == 0) {
		icon_fill(cr, 0.36, 0.62, 1.00, 0.18, 0.36, 0.90);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
		rounded(cr, 6, 7, 16, 12, 2);
		cairo_fill(cr);
		cairo_rectangle(cr, 12, 19, 4, 3);
		cairo_fill(cr);
		rounded(cr, 8, 21, 12, 2.2, 1);
		cairo_fill(cr);
	} else if (strcmp(id, "drivers") == 0) {
		icon_fill(cr, 1.00, 0.72, 0.28, 0.94, 0.42, 0.12);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
		rounded(cr, 7, 8, 14, 12, 2.4);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0.85, 0.40, 0.10, 1);
		for (int i = 0; i < 4; i++) {
			cairo_rectangle(cr, 9 + i * 3.2, 5.2, 1.6, 3);
			cairo_rectangle(cr, 9 + i * 3.2, 19.6, 1.6, 3);
			cairo_fill(cr);
		}
	} else if (strcmp(id, "compatibility") == 0) {
		icon_fill(cr, 0.70, 0.48, 0.98, 0.40, 0.24, 0.86);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.55);
		rounded(cr, 6, 6, 12, 12, 3);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
		rounded(cr, 10, 10, 12, 12, 3);
		cairo_fill(cr);
	} else if (strcmp(id, "olympium") == 0) {
		icon_fill(cr, 0.22, 0.38, 0.66, 0.08, 0.14, 0.32);
		cairo_set_source_rgba(cr, 1.00, 0.84, 0.40, 1);
		cairo_set_line_width(cr, 2.1);
		cairo_move_to(cr, 8, 19);
		cairo_curve_to(cr, 7, 12, 18, 16, 14, 9);
		cairo_stroke(cr);
		cairo_arc(cr, 17, 11, 2.3, 0, 6.2832);
		cairo_fill(cr);
		cairo_arc(cr, 21, 7.5, 1.2, 0, 6.2832);
		cairo_fill(cr);
	} else if (strcmp(id, "license") == 0) {
		icon_fill(cr, 0.36, 0.82, 0.52, 0.12, 0.62, 0.36);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.96);
		rounded(cr, 8, 6, 12, 16, 2);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0.16, 0.58, 0.32, 0.9);
		cairo_set_line_width(cr, 1.4);
		cairo_move_to(cr, 10.5, 11);
		cairo_line_to(cr, 17.5, 11);
		cairo_move_to(cr, 10.5, 14.5);
		cairo_line_to(cr, 16, 14.5);
		cairo_stroke(cr);
	} else {
		icon_fill(cr, 0.62, 0.64, 0.70, 0.38, 0.40, 0.46);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.96);
		cairo_arc(cr, 14, 14, 8, 0, 6.2832);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0.40, 0.42, 0.48, 1);
		cairo_arc(cr, 14, 11.2, 1.15, 0, 6.2832);
		cairo_fill(cr);
		cairo_rectangle(cr, 13.2, 13.4, 1.6, 5.2);
		cairo_fill(cr);
	}
	return FALSE;
}

static GtkWidget *pane(const char *heading) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
	gtk_widget_set_margin_top(box, 18);
	gtk_widget_set_margin_start(box, 28);
	gtk_widget_set_margin_end(box, 28);
	gtk_widget_set_margin_bottom(box, 22);
	GtkWidget *title = gtk_label_new(heading);
	gtk_style_context_add_class(gtk_widget_get_style_context(title), "heading");
	gtk_widget_set_halign(title, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);
	return box;
}

static GtkWidget *card(void) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(box), "card");
	return box;
}

static void add_sep_if_needed(GtkWidget *box) {
	GList *children = gtk_container_get_children(GTK_CONTAINER(box));
	int count = g_list_length(children);
	g_list_free(children);
	if (count == 0) {
		return;
	}
	GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_style_context_add_class(gtk_widget_get_style_context(sep), "rowsep");
	gtk_widget_set_margin_start(sep, 16);
	gtk_box_pack_start(GTK_BOX(box), sep, FALSE, FALSE, 0);
}

static void add_text(GtkWidget *box, const char *text, const char *css_class) {
	add_sep_if_needed(box);
	GtkWidget *label = gtk_label_new(text);
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(label), 56);
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	gtk_widget_set_margin_start(label, 16);
	gtk_widget_set_margin_end(label, 16);
	gtk_widget_set_margin_top(label, 10);
	gtk_widget_set_margin_bottom(label, 10);
	if (css_class != NULL) {
		gtk_style_context_add_class(gtk_widget_get_style_context(label), css_class);
	}
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
}

static void add_note(GtkWidget *box, const char *text) {
	GtkWidget *label = gtk_label_new(text);
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(label), 58);
	gtk_style_context_add_class(gtk_widget_get_style_context(label), "note");
	gtk_widget_set_margin_start(label, 16);
	gtk_widget_set_margin_end(label, 8);
	gtk_widget_set_margin_top(label, -6);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
}

static GtkWidget *add_row(GtkWidget *box, const char *name, const char *value) {
	add_sep_if_needed(box);
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_widget_set_margin_start(row, 16);
	gtk_widget_set_margin_end(row, 14);
	gtk_widget_set_margin_top(row, 9);
	gtk_widget_set_margin_bottom(row, 9);
	GtkWidget *left = gtk_label_new(name);
	gtk_widget_set_halign(left, GTK_ALIGN_START);
	gtk_label_set_xalign(GTK_LABEL(left), 0);
	GtkWidget *right = NULL;
	if (value != NULL) {
		right = gtk_label_new(value);
		gtk_style_context_add_class(gtk_widget_get_style_context(right), "dim");
		gtk_label_set_selectable(GTK_LABEL(right), TRUE);
		gtk_widget_set_halign(right, GTK_ALIGN_END);
	}
	gtk_box_pack_start(GTK_BOX(row), left, FALSE, FALSE, 0);
	if (right != NULL) {
		gtk_box_pack_end(GTK_BOX(row), right, FALSE, FALSE, 0);
	}
	gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
	return row;
}

static gboolean on_glass_switch(GtkSwitch *sw, gboolean state, gpointer data) {
	(void)sw;
	(void)state;
	(void)data;
	send_contrast();
	return FALSE;
}

static GtkWidget *page_appearance(void) {
	GtkWidget *box = pane("Appearance");
	char *raw = read_file("/run/dev-desktop/status", "");
	int live = strstr(raw, "blur=live") != NULL;
	int contrast = strstr(raw, "contrast=1") != NULL;
	g_free(raw);

	GtkWidget *glass = card();
	GtkWidget *row = add_row(glass, "Liquid Glass", NULL);
	GtkWidget *sw = gtk_switch_new();
	gtk_switch_set_active(GTK_SWITCH(sw), !contrast);
	g_signal_connect(sw, "state-set", G_CALLBACK(on_glass_switch), NULL);
	gtk_box_pack_end(GTK_BOX(row), sw, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), glass, FALSE, FALSE, 0);
	if (contrast) {
		add_note(box, "Contrast is on, so surfaces are opaque. It also turns on by itself when the display cannot be read back.");
	} else if (!live) {
		add_note(box, "Glass is the default. This session is still starting the live blur.");
	} else {
		add_note(box, "The menu bar, menus, dock, window title bars, dialogs, and Launchpad bend and blur the live picture behind them.");
	}

	GtkWidget *type = card();
	add_row(type, "System font", "Ubuntu");
	add_row(type, "Monospace", "Ubuntu Mono");
	gtk_box_pack_start(GTK_BOX(box), type, FALSE, FALSE, 0);
	add_note(box, "Ubuntu is the interface font. Scripts it does not cover fall back to Noto Sans, then Noto Sans CJK, then DejaVu Sans.");
	return box;
}

static GtkWidget *page_drivers(void) {
	GtkWidget *box = pane("Drivers");
	add_note(box, "These driver and firmware packages are in this image. NVIDIA's proprietary driver and Broadcom STA are not included. In Terminal, run driver-install --detect.");
	char *manifest = read_file("/usr/share/olympium/driver-manifest.txt",
		"The driver list is not available on this system.");
	GtkWidget *view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 14);
	gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 14);
	gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 10);
	gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), 10);
	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), manifest, -1);
	g_free(manifest);
	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 280);
	gtk_style_context_add_class(gtk_widget_get_style_context(scroll), "card");
	gtk_container_add(GTK_CONTAINER(scroll), view);
	gtk_widget_set_margin_top(scroll, 4);
	gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
	return box;
}

static GtkWidget *page_compat(void) {
	GtkWidget *box = pane("Compatibility");
	GtkWidget *info = card();
	add_text(info,
		"Instruction translation and an application ABI are different layers.\n\n"
		"A pure software translator changes machine instructions between x86_64 and aarch64. "
		"It does not parse Mach-O, run dyld, or provide libSystem, Objective-C, AppKit, or UIKit.\n\n"
		"This desktop can identify a Mach-O or fat header. It does not execute it. "
		"A Linux ELF built here is not an original macOS or iOS GUI application.\n\n"
		"No Apple operating-system image or framework is bundled. "
		"Code signatures and DRM are not bypassed. Applications are not downloaded.", NULL);
	gtk_box_pack_start(GTK_BOX(box), info, FALSE, FALSE, 0);
	return box;
}

static GtkWidget *page_olympium(void) {
	GtkWidget *box = pane("Olympium");
	GtkWidget *info = card();
	add_text(info,
		"If olympium-linux or linkspan-linux is installed, the dock entry starts it. "
		"This image does not vendor that source and does not write back to it.\n\n"
		"The Olympium project refuses macOS and iOS guests on Linux. That policy lives in "
		"the other repository and is not changed here.\n\n"
		"Guest images are not embedded: redistribution is not assumed when the artifact "
		"license is unresolved.\n\n"
		"x86/ARM pure software translation is an Olympium task. This desktop does not require KVM, "
		"and it does not claim that translation is finished or that it runs macOS or iOS applications.",
		NULL);
	gtk_box_pack_start(GTK_BOX(box), info, FALSE, FALSE, 0);
	return box;
}

static GtkWidget *page_license(void) {
	GtkWidget *box = pane("License");
	GtkWidget *info = card();
	int active = g_file_test("/var/lib/olympium/license", G_FILE_TEST_EXISTS);
	add_row(info, "Purchase reminder", active ? "Hidden" : "Every 5 days");
	gtk_box_pack_start(GTK_BOX(box), info, FALSE, FALSE, 0);
	add_note(box,
		"Olympium is a commercial system. For the first 5 days there is no reminder. After that a reminder asks you to purchase a license, and you can always close it. Closing it changes nothing else: every feature keeps working, on a personal computer and on school, business, and bulk installs alike. A recorded license only hides the reminder. Run olympium-activate with your key in Terminal.");
	return box;
}

static GtkWidget *page_about(void) {
	GtkWidget *box = pane("About");
	GtkWidget *info = card();
	struct utsname uts;
	char *branding = read_file("/etc/dev-desktop/branding.conf", "");
	char version[64] = "0.1.0";
	const char *found = strstr(branding, "version=");
	if (found != NULL) {
		snprintf(version, sizeof version, "%.60s", found + 8);
		version[strcspn(version, "\r\n")] = '\0';
	}
	g_free(branding);
	add_row(info, "Name", "Olympium");
	add_row(info, "Version", version);
	if (uname(&uts) == 0) {
		add_row(info, "Processor", uts.machine);
		add_row(info, "Kernel", uts.release);
	}
	add_row(info, "Browser", "Firefox");
	gtk_box_pack_start(GTK_BOX(box), info, FALSE, FALSE, 0);
	add_note(box, "Olympium is a native Linux system with its own desktop. It is not macOS and does not contain Apple software.");
	return box;
}

static void select_pane(const char *id) {
	if (gtk_stack_get_child_by_name(GTK_STACK(stack), id) == NULL) {
		return;
	}
	gtk_stack_set_visible_child_name(GTK_STACK(stack), id);
	for (int i = 0; i < nav_count; i++) {
		const char *bid = g_object_get_data(G_OBJECT(nav_buttons[i]), "pane");
		GtkStyleContext *ctx = gtk_widget_get_style_context(nav_buttons[i]);
		if (bid != NULL && strcmp(bid, id) == 0) {
			gtk_style_context_add_class(ctx, "active");
		} else {
			gtk_style_context_remove_class(ctx, "active");
		}
	}
}

static void on_pane(GtkButton *button, gpointer data) {
	(void)button;
	select_pane((const char *)data);
}

static void on_search(GtkEditable *editable, gpointer data) {
	(void)data;
	const char *query = gtk_entry_get_text(GTK_ENTRY(editable));
	int shown = 0;
	const char *first = NULL;
	int active_hidden = 0;
	const char *active = NULL;
	for (int i = 0; i < nav_count; i++) {
		const char *label = g_object_get_data(G_OBJECT(nav_buttons[i]), "label");
		const char *id = g_object_get_data(G_OBJECT(nav_buttons[i]), "pane");
		int match = query[0] == '\0' || (label != NULL && strcasestr(label, query) != NULL);
		gtk_widget_set_visible(nav_buttons[i], match);
		if (match) {
			shown++;
			if (first == NULL) {
				first = id;
			}
		}
		if (gtk_style_context_has_class(gtk_widget_get_style_context(nav_buttons[i]), "active")) {
			active = id;
			if (!match) {
				active_hidden = 1;
			}
		}
	}
	gtk_widget_set_visible(empty_label, shown == 0);
	if (active_hidden && first != NULL) {
		select_pane(first);
	} else if (active != NULL && query[0] == '\0') {
		select_pane(active);
	}
}

static void add_pane(GtkWidget *side, const char *id, const char *label, GtkWidget *page) {
	gtk_stack_add_named(GTK_STACK(stack), page, id);
	GtkWidget *button = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_style_context_add_class(gtk_widget_get_style_context(button), "place");
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *icon = gtk_drawing_area_new();
	gtk_widget_set_size_request(icon, 28, 28);
	g_signal_connect(icon, "draw", G_CALLBACK(draw_nav_icon), (gpointer)id);
	GtkWidget *text = gtk_label_new(label);
	gtk_label_set_xalign(GTK_LABEL(text), 0);
	gtk_widget_set_halign(text, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), text, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(button), row);
	g_object_set_data_full(G_OBJECT(button), "pane", g_strdup(id), g_free);
	g_object_set_data_full(G_OBJECT(button), "label", g_strdup(label), g_free);
	g_signal_connect(button, "clicked", G_CALLBACK(on_pane), (gpointer)g_object_get_data(G_OBJECT(button), "pane"));
	gtk_box_pack_start(GTK_BOX(side), button, FALSE, FALSE, 0);
	if (nav_count < 8) {
		nav_buttons[nav_count++] = button;
	}
}

int main(int argc, char **argv) {
	gtk_init(&argc, &argv);
	g_object_set(gtk_settings_get_default(), "gtk-font-name", "Ubuntu 12", NULL);
	GtkCssProvider *css = gtk_css_provider_new();
	gtk_css_provider_load_from_data(css,
		"window { background: #f2f2f6; }"
		"label { color: #1d1d1f; }"
		".sidebar { background: #e7e7ec; }"
		"entry {"
		"  background: rgba(120,120,128,0.16); color: #1d1d1f;"
		"  border: none; border-radius: 8px; box-shadow: none;"
		"  padding: 3px 8px; min-height: 26px; caret-color: #0a84ff;"
		"}"
		"button.place {"
		"  padding: 4px 8px; margin: 1px 10px; border-radius: 8px;"
		"  border: none; background: none; box-shadow: none;"
		"}"
		"button.place:hover { background: rgba(0,0,0,0.05); }"
		"button.place.active, button.place.active:hover { background: rgba(0,0,0,0.10); }"
		".heading { font-weight: bold; font-size: 28px; }"
		".dim { color: #8e8e93; }"
		".note { color: #8e8e93; font-size: 12px; }"
		".card {"
		"  background: #ffffff; border: none; border-radius: 12px;"
		"  box-shadow: none; padding: 0;"
		"}"
		"separator.rowsep { background: #e5e5ea; min-height: 1px; }"
		"textview, textview text {"
		"  background: #ffffff; color: #1d1d1f;"
		"  font-family: 'Ubuntu Mono', monospace; font-size: 12px;"
		"}"
		"switch {"
		"  background: #e9e9eb; border: none; border-radius: 12px;"
		"  min-width: 42px; min-height: 24px; background-image: none;"
		"  box-shadow: inset 0 0 0 1px rgba(0,0,0,0.05);"
		"}"
		"switch:checked { background: #34c759; }"
		"switch slider {"
		"  background: #ffffff; border: none; border-radius: 10px;"
		"  min-width: 20px; min-height: 20px;"
		"  box-shadow: 0 1px 2px rgba(0,0,0,0.28);"
		"}"
		, -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
		GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	const char *page = "appearance";
	for (int i = 1; i < argc; i++) {
		if (strncmp(argv[i], "--page=", 7) == 0) {
			page = argv[i] + 7;
		}
	}
	if (strcmp(page, "compat") == 0) {
		page = "compatibility";
	} else if (strcmp(page, "desktop") == 0) {
		page = "appearance";
	}

	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
	gtk_window_set_default_size(GTK_WINDOW(window), 900, 560);
	gtk_window_set_title(GTK_WINDOW(window), "Settings");
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add(GTK_CONTAINER(window), root);

	GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_style_context_add_class(gtk_widget_get_style_context(side), "sidebar");
	gtk_widget_set_size_request(side, 236, -1);
	GtkWidget *search = gtk_search_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(search), "Search");
	gtk_widget_set_margin_top(search, 14);
	gtk_widget_set_margin_start(search, 12);
	gtk_widget_set_margin_end(search, 12);
	gtk_widget_set_margin_bottom(search, 8);
	gtk_box_pack_start(GTK_BOX(side), search, FALSE, FALSE, 0);
	g_signal_connect(search, "changed", G_CALLBACK(on_search), NULL);

	GtkWidget *pad = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
	gtk_box_pack_start(GTK_BOX(side), pad, FALSE, FALSE, 0);
	empty_label = gtk_label_new("No Results");
	gtk_style_context_add_class(gtk_widget_get_style_context(empty_label), "note");
	gtk_widget_set_margin_top(empty_label, 18);
	gtk_widget_set_no_show_all(empty_label, TRUE);
	gtk_widget_hide(empty_label);
	gtk_box_pack_start(GTK_BOX(side), empty_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(root), side, FALSE, FALSE, 0);

	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(root), scroll, TRUE, TRUE, 0);
	stack = gtk_stack_new();
	gtk_container_add(GTK_CONTAINER(scroll), stack);

	add_pane(pad, "appearance", "Appearance", page_appearance());
	add_pane(pad, "drivers", "Drivers", page_drivers());
	add_pane(pad, "compatibility", "Compatibility", page_compat());
	add_pane(pad, "olympium", "Olympium", page_olympium());
	add_pane(pad, "license", "License", page_license());
	add_pane(pad, "about", "About", page_about());

	gtk_widget_show_all(window);
	select_pane(page);
	gtk_main();
	return 0;
}
