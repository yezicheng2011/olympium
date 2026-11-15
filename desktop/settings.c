/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * Settings: a sidebar of panes on the left and one pane on the right.
 * The compositor draws the glass title bar and the left window controls.
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <unistd.h>

static GtkWidget *stack;

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

static GtkWidget *pane(const char *heading) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_margin_top(box, 22);
	gtk_widget_set_margin_start(box, 26);
	gtk_widget_set_margin_end(box, 26);
	gtk_widget_set_margin_bottom(box, 18);
	GtkWidget *title = gtk_label_new(heading);
	gtk_style_context_add_class(gtk_widget_get_style_context(title), "heading");
	gtk_widget_set_halign(title, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);
	return box;
}

static GtkWidget *card(void) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_style_context_add_class(gtk_widget_get_style_context(box), "card");
	return box;
}

static void add_text(GtkWidget *box, const char *text, const char *css_class) {
	GtkWidget *label = gtk_label_new(text);
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(label), 62);
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	if (css_class != NULL) {
		gtk_style_context_add_class(gtk_widget_get_style_context(label), css_class);
	}
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
}

static void add_row(GtkWidget *box, const char *name, const char *value) {
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	GtkWidget *left = gtk_label_new(name);
	gtk_widget_set_halign(left, GTK_ALIGN_START);
	GtkWidget *right = gtk_label_new(value);
	gtk_style_context_add_class(gtk_widget_get_style_context(right), "dim");
	gtk_label_set_selectable(GTK_LABEL(right), TRUE);
	gtk_box_pack_start(GTK_BOX(row), left, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(row), right, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
}

static GtkWidget *page_appearance(void) {
	GtkWidget *box = pane("Appearance");
	char *raw = read_file("/run/dev-desktop/status", "");
	int live = strstr(raw, "blur=live") != NULL;
	int contrast = strstr(raw, "contrast=1") != NULL;
	g_free(raw);
	GtkWidget *glass = card();
	add_row(glass, "Glass", contrast ? "Off" : (live ? "On" : "Starting"));
	if (contrast) {
		add_text(glass, "Contrast mode draws opaque surfaces. It also turns on by itself "
			"when the display cannot be read back.", "dim");
	} else {
		add_text(glass, "The menu bar, menus, dock, window title bars, and Launchpad bend and blur "
			"the live picture behind them on every frame.", "dim");
	}
	gtk_box_pack_start(GTK_BOX(box), glass, FALSE, FALSE, 0);
	GtkWidget *button = gtk_button_new_with_label(contrast ? "Turn glass on" : "Reduce transparency");
	gtk_widget_set_halign(button, GTK_ALIGN_START);
	g_signal_connect(button, "clicked", G_CALLBACK(send_contrast), NULL);
	gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
	return box;
}

static GtkWidget *page_drivers(void) {
	GtkWidget *box = pane("Drivers");
	add_text(box,
		"These driver and firmware packages are in this image. NVIDIA's proprietary driver and "
		"Broadcom STA are not included. Open Terminal and run driver-install --detect to see "
		"whether either applies to this computer.", "dim");
	char *manifest = read_file("/usr/share/olympium/driver-manifest.txt",
		"The driver list is not available on this system.");
	GtkWidget *view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 10);
	gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 8);
	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), manifest, -1);
	g_free(manifest);
	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_style_context_add_class(gtk_widget_get_style_context(scroll), "card");
	gtk_container_add(GTK_CONTAINER(scroll), view);
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
	add_row(info, "Purchase reminder", active ? "Hidden, a license is recorded" : "Shown every 5 days");
	add_text(info,
		"Olympium is a commercial system. For the first 5 days there is no reminder. After that "
		"a reminder asks you to purchase a license, and you can always close it. Closing it "
		"changes nothing else: every feature keeps working, on a personal computer and on "
		"school, business, and bulk installs alike.\n\n"
		"A recorded license only hides the reminder. Run olympium-activate with your key in Terminal.",
		"dim");
	gtk_box_pack_start(GTK_BOX(box), info, FALSE, FALSE, 0);
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
		add_row(info, "Processor type", uts.machine);
		add_row(info, "Kernel", uts.release);
	}
	gtk_box_pack_start(GTK_BOX(box), info, FALSE, FALSE, 0);
	add_text(box, "Olympium is a native Linux system with its own desktop. It is not macOS "
		"and does not contain Apple software.", "dim");
	return box;
}

static void on_pane(GtkButton *button, gpointer data) {
	(void)button;
	gtk_stack_set_visible_child_name(GTK_STACK(stack), (const char *)data);
}

static void add_pane(GtkWidget *side, const char *id, const char *label, GtkWidget *page) {
	gtk_stack_add_named(GTK_STACK(stack), page, id);
	GtkWidget *button = gtk_button_new_with_label(label);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_style_context_add_class(gtk_widget_get_style_context(button), "place");
	gtk_widget_set_halign(gtk_bin_get_child(GTK_BIN(button)), GTK_ALIGN_START);
	g_signal_connect(button, "clicked", G_CALLBACK(on_pane), (gpointer)id);
	gtk_box_pack_start(GTK_BOX(side), button, FALSE, FALSE, 0);
}

int main(int argc, char **argv) {
	gtk_init(&argc, &argv);
	g_object_set(gtk_settings_get_default(), "gtk-font-name", "Ubuntu 12", NULL);
	GtkCssProvider *css = gtk_css_provider_new();
	gtk_css_provider_load_from_data(css,
		"window { background: #f5f5f7; }"
		"label { color: #1d1d1f; }"
		".sidebar { background: #e9e9ed; border-right: 1px solid #d9d9de; }"
		"button.place { padding: 5px 10px; margin: 0 8px; border-radius: 7px;"
		"  border: none; background: none; box-shadow: none; }"
		"button.place:hover { background: rgba(0,0,0,0.07); }"
		".heading { font-weight: bold; font-size: 20px; }"
		".dim { color: #6e6e73; }"
		".card { background: #ffffff; border: 1px solid #e2e2e6; border-radius: 10px;"
		"  padding: 12px 14px; }"
		"textview, textview text { background: #ffffff; color: #1d1d1f; font-size: 11px; }"
		"button { color: #1d1d1f; }"
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
	gtk_window_set_default_size(GTK_WINDOW(window), 780, 500);
	gtk_window_set_title(GTK_WINDOW(window), "Settings");
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add(GTK_CONTAINER(window), root);
	GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_style_context_add_class(gtk_widget_get_style_context(side), "sidebar");
	gtk_widget_set_size_request(side, 200, -1);
	GtkWidget *pad = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_widget_set_margin_top(pad, 12);
	gtk_box_pack_start(GTK_BOX(side), pad, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(root), side, FALSE, FALSE, 0);

	stack = gtk_stack_new();
	gtk_box_pack_start(GTK_BOX(root), stack, TRUE, TRUE, 0);
	add_pane(pad, "appearance", "Appearance", page_appearance());
	add_pane(pad, "drivers", "Drivers", page_drivers());
	add_pane(pad, "compatibility", "Compatibility", page_compat());
	add_pane(pad, "olympium", "Olympium", page_olympium());
	add_pane(pad, "license", "License", page_license());
	add_pane(pad, "about", "About", page_about());

	gtk_widget_show_all(window);
	if (gtk_stack_get_child_by_name(GTK_STACK(stack), page) != NULL) {
		gtk_stack_set_visible_child_name(GTK_STACK(stack), page);
	}
	gtk_main();
	return 0;
}
