/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * An X program that shows a window, hides it, shows it again and destroys
 * it, as many times as it is told to. Hiding an X window takes its Wayland
 * surface away while the window lives on. test-wine.sh runs this to check
 * that the compositor shows the window again and is still standing after.
 *
 *   test-xremap [ROUNDS]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>

static void pause_ms(xcb_connection_t *c, int ms) {
	xcb_flush(c);
	usleep((useconds_t)ms * 1000);
}

int main(int argc, char **argv) {
	int rounds = argc > 1 ? atoi(argv[1]) : 1;
	xcb_connection_t *c = xcb_connect(NULL, NULL);
	if (c == NULL || xcb_connection_has_error(c)) {
		fprintf(stderr, "test-xremap: no X display\n");
		return 2;
	}
	xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
	for (int i = 0; i < rounds; i++) {
		xcb_window_t w = xcb_generate_id(c);
		uint32_t values[] = {screen->white_pixel};
		xcb_create_window(c, XCB_COPY_FROM_PARENT, w, screen->root, 40, 40, 320, 180, 0,
			XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, XCB_CW_BACK_PIXEL, values);
		const char *name = "Desk Remap";
		xcb_change_property(c, XCB_PROP_MODE_REPLACE, w, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
			(uint32_t)strlen(name), name);
		xcb_map_window(c, w);
		pause_ms(c, 250);
		xcb_unmap_window(c, w);
		pause_ms(c, 250);
		xcb_map_window(c, w);
		pause_ms(c, 250);
		/* Hidden first, then destroyed: the order Wine closes a window in. */
		xcb_unmap_window(c, w);
		pause_ms(c, 60);
		xcb_destroy_window(c, w);
		pause_ms(c, 60);
	}
	xcb_disconnect(c);
	return 0;
}
