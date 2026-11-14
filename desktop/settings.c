/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

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

static char *read_file(const char *path) {
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		return g_strdup("Status file is not available yet.");
	}
	char buf[2048];
	size_t n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = '\0';
	return g_strdup(buf);
}

static GtkWidget *page_desktop(void) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_top(box, 16);
	gtk_widget_set_margin_start(box, 16);
	gtk_widget_set_margin_end(box, 16);
	GtkWidget *status = gtk_label_new(read_file("/run/dev-desktop/status"));
	gtk_label_set_xalign(GTK_LABEL(status), 0);
	gtk_label_set_selectable(GTK_LABEL(status), TRUE);
	gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 0);
	GtkWidget *button = gtk_button_new_with_label("Toggle glass / contrast");
	g_signal_connect(button, "clicked", G_CALLBACK(send_contrast), NULL);
	gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
	GtkWidget *note = gtk_label_new(
		"Glass mode blurs the live framebuffer behind the panel and dock.\n"
		"Contrast mode is opaque and does not claim a blur effect.\n"
		"The title comes from /etc/dev-desktop/branding.conf.");
	gtk_label_set_xalign(GTK_LABEL(note), 0);
	gtk_box_pack_start(GTK_BOX(box), note, FALSE, FALSE, 0);
	return box;
}

static GtkWidget *page_text(const char *text) {
	GtkWidget *view = gtk_text_view_new();
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD);
	gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), text, -1);
	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(scroll), view);
	return scroll;
}

int main(int argc, char **argv) {
	gtk_init(&argc, &argv);
	g_object_set(gtk_settings_get_default(),
		"gtk-decoration-layout", "close,minimize,maximize:",
		"gtk-font-name", "Ubuntu 11", NULL);
	const char *page = "desktop";
	for (int i = 1; i < argc; i++) {
		if (strncmp(argv[i], "--page=", 7) == 0) {
			page = argv[i] + 7;
		}
	}
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
	gtk_window_set_default_size(GTK_WINDOW(window), 760, 520);
	gtk_window_set_title(GTK_WINDOW(window), "Settings");
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	GtkWidget *notebook = gtk_notebook_new();
	gtk_container_add(GTK_CONTAINER(window), notebook);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page_desktop(), gtk_label_new("Desktop"));
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page_text(
		"Instruction translation and an application ABI are different layers.\n\n"
		"A pure software translator changes machine instructions between x86_64 and aarch64. "
		"It does not parse Mach-O, run dyld, or provide libSystem, Objective-C, AppKit, or UIKit.\n\n"
		"This desktop can identify a Mach-O or fat header. It does not execute it. "
		"A Linux ELF built here is not an original macOS or iOS GUI application.\n\n"
		"Olympium commit 25a592cfd65a4f07449ca03e919bfa97626bb164 refuses macOS and iOS guests on Linux. "
		"That policy lives in the other repository and is not changed here.\n\n"
		"No Apple operating-system image or framework is bundled. "
		"Code signatures and DRM are not bypassed. Applications are not downloaded."),
		gtk_label_new("Compatibility"));
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page_text(
		"Olympium integration is read-only against yezicheng2011/aaa at 25a592c.\n\n"
		"If olympium-linux or linkspan-linux from that revision is installed, the dock launches it. "
		"This image does not vendor that source and does not write back to it.\n\n"
		"The pinned revision has a Linux OpenHarmony window with launch and stop, host pointer input, "
		"and a separate Android Cuttlefish path. Both are work in progress. "
		"Guest images are not embedded: redistribution is not assumed when the artifact license is unresolved.\n\n"
		"x86/ARM pure software translation is an Olympium task. This desktop does not require KVM, "
		"and it does not claim that translation is finished or that it runs macOS or iOS applications."),
		gtk_label_new("Olympium"));
	if (strcmp(page, "compat") == 0) {
		gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 1);
	} else if (strcmp(page, "olympium") == 0) {
		gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 2);
	}
	gtk_widget_show_all(window);
	gtk_main();
	return 0;
}
