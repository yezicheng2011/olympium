/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Boot mark for GRUB. The Olympium dragon from dragon.h.
 */
#include "dragon.h"

#include <cairo.h>
#include <stdio.h>

static void dragon(cairo_t *cr, double x, double y, double size) {
	draw_dragon_mark(cr, x, y, size, 1.00, 0.92, 0.54);
}

int main(int argc, char **argv) {
	const char *path = argc > 1 ? argv[1] : "splash.png";
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1280, 800);
	cairo_t *cr = cairo_create(surface);
	cairo_pattern_t *pat = cairo_pattern_create_linear(0, 0, 0, 800);
	cairo_pattern_add_color_stop_rgb(pat, 0, 0.10, 0.13, 0.20);
	cairo_pattern_add_color_stop_rgb(pat, 1, 0.16, 0.18, 0.22);
	cairo_set_source(cr, pat);
	cairo_paint(cr);
	cairo_pattern_destroy(pat);
	dragon(cr, 540, 210, 200);
	cairo_set_source_rgba(cr, 0.95, 0.95, 0.96, 1);
	cairo_select_font_face(cr, "Ubuntu", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 36);
	cairo_move_to(cr, 548, 470);
	cairo_show_text(cr, "Olympium");
	cairo_destroy(cr);
	cairo_status_t status = cairo_surface_write_to_png(surface, path);
	cairo_surface_destroy(surface);
	return status == CAIRO_STATUS_SUCCESS ? 0 : 1;
}
