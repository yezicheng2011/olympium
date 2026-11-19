/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * An X window that is repainted every few milliseconds until it is killed,
 * so the compositor has a client that keeps changing. test-render.sh uses it
 * to check the glass cache while something is animating.
 *
 *   test-animate [PAUSE_MS]
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <xcb/xcb.h>
int main(int argc, char **argv) {
	int ms = argc > 1 ? atoi(argv[1]) : 16;
	xcb_connection_t *c = xcb_connect(NULL, NULL);
	xcb_screen_t *s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
	xcb_window_t w = xcb_generate_id(c);
	uint32_t v[] = {s->white_pixel};
	xcb_create_window(c, XCB_COPY_FROM_PARENT, w, s->root, 40, 40, 640, 400, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, s->root_visual, XCB_CW_BACK_PIXEL, v);
	xcb_map_window(c, w);
	xcb_gcontext_t gc = xcb_generate_id(c);
	uint32_t gv[] = {0};
	xcb_create_gc(c, gc, w, XCB_GC_FOREGROUND, gv);
	for (int i = 0; ; i++) {
		uint32_t col = (i * 0x050301) & 0xffffff;
		xcb_change_gc(c, gc, XCB_GC_FOREGROUND, &col);
		xcb_rectangle_t r = {(i * 7) % 500, (i * 5) % 300, 140, 100};
		xcb_poly_fill_rectangle(c, w, gc, 1, &r);
		xcb_flush(c);
		usleep(ms * 1000);
	}
}
