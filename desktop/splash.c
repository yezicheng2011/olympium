/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * Boot mark for GRUB. Original geometric dragon, not an apple.
 */
#include <cairo.h>
#include <stdio.h>

static void dragon(cairo_t *cr, double x, double y, double size) {
	cairo_save(cr);
	cairo_translate(cr, x, y);
	cairo_set_source_rgba(cr, 0.93, 0.78, 0.45, 1);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_width(cr, size * 0.08);
	cairo_move_to(cr, size * 0.15, size * 0.78);
	cairo_curve_to(cr, size * 0.02, size * 0.40, size * 0.78, size * 0.66,
		size * 0.46, size * 0.28);
	cairo_stroke(cr);
	cairo_set_line_width(cr, size * 0.05);
	cairo_move_to(cr, size * 0.44, size * 0.28);
	cairo_line_to(cr, size * 0.62, size * 0.06);
	cairo_stroke(cr);
	cairo_arc(cr, size * 0.62, size * 0.34, size * 0.12, 0, 6.2832);
	cairo_fill(cr);
	cairo_arc(cr, size * 0.86, size * 0.18, size * 0.045, 0, 6.2832);
	cairo_fill(cr);
	cairo_restore(cr);
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
	dragon(cr, 560, 250, 160);
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
