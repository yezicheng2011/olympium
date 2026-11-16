/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * The Olympium mark: an Eastern dragon in profile. A long body with no
 * wings, coiled in an S and tapering to a tufted tail, a head with a long
 * snout, an open jaw and antlers, a mane, a whisker, two clawed feet, a
 * ridge along the back, and the pearl it chases. Drawn here from curves;
 * there is no image file. Below 40 pixels the fine parts are left out so the
 * outline stays readable in the menu bar.
 */
#ifndef DEV_DRAGON_H
#define DEV_DRAGON_H

#include <cairo.h>
#include <math.h>

static inline void dragon_bez(const double p[8], double t, double *x, double *y) {
	double u = 1 - t;
	*x = u * u * u * p[0] + 3 * u * u * t * p[2] + 3 * u * t * t * p[4] + t * t * t * p[6];
	*y = u * u * u * p[1] + 3 * u * u * t * p[3] + 3 * u * t * t * p[5] + t * t * t * p[7];
}

/* Draws the mark in a square of the given size at x, y, in one colour. */
static inline void draw_dragon_mark(cairo_t *cr, double x, double y, double size,
		double r, double g, double b) {
	/* The spine, tail tip to neck, in a unit square. */
	static const double spine[3][8] = {
		{0.10, 0.60, 0.02, 0.86, 0.30, 0.98, 0.48, 0.84},
		{0.48, 0.84, 0.66, 0.70, 0.20, 0.62, 0.30, 0.44},
		{0.30, 0.44, 0.38, 0.28, 0.62, 0.52, 0.72, 0.36},
	};
	enum { PER = 28, N = 3 * PER + 1 };
	double px[N], py[N], lx[N], ly[N], rx[N], ry[N];
	int detail = size >= 40;
	for (int s = 0; s < 3; s++) {
		for (int i = 0; i < PER; i++) {
			dragon_bez(spine[s], (double)i / PER, &px[s * PER + i], &py[s * PER + i]);
		}
	}
	px[N - 1] = spine[2][6];
	py[N - 1] = spine[2][7];
	cairo_save(cr);
	cairo_translate(cr, x, y);
	cairo_scale(cr, size, size);
	cairo_set_source_rgba(cr, r, g, b, 1);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	/* Everything is drawn into a group so the eye can be cut out of it. */
	cairo_push_group(cr);
	cairo_set_source_rgba(cr, r, g, b, 1);
	for (int i = 0; i < N; i++) {
		int a = i > 0 ? i - 1 : 0;
		int c = i < N - 1 ? i + 1 : N - 1;
		double dx = px[c] - px[a];
		double dy = py[c] - py[a];
		double d = hypot(dx, dy);
		if (d <= 0) {
			d = 1;
		}
		dx /= d;
		dy /= d;
		double f = (double)i / (N - 1);
		double grow = f * 1.25 < 1 ? f * 1.25 : 1;
		double w = (detail ? 0.010 : 0.022) + (detail ? 0.052 : 0.058) * sin(grow * 1.5708);
		lx[i] = px[i] - dy * w;
		ly[i] = py[i] + dx * w;
		rx[i] = px[i] + dy * w;
		ry[i] = py[i] - dx * w;
		if (detail && i % 7 == 3 && f > 0.15 && f < 0.95) {
			/* One spike of the ridge along the back. */
			cairo_move_to(cr, px[i] - dy * w * 0.8 - dx * 0.020, py[i] + dx * w * 0.8 - dy * 0.020);
			cairo_line_to(cr, px[i] - dy * (w + 0.045) - dx * 0.030, py[i] + dx * (w + 0.045) - dy * 0.030);
			cairo_line_to(cr, px[i] - dy * w * 0.8 + dx * 0.025, py[i] + dx * w * 0.8 + dy * 0.025);
			cairo_close_path(cr);
			cairo_fill(cr);
		}
	}
	cairo_move_to(cr, lx[0], ly[0]);
	for (int i = 1; i < N; i++) {
		cairo_line_to(cr, lx[i], ly[i]);
	}
	for (int i = N - 1; i >= 0; i--) {
		cairo_line_to(cr, rx[i], ry[i]);
	}
	cairo_close_path(cr);
	cairo_fill(cr);
	/* The tuft at the end of the tail. */
	cairo_move_to(cr, 0.10, 0.60);
	cairo_curve_to(cr, 0.06, 0.52, 0.10, 0.46, 0.17, 0.44);
	cairo_curve_to(cr, 0.13, 0.50, 0.14, 0.55, 0.12, 0.61);
	cairo_close_path(cr);
	cairo_fill(cr);
	/* The head: brow, long snout, open jaw. */
	cairo_move_to(cr, 0.66, 0.30);
	cairo_curve_to(cr, 0.72, 0.20, 0.84, 0.20, 0.97, 0.27);
	cairo_line_to(cr, 0.985, 0.315);
	cairo_line_to(cr, 0.86, 0.335);
	cairo_line_to(cr, 0.95, 0.40);
	cairo_curve_to(cr, 0.86, 0.45, 0.76, 0.46, 0.68, 0.43);
	cairo_close_path(cr);
	cairo_fill(cr);
	/* An antler. */
	cairo_set_line_width(cr, detail ? 0.032 : 0.05);
	cairo_move_to(cr, 0.72, 0.25);
	cairo_curve_to(cr, 0.66, 0.16, 0.60, 0.12, 0.52, 0.10);
	cairo_stroke(cr);
	if (detail) {
		cairo_set_line_width(cr, 0.024);
		cairo_move_to(cr, 0.63, 0.155);
		cairo_line_to(cr, 0.64, 0.07);
		cairo_stroke(cr);
		/* The second antler, behind the first. */
		cairo_move_to(cr, 0.76, 0.23);
		cairo_curve_to(cr, 0.74, 0.14, 0.72, 0.10, 0.74, 0.03);
		cairo_stroke(cr);
		/* The mane, two locks behind the jaw. */
		cairo_set_line_width(cr, 0.03);
		cairo_move_to(cr, 0.67, 0.33);
		cairo_curve_to(cr, 0.58, 0.31, 0.54, 0.34, 0.50, 0.30);
		cairo_move_to(cr, 0.68, 0.39);
		cairo_curve_to(cr, 0.60, 0.40, 0.56, 0.44, 0.52, 0.42);
		cairo_stroke(cr);
		/* A whisker from the nose. */
		cairo_set_line_width(cr, 0.016);
		cairo_move_to(cr, 0.95, 0.30);
		cairo_curve_to(cr, 1.02, 0.40, 0.96, 0.50, 1.00, 0.58);
		cairo_stroke(cr);
		/* Two feet, three claws each. */
		static const double legs[2][4] = {{0.40, 0.86, 0.40, 0.99}, {0.36, 0.40, 0.47, 0.33}};
		for (int k = 0; k < 2; k++) {
			cairo_set_line_width(cr, 0.03);
			cairo_move_to(cr, legs[k][0], legs[k][1]);
			cairo_line_to(cr, legs[k][2], legs[k][3]);
			cairo_stroke(cr);
			cairo_set_line_width(cr, 0.016);
			double angle = atan2(legs[k][3] - legs[k][1], legs[k][2] - legs[k][0]);
			for (int c = -1; c <= 1; c++) {
				cairo_move_to(cr, legs[k][2], legs[k][3]);
				cairo_line_to(cr, legs[k][2] + 0.05 * cos(angle + c * 0.6),
					legs[k][3] + 0.05 * sin(angle + c * 0.6));
			}
			cairo_stroke(cr);
		}
	}
	/* The pearl. */
	cairo_arc(cr, 0.86, 0.62, detail ? 0.055 : 0.07, 0, 6.2832);
	cairo_fill(cr);
	/* The eye, cut out. */
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_arc(cr, 0.80, 0.275, detail ? 0.018 : 0.028, 0, 6.2832);
	cairo_fill(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_pop_group_to_source(cr);
	cairo_paint(cr);
	cairo_restore(cr);
}

#endif
