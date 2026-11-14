/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * Native Wayland compositor and shell. Window buffers are composited by
 * wlroots. The panel, dock, and launcher sample that framebuffer and run a
 * live box blur. They are not a pre-blurred wallpaper.
 */
#define _GNU_SOURCE

#include "blur.h"

#include <cairo.h>
#include <glib.h>
#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <pango/pangocairo.h>
#include <pixman.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

enum cursor_mode {
	CURSOR_PASSTHROUGH,
	CURSOR_MOVE,
	CURSOR_RESIZE
};

struct app_entry {
	const char *id;
	const char *label;
	const char *path;
	const char *arg;
};

static const struct app_entry APPS[] = {
	{"files", "Files", "/usr/bin/dev-files", NULL},
	{"terminal", "Terminal", "/usr/bin/foot", NULL},
	{"settings", "Settings", "/usr/bin/dev-settings", NULL},
	{"olympium", "Olympium", "/usr/libexec/dev-desktop/olympium-launch", NULL},
	{"compat", "Compatibility", "/usr/bin/dev-settings", "--page=compat"},
};

enum { APP_COUNT = sizeof APPS / sizeof APPS[0] };

struct view {
	struct wl_list link;
	struct server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	int placed;
};

struct keyboard {
	struct wl_list link;
	struct server *server;
	struct wlr_keyboard *wlr_keyboard;
	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

struct output {
	struct wl_list link;
	struct server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

struct server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	struct wlr_scene_rect *wallpaper;
	struct wlr_scene_rect *band;
	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_surface;
	struct wl_list views;
	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;
	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;
	enum cursor_mode cursor_mode;
	struct view *grabbed;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;
	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;
	int screen_w;
	int screen_h;
	int contrast;
	int launcher;
	int blur_ok;
	int blur_fail;
	unsigned frames;
	char blur_error[160];
	char title[96];
	char arch[32];
	char filter[64];
	char notice[160];
	int press_app;
	int press_contrast;
	int press_brand;
	int hover_app;
	int press_light;
	struct view *press_view;
	char frame_title[128];
	struct wl_listener new_decoration;
	char shot_path[256];
	int shot_written;
	int control_fd;
	struct wl_event_source *shot_timer;
	struct wl_event_source *tick_timer;
};

static const int PANEL_H = 28;
static const int TITLE_H = 32;
static const int DOCK_H = 64;
static const int DOCK_MARGIN = 8;
static const int DOCK_SLOT = 56;
static const char *UI_FONT = "Ubuntu 12";
static const char *UI_FONT_SM = "Ubuntu 11";

static void focus_view(struct view *view, struct wlr_surface *surface);

static void load_branding(struct server *server) {
	snprintf(server->title, sizeof server->title, "Olympium");
	FILE *f = fopen("/etc/dev-desktop/branding.conf", "r");
	if (f == NULL) {
		f = fopen("branding/branding.conf", "r");
	}
	if (f == NULL) {
		return;
	}
	char line[256];
	while (fgets(line, sizeof line, f) != NULL) {
		if (line[0] == '#' || line[0] == '\n') {
			continue;
		}
		char *eq = strchr(line, '=');
		if (eq == NULL) {
			continue;
		}
		*eq = '\0';
		char *value = eq + 1;
		value[strcspn(value, "\r\n")] = '\0';
		if (strcmp(line, "title") == 0 && value[0] != '\0') {
			snprintf(server->title, sizeof server->title, "%s", value);
		}
	}
	fclose(f);
}

static void write_status(struct server *server) {
	mkdir("/run/dev-desktop", 0755);
	FILE *f = fopen("/run/dev-desktop/status", "w");
	if (f == NULL) {
		return;
	}
	fprintf(f,
		"title=%s\narch=%s\ncontrast=%d\nblur=%s\nframes=%u\nblur_error=%s\nnotice=%s\n",
		server->title, server->arch, server->contrast,
		server->contrast ? "off-contrast" : (server->blur_ok ? "live" : "unavailable"),
		server->frames, server->blur_error, server->notice);
	fclose(f);
}

static void launch_app(struct server *server, const struct app_entry *app) {
	if (access(app->path, X_OK) != 0) {
		snprintf(server->notice, sizeof server->notice, "Missing %s", app->path);
		return;
	}
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		if (app->arg != NULL) {
			execl(app->path, app->path, app->arg, (char *)NULL);
		} else {
			execl(app->path, app->path, (char *)NULL);
		}
		_exit(127);
	}
	snprintf(server->notice, sizeof server->notice, "Launched %s", app->label);
	server->launcher = 0;
	server->filter[0] = '\0';
}

static int app_matches(const struct app_entry *app, const char *filter) {
	if (filter[0] == '\0') {
		return 1;
	}
	return strcasestr(app->label, filter) != NULL || strcasestr(app->id, filter) != NULL;
}

static void dock_box(const struct server *server, struct wlr_box *box) {
	int width = 20 + APP_COUNT * DOCK_SLOT;
	if (width > server->screen_w - 48) {
		width = server->screen_w - 48;
	}
	box->width = width;
	box->height = DOCK_H;
	box->x = (server->screen_w - width) / 2;
	box->y = server->screen_h - DOCK_H - DOCK_MARGIN;
}

static void launcher_box(const struct server *server, struct wlr_box *box) {
	box->width = 520;
	box->height = 360;
	if (box->width > server->screen_w - 40) {
		box->width = server->screen_w - 40;
	}
	box->x = (server->screen_w - box->width) / 2;
	box->y = 80;
}

static int point_in(const struct wlr_box *box, double x, double y) {
	return x >= box->x && y >= box->y && x < box->x + box->width && y < box->y + box->height;
}

static int dock_app_at(const struct server *server, double x, double y) {
	struct wlr_box dock;
	dock_box(server, &dock);
	if (!point_in(&dock, x, y)) {
		return -1;
	}
	int index = (int)((x - dock.x - 10) / DOCK_SLOT);
	if (index < 0 || index >= APP_COUNT) {
		return -1;
	}
	return index;
}

static int hit_contrast(const struct server *server, double x, double y) {
	(void)server;
	(void)x;
	(void)y;
	return 0;
}

static int hit_brand(const struct server *server, double x, double y) {
	(void)server;
	return x >= 6 && x < 34 && y >= 0 && y < PANEL_H;
}

/* Original geometric mark: a short serpentine body, a horn, and a pearl.
 * It is not an apple, a leaf, or a bitten circle. */
static void draw_dragon(cairo_t *cr, double x, double y, double size, double r, double g, double b) {
	cairo_save(cr);
	cairo_translate(cr, x, y);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_source_rgba(cr, r, g, b, 1);
	cairo_set_line_width(cr, size * 0.16);
	cairo_move_to(cr, size * 0.18, size * 0.78);
	cairo_curve_to(cr, size * 0.05, size * 0.42, size * 0.72, size * 0.62,
		size * 0.48, size * 0.30);
	cairo_stroke(cr);
	cairo_set_line_width(cr, size * 0.11);
	cairo_move_to(cr, size * 0.46, size * 0.30);
	cairo_line_to(cr, size * 0.62, size * 0.08);
	cairo_stroke(cr);
	cairo_arc(cr, size * 0.62, size * 0.34, size * 0.16, 0, 6.2832);
	cairo_fill(cr);
	cairo_arc(cr, size * 0.84, size * 0.22, size * 0.07, 0, 6.2832);
	cairo_fill(cr);
	cairo_restore(cr);
}

static void draw_icon(cairo_t *cr, const char *id, double x, double y) {
	cairo_save(cr);
	cairo_translate(cr, x, y);
	cairo_new_path(cr);
	cairo_arc(cr, 6, 6, 6, 3.1416, 4.7124);
	cairo_arc(cr, 30, 6, 6, 4.7124, 6.2832);
	cairo_arc(cr, 30, 30, 6, 0, 1.5708);
	cairo_arc(cr, 6, 30, 6, 1.5708, 3.1416);
	cairo_close_path(cr);
	if (strcmp(id, "files") == 0) {
		cairo_set_source_rgba(cr, 0.93, 0.78, 0.42, 1);
	} else if (strcmp(id, "terminal") == 0) {
		cairo_set_source_rgba(cr, 0.22, 0.24, 0.28, 1);
	} else if (strcmp(id, "settings") == 0) {
		cairo_set_source_rgba(cr, 0.55, 0.58, 0.64, 1);
	} else if (strcmp(id, "olympium") == 0) {
		cairo_set_source_rgba(cr, 0.16, 0.22, 0.30, 1);
	} else {
		cairo_set_source_rgba(cr, 0.45, 0.52, 0.62, 1);
	}
	cairo_fill(cr);
	cairo_restore(cr);
	if (strcmp(id, "files") == 0) {
		cairo_set_source_rgba(cr, 0.45, 0.30, 0.08, 1);
		cairo_rectangle(cr, x + 8, y + 16, 20, 12);
		cairo_rectangle(cr, x + 8, y + 11, 10, 6);
		cairo_fill(cr);
	} else if (strcmp(id, "terminal") == 0) {
		cairo_set_source_rgba(cr, 0.72, 0.93, 0.78, 1);
		cairo_set_line_width(cr, 1.6);
		cairo_move_to(cr, x + 11, y + 14);
		cairo_line_to(cr, x + 18, y + 18);
		cairo_line_to(cr, x + 11, y + 22);
		cairo_stroke(cr);
	} else if (strcmp(id, "settings") == 0) {
		cairo_set_source_rgba(cr, 0.12, 0.13, 0.16, 1);
		cairo_set_line_width(cr, 2.2);
		cairo_arc(cr, x + 18, y + 18, 6, 0, 6.2832);
		cairo_stroke(cr);
		cairo_arc(cr, x + 18, y + 18, 2.2, 0, 6.2832);
		cairo_fill(cr);
	} else if (strcmp(id, "olympium") == 0) {
		draw_dragon(cr, x + 4, y + 4, 28, 0.93, 0.78, 0.45);
	} else {
		cairo_set_source_rgba(cr, 0.95, 0.96, 0.97, 1);
		for (int i = 0; i < 3; i++) {
			cairo_rectangle(cr, x + 10, y + 10 + i * 7, 16, 3);
		}
		cairo_fill(cr);
	}
}

static int paint_text(cairo_t *cr, const char *font, const char *text,
		double x, double y, double r, double g, double b) {
	PangoLayout *layout = pango_cairo_create_layout(cr);
	pango_layout_set_text(layout, text, -1);
	PangoFontDescription *desc = pango_font_description_from_string(font);
	pango_layout_set_font_description(layout, desc);
	cairo_set_source_rgba(cr, r, g, b, 1);
	cairo_move_to(cr, x, y);
	pango_cairo_show_layout(cr, layout);
	int width = 0;
	int height = 0;
	pango_layout_get_pixel_size(layout, &width, &height);
	pango_font_description_free(desc);
	g_object_unref(layout);
	return width;
}

static cairo_surface_t *frosted_surface(uint8_t *bgra, int width, int height, int stride,
		int contrast, double radius, int dark) {
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			bgra[y * stride + x * 4 + 3] = 255;
		}
	}
	cairo_surface_t *src = cairo_image_surface_create_for_data(
		bgra, CAIRO_FORMAT_ARGB32, width, height, stride);
	cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
	cairo_t *cr = cairo_create(out);
	cairo_save(cr);
	if (radius != 0) {
		double r = radius < 0 ? -radius : radius;
		int top_only = radius < 0;
		if (r > width / 2.0) {
			r = width / 2.0;
		}
		if (!top_only && r > height / 2.0) {
			r = height / 2.0;
		}
		cairo_new_path(cr);
		cairo_arc(cr, r, r, r, 3.1416, 1.5 * 3.1416);
		cairo_arc(cr, width - r, r, r, 1.5 * 3.1416, 6.2832);
		if (top_only) {
			cairo_line_to(cr, width, height);
			cairo_line_to(cr, 0, height);
		} else {
			cairo_arc(cr, width - r, height - r, r, 0, 0.5 * 3.1416);
			cairo_arc(cr, r, height - r, r, 0.5 * 3.1416, 3.1416);
		}
		cairo_close_path(cr);
		cairo_clip(cr);
	}
	if (!contrast && bgra != NULL) {
		cairo_set_source_surface(cr, src, 0, 0);
		cairo_paint(cr);
		if (dark) {
			cairo_set_source_rgba(cr, 0.07, 0.08, 0.10, 0.38);
		} else {
			cairo_set_source_rgba(cr, 0.97, 0.97, 0.98, 0.42);
		}
		cairo_paint(cr);
	} else if (dark) {
		cairo_set_source_rgba(cr, 0.12, 0.13, 0.15, 1);
		cairo_paint(cr);
	} else {
		cairo_set_source_rgba(cr, 0.96, 0.96, 0.97, 1);
		cairo_paint(cr);
	}
	cairo_restore(cr);
	cairo_surface_destroy(src);
	cairo_destroy(cr);
	return out;
}

static struct wlr_texture *texture_from_cairo(struct wlr_renderer *renderer,
		cairo_surface_t *surface) {
	cairo_surface_flush(surface);
	unsigned char *data = cairo_image_surface_get_data(surface);
	int width = cairo_image_surface_get_width(surface);
	int height = cairo_image_surface_get_height(surface);
	int stride = cairo_image_surface_get_stride(surface);
	return wlr_texture_from_pixels(renderer, DRM_FORMAT_ARGB8888,
		(uint32_t)stride, (uint32_t)width, (uint32_t)height, data);
}

static int read_region(struct server *server, int x, int y, int w, int h, uint8_t *buf, int stride) {
	if (w <= 0 || h <= 0) {
		return 0;
	}
	if (x < 0) {
		x = 0;
	}
	if (y < 0) {
		y = 0;
	}
	bool ok = wlr_renderer_read_pixels(server->renderer, DRM_FORMAT_ARGB8888,
		(uint32_t)stride, (uint32_t)w, (uint32_t)h,
		(uint32_t)x, (uint32_t)y, 0, 0, buf);
	if (!ok) {
		ok = wlr_renderer_read_pixels(server->renderer, DRM_FORMAT_XRGB8888,
			(uint32_t)stride, (uint32_t)w, (uint32_t)h,
			(uint32_t)x, (uint32_t)y, 0, 0, buf);
	}
	return ok;
}

static void draw_chrome_on(struct server *server, cairo_t *cr, cairo_surface_t *surface,
		const struct wlr_box *box, int which) {
	(void)surface;
	if (which == 0) {
		draw_dragon(cr, 8, 4, 20, 0.93, 0.78, 0.45);
		const char *app = server->frame_title[0] != '\0' ? server->frame_title : server->title;
		int app_w = paint_text(cr, "Ubuntu Bold 13", app, 36, 5, 0.96, 0.96, 0.97);
		const char *menus[] = {"File", "Edit", "View", "Go", "Window", "Help"};
		double mx = 36 + app_w + 22;
		for (int i = 0; i < 6; i++) {
			int item_w = paint_text(cr, UI_FONT, menus[i], mx, 5, 0.90, 0.91, 0.93);
			mx += item_w + 18;
		}
		time_t now = time(NULL);
		struct tm tm;
		localtime_r(&now, &tm);
		char clock[32];
		strftime(clock, sizeof clock, "%a %H:%M", &tm);
		paint_text(cr, UI_FONT, clock, server->screen_w - 118, 6, 0.96, 0.96, 0.97);
		return;
	}
	if (which == 1) {
		for (int i = 0; i < APP_COUNT; i++) {
			double ix = 12 + i * DOCK_SLOT + (DOCK_SLOT - 36) / 2.0;
			draw_icon(cr, APPS[i].id, ix, 14);
		}
		return;
	}
	if (which == 4) {
		const char *label = server->frame_title[0] != '\0' ? server->frame_title : "";
		paint_text(cr, UI_FONT_SM, label, 8, 3, 0.12, 0.12, 0.14);
		return;
	}
	if (which == 3) {
		const double lights[][3] = {
			{1.0, 0.37, 0.34},
			{0.996, 0.74, 0.18},
			{0.16, 0.78, 0.25}
		};
		for (int i = 0; i < 3; i++) {
			cairo_set_source_rgba(cr, lights[i][0], lights[i][1], lights[i][2], 1);
			cairo_arc(cr, 16 + i * 18, box->height / 2.0, 6.0, 0, 6.2832);
			cairo_fill(cr);
		}
		const char *title = server->frame_title[0] != '\0' ? server->frame_title : "Window";
		paint_text(cr, UI_FONT, title, box->width / 2.0 - strlen(title) * 3.4, 8,
			0.12, 0.12, 0.14);
		return;
	}
	paint_text(cr, "Ubuntu Bold 18", server->title, 28, 24, 0.10, 0.10, 0.12);
	char prompt[96];
	snprintf(prompt, sizeof prompt, "Search  %s", server->filter);
	paint_text(cr, UI_FONT, prompt, 28, 64, 0.25, 0.27, 0.30);
	int row = 0;
	for (int i = 0; i < APP_COUNT; i++) {
		if (!app_matches(&APPS[i], server->filter)) {
			continue;
		}
		double y = 110 + row * 42;
		draw_icon(cr, APPS[i].id, 28, y);
		paint_text(cr, UI_FONT, APPS[i].label, 78, y + 8, 0.10, 0.10, 0.12);
		row++;
	}
	if (server->notice[0] != '\0') {
		paint_text(cr, UI_FONT_SM, server->notice, 28, box->height - 36,
			0.35, 0.22, 0.12);
	}
}

static struct wlr_texture *make_chrome(struct server *server, int x, int y, int w, int h,
		double radius, int which) {
	int stride = w * 4;
	uint8_t *pixels = calloc((size_t)stride, (size_t)h);
	if (pixels == NULL) {
		return NULL;
	}
	int sampled = 0;
	if (!server->contrast) {
		sampled = read_region(server, x, y, w, h, pixels, stride);
		if (sampled) {
			dev_blur_bgra(pixels, w, h, stride, 8);
			server->blur_ok = 1;
			server->blur_error[0] = '\0';
		} else {
			server->blur_fail++;
			snprintf(server->blur_error, sizeof server->blur_error,
				"renderer readback failed");
			if (server->blur_fail > 8) {
				server->contrast = 1;
				snprintf(server->notice, sizeof server->notice,
					"Glass blur unavailable; contrast mode is on");
			}
		}
	}
	int dark = which == 0 || which == 1;
	cairo_surface_t *base = frosted_surface(pixels, w, h, stride,
		server->contrast || !sampled, radius, dark);
	cairo_t *cr = cairo_create(base);
	struct wlr_box box = {.x = x, .y = y, .width = w, .height = h};
	draw_chrome_on(server, cr, base, &box, which);
	cairo_destroy(cr);
	struct wlr_texture *texture = texture_from_cairo(server->renderer, base);
	cairo_surface_destroy(base);
	free(pixels);
	return texture;
}

static void render_texture_at(struct server *server, struct wlr_output *output,
		struct wlr_texture *texture, int x, int y) {
	if (texture == NULL) {
		return;
	}
	wlr_render_texture(server->renderer, texture, output->transform_matrix, x, y, 1.0f);
}

static void write_shot(struct server *server, int width, int height) {
	if (server->shot_path[0] == '\0' || server->shot_written) {
		return;
	}
	if (server->frames < 40) {
		return;
	}
	int stride = width * 4;
	uint8_t *pixels = calloc((size_t)stride, (size_t)height);
	if (pixels == NULL) {
		return;
	}
	if (!read_region(server, 0, 0, width, height, pixels, stride)) {
		free(pixels);
		snprintf(server->blur_error, sizeof server->blur_error, "screenshot readback failed");
		return;
	}
	FILE *f = fopen(server->shot_path, "wb");
	if (f == NULL) {
		free(pixels);
		return;
	}
	fprintf(f, "P6\n%d %d\n255\n", width, height);
	for (int i = 0; i < width * height; i++) {
		fputc(pixels[i * 4 + 2], f);
		fputc(pixels[i * 4 + 1], f);
		fputc(pixels[i * 4 + 0], f);
	}
	fclose(f);
	free(pixels);
	server->shot_written = 1;
	wlr_log(WLR_INFO, "wrote %s", server->shot_path);
	wl_display_terminate(server->wl_display);
}

static int view_title_box(struct view *view, struct wlr_box *box) {
	if (view->scene_tree == NULL || !view->scene_tree->node.enabled) {
		return 0;
	}
	struct wlr_box geo;
	wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);
	int width = geo.width > 0 ? geo.width : view->xdg_toplevel->base->surface->current.width;
	int height = geo.height > 0 ? geo.height : view->xdg_toplevel->base->surface->current.height;
	if (width < 80 || height < 40) {
		return 0;
	}
	box->x = view->scene_tree->node.x + geo.x;
	box->y = view->scene_tree->node.y + geo.y - TITLE_H;
	box->width = width;
	box->height = TITLE_H;
	return 1;
}

static int title_light(const struct wlr_box *box, double x, double y) {
	if (!point_in(box, x, y)) {
		return 0;
	}
	double cy = box->y + box->height / 2.0;
	for (int i = 0; i < 3; i++) {
		double cx = box->x + 16 + i * 18;
		double dx = x - cx;
		double dy = y - cy;
		if (dx * dx + dy * dy <= 64) {
			return i + 1;
		}
	}
	return 0;
}

static struct wlr_texture *make_shadow(struct server *server, int width, int height) {
	int pad = 36;
	int sw = width + pad * 2;
	int sh = height + pad * 2;
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
	cairo_t *cr = cairo_create(surface);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.45);
	double r = 16;
	double x = pad;
	double y = pad + 6;
	cairo_new_path(cr);
	cairo_arc(cr, x + r, y + r, r, 3.1416, 4.7124);
	cairo_arc(cr, x + width - r, y + r, r, 4.7124, 6.2832);
	cairo_arc(cr, x + width - r, y + height - r, r, 0, 1.5708);
	cairo_arc(cr, x + r, y + height - r, r, 1.5708, 3.1416);
	cairo_close_path(cr);
	cairo_fill(cr);
	cairo_destroy(cr);
	cairo_surface_flush(surface);
	uint8_t *pixels = cairo_image_surface_get_data(surface);
	int stride = cairo_image_surface_get_stride(surface);
	dev_blur_bgra(pixels, sw, sh, stride, 10);
	cr = cairo_create(surface);
	cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	cairo_rectangle(cr, pad, pad, width, height);
	cairo_fill(cr);
	cairo_destroy(cr);
	struct wlr_texture *texture = texture_from_cairo(server->renderer, surface);
	cairo_surface_destroy(surface);
	return texture;
}

static void output_frame(struct wl_listener *listener, void *data) {
	(void)data;
	struct output *output = wl_container_of(listener, output, frame);
	struct server *server = output->server;
	struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
		server->scene, output->wlr_output);
	if (scene_output == NULL) {
		return;
	}
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	if (!wlr_scene_output_build_state(scene_output, &state, NULL)) {
		wlr_output_state_finish(&state);
		return;
	}
	server->screen_w = output->wlr_output->width;
	server->screen_h = output->wlr_output->height;
	struct wlr_texture *panel = NULL;
	struct wlr_texture *dock = NULL;
	struct wlr_texture *launcher = NULL;
	if (server->screen_w > 0 && server->screen_h > 0 &&
			wlr_renderer_begin_with_buffer(server->renderer, state.buffer)) {
		struct view *front = NULL;
		struct view *view;
		wl_list_for_each(view, &server->views, link) {
			if (view->scene_tree->node.enabled) {
				front = view;
				break;
			}
		}
		if (front != NULL && front->xdg_toplevel->title != NULL && front->xdg_toplevel->title[0] != '\0') {
			snprintf(server->frame_title, sizeof server->frame_title, "%s", front->xdg_toplevel->title);
		} else {
			snprintf(server->frame_title, sizeof server->frame_title, "%s", server->title);
		}
		wl_list_for_each_reverse(view, &server->views, link) {
			struct wlr_box title;
			if (!view_title_box(view, &title)) {
				continue;
			}
			int frame_h = title.height + view->xdg_toplevel->base->surface->current.height;
			struct wlr_texture *shadow = make_shadow(server, title.width, frame_h);
			render_texture_at(server, output->wlr_output, shadow, title.x - 36, title.y - 24);
			wlr_texture_destroy(shadow);
			char saved[128];
			snprintf(saved, sizeof saved, "%s", server->frame_title);
			if (view->xdg_toplevel->title != NULL && view->xdg_toplevel->title[0] != '\0') {
				snprintf(server->frame_title, sizeof server->frame_title, "%s",
					view->xdg_toplevel->title);
			}
			struct wlr_texture *bar = make_chrome(server, title.x, title.y,
				title.width, title.height, -12, 3);
			render_texture_at(server, output->wlr_output, bar, title.x, title.y);
			wlr_texture_destroy(bar);
			snprintf(server->frame_title, sizeof server->frame_title, "%s", saved);
		}
		panel = make_chrome(server, 0, 0, server->screen_w, PANEL_H, 0, 0);
		struct wlr_box dockb;
		dock_box(server, &dockb);
		dock = make_chrome(server, dockb.x, dockb.y, dockb.width, dockb.height, 20, 1);
		render_texture_at(server, output->wlr_output, panel, 0, 0);
		render_texture_at(server, output->wlr_output, dock, dockb.x, dockb.y);
		if (server->hover_app >= 0 && server->hover_app < APP_COUNT) {
			char saved[128];
			snprintf(saved, sizeof saved, "%s", server->frame_title);
			snprintf(server->frame_title, sizeof server->frame_title, "%s",
				APPS[server->hover_app].label);
			struct wlr_texture *tip = make_chrome(server,
				dockb.x + 10 + server->hover_app * DOCK_SLOT, dockb.y - 28,
				DOCK_SLOT, 22, 8, 4);
			snprintf(server->frame_title, sizeof server->frame_title, "%s", saved);
			render_texture_at(server, output->wlr_output, tip,
				dockb.x + 10 + server->hover_app * DOCK_SLOT, dockb.y - 28);
			wlr_texture_destroy(tip);
		}
		if (server->launcher) {
			struct wlr_box lb;
			launcher_box(server, &lb);
			launcher = make_chrome(server, lb.x, lb.y, lb.width, lb.height, 18, 2);
			render_texture_at(server, output->wlr_output, launcher, lb.x, lb.y);
		}
		server->frames++;
		write_shot(server, server->screen_w, server->screen_h);
		wlr_renderer_end(server->renderer);
		pixman_region32_t full;
		pixman_region32_init_rect(&full, 0, 0, server->screen_w, server->screen_h);
		wlr_output_state_set_damage(&state, &full);
		pixman_region32_fini(&full);
	}
	wlr_output_commit_state(output->wlr_output, &state);
	wlr_output_state_finish(&state);
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
	wlr_texture_destroy(panel);
	wlr_texture_destroy(dock);
	wlr_texture_destroy(launcher);
	if ((server->frames % 30) == 0) {
		write_status(server);
	}
}

static void output_request_state(struct wl_listener *listener, void *data) {
	struct output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct output *output = wl_container_of(listener, output, destroy);
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);
}

static void ensure_wallpaper(struct server *server, int width, int height) {
	if (server->wallpaper != NULL || width <= 0 || height <= 0) {
		return;
	}
	int bands = 18;
	int band_h = height / bands;
	struct wlr_scene_rect *first = NULL;
	for (int i = 0; i < bands; i++) {
		float t = (float)i / (float)(bands - 1);
		float color[4] = {
			0.10f + t * 0.05f,
			0.13f + t * 0.04f,
			0.20f + t * 0.03f,
			1.0f
		};
		int y = i * band_h;
		int h = (i == bands - 1) ? height - y : band_h;
		struct wlr_scene_rect *band = wlr_scene_rect_create(&server->scene->tree, width, h, color);
		wlr_scene_node_set_position(&band->node, 0, y);
		if (first == NULL) {
			first = band;
		}
		wlr_scene_node_lower_to_bottom(&band->node);
	}
	server->wallpaper = first;
}

static void server_new_output(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;
	wlr_output_init_render(wlr_output, server->allocator, server->renderer);
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL) {
		wlr_output_state_set_mode(&state, mode);
	} else {
		int32_t w = wlr_output->width > 0 ? wlr_output->width : 1280;
		int32_t h = wlr_output->height > 0 ? wlr_output->height : 800;
		wlr_output_state_set_custom_mode(&state, w, h, 60000);
	}
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	struct output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	output->server = server;
	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);
	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);
	wl_list_insert(&server->outputs, &output->link);

	struct wlr_output_layout_output *layout_output =
		wlr_output_layout_add_auto(server->output_layout, wlr_output);
	struct wlr_scene_output *scene_output = wlr_scene_output_create(server->scene, wlr_output);
	wlr_scene_output_layout_add_output(server->scene_layout, layout_output, scene_output);
	ensure_wallpaper(server, wlr_output->width, wlr_output->height);
	server->screen_w = wlr_output->width;
	server->screen_h = wlr_output->height;
}

static void focus_view(struct view *view, struct wlr_surface *surface) {
	if (view == NULL) {
		return;
	}
	struct server *server = view->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *previous = seat->keyboard_state.focused_surface;
	if (previous == surface) {
		return;
	}
	if (previous != NULL) {
		struct wlr_xdg_toplevel *previous_toplevel =
			wlr_xdg_toplevel_try_from_wlr_surface(previous);
		if (previous_toplevel != NULL) {
			wlr_xdg_toplevel_set_activated(previous_toplevel, false);
		}
	}
	wlr_scene_node_raise_to_top(&view->scene_tree->node);
	wl_list_remove(&view->link);
	wl_list_insert(&server->views, &view->link);
	wlr_xdg_toplevel_set_activated(view->xdg_toplevel, true);
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(seat, view->xdg_toplevel->base->surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
	(void)data;
	struct keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
		&keyboard->wlr_keyboard->modifiers);
}

static struct view *view_at(struct server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
	if (scene_surface == NULL) {
		return NULL;
	}
	*surface = scene_surface->surface;
	struct wlr_scene_tree *tree = node->parent;
	while (tree != NULL && tree->node.data == NULL) {
		tree = tree->node.parent;
	}
	if (tree == NULL) {
		return NULL;
	}
	return tree->node.data;
}

static int chrome_contains(struct server *server, double x, double y) {
	if (y < PANEL_H) {
		return 1;
	}
	struct wlr_box dock;
	dock_box(server, &dock);
	if (point_in(&dock, x, y)) {
		return 1;
	}
	if (server->launcher) {
		struct wlr_box box;
		launcher_box(server, &box);
		if (point_in(&box, x, y)) {
			return 1;
		}
	}
	struct view *view;
	wl_list_for_each(view, &server->views, link) {
		struct wlr_box title;
		if (view_title_box(view, &title) && point_in(&title, x, y)) {
			return 1;
		}
	}
	return 0;
}

static void launcher_activate_first(struct server *server) {
	for (int i = 0; i < APP_COUNT; i++) {
		if (app_matches(&APPS[i], server->filter)) {
			launch_app(server, &APPS[i]);
			return;
		}
	}
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
	struct keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);
	int handled = 0;
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (int i = 0; i < nsyms; i++) {
			if (syms[i] == XKB_KEY_Super_L || syms[i] == XKB_KEY_Super_R) {
				server->launcher = !server->launcher;
				server->filter[0] = '\0';
				handled = 1;
			}
		}
		if (!handled && server->launcher) {
			for (int i = 0; i < nsyms; i++) {
				if (syms[i] == XKB_KEY_Escape) {
					server->launcher = 0;
					server->filter[0] = '\0';
					handled = 1;
				} else if (syms[i] == XKB_KEY_Return) {
					launcher_activate_first(server);
					handled = 1;
				} else if (syms[i] == XKB_KEY_BackSpace) {
					size_t n = strlen(server->filter);
					if (n > 0) {
						server->filter[n - 1] = '\0';
					}
					handled = 1;
				} else {
					char utf[8];
					int len = xkb_keysym_to_utf8(syms[i], utf, sizeof utf);
					if (len > 1 && strlen(server->filter) + (size_t)len < sizeof server->filter) {
						strcat(server->filter, utf);
						handled = 1;
					}
				}
			}
		}
	}
	if (!handled) {
		wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(server->seat, event->time_msec,
			event->keycode, event->state);
	}
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

static void server_new_keyboard(struct server *server, struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);
	struct keyboard *keyboard = calloc(1, sizeof(*keyboard));
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);
	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);
	wlr_seat_set_keyboard(server->seat, wlr_keyboard);
	wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_pointer(struct server *server, struct wlr_input_device *device) {
	wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_input(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	default:
		break;
	}
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

static void seat_request_cursor(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused = server->seat->pointer_state.focused_client;
	if (focused == event->seat_client) {
		wlr_cursor_set_surface(server->cursor, event->surface,
			event->hotspot_x, event->hotspot_y);
	}
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static void reset_cursor_mode(struct server *server) {
	server->cursor_mode = CURSOR_PASSTHROUGH;
	server->grabbed = NULL;
}

static void process_cursor_move(struct server *server) {
	struct view *view = server->grabbed;
	wlr_scene_node_set_position(&view->scene_tree->node,
		server->cursor->x - server->grab_x,
		server->cursor->y - server->grab_y);
}

static void process_cursor_motion(struct server *server, uint32_t time) {
	server->hover_app = dock_app_at(server, server->cursor->x, server->cursor->y);
	if (server->cursor_mode == CURSOR_MOVE) {
		process_cursor_move(server);
		return;
	}
	double sx, sy;
	struct wlr_surface *surface = NULL;
	if (chrome_contains(server, server->cursor->x, server->cursor->y)) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
		wlr_seat_pointer_clear_focus(server->seat);
		return;
	}
	struct view *view = view_at(server, server->cursor->x, server->cursor->y,
		&surface, &sx, &sy);
	if (view == NULL) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
	if (surface != NULL) {
		wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
	} else {
		wlr_seat_pointer_clear_focus(server->seat);
	}
}

static void server_cursor_motion(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

static void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

static void press_chrome(struct server *server) {
	double x = server->cursor->x;
	double y = server->cursor->y;
	server->press_app = -1;
	server->press_contrast = 0;
	server->press_brand = 0;
	server->press_light = 0;
	server->press_view = NULL;
	if (server->launcher) {
		struct wlr_box box;
		launcher_box(server, &box);
		if (point_in(&box, x, y)) {
			int row = (int)((y - box.y - 110) / 42);
			if (row >= 0) {
				int seen = 0;
				for (int i = 0; i < APP_COUNT; i++) {
					if (!app_matches(&APPS[i], server->filter)) {
						continue;
					}
					if (seen == row) {
						server->press_app = i;
						return;
					}
					seen++;
				}
			}
			return;
		}
		server->launcher = 0;
		return;
	}
	struct view *view;
	wl_list_for_each(view, &server->views, link) {
		struct wlr_box title;
		int light;
		if (!view_title_box(view, &title)) {
			continue;
		}
		light = title_light(&title, x, y);
		if (light != 0) {
			server->press_light = light;
			server->press_view = view;
			return;
		}
	}
	if (hit_contrast(server, x, y)) {
		server->press_contrast = 1;
		return;
	}
	if (hit_brand(server, x, y)) {
		server->press_brand = 1;
		return;
	}
	int app = dock_app_at(server, x, y);
	if (app >= 0) {
		server->press_app = app;
	}
}

static void release_chrome(struct server *server) {
	double x = server->cursor->x;
	double y = server->cursor->y;
	if (server->press_view != NULL && server->press_light != 0) {
		struct wlr_box title;
		if (view_title_box(server->press_view, &title) &&
				title_light(&title, x, y) == server->press_light) {
			if (server->press_light == 1) {
				wlr_xdg_toplevel_send_close(server->press_view->xdg_toplevel);
			} else if (server->press_light == 2) {
				wlr_scene_node_set_enabled(&server->press_view->scene_tree->node, false);
			} else if (server->press_light == 3) {
				int width = server->screen_w - 80;
				int height = server->screen_h - PANEL_H - DOCK_H - TITLE_H - 36;
				wlr_xdg_toplevel_set_size(server->press_view->xdg_toplevel, width, height);
				wlr_scene_node_set_position(&server->press_view->scene_tree->node,
					40, PANEL_H + 8);
			}
		}
	} else if (server->press_contrast && hit_contrast(server, x, y)) {
		server->contrast = !server->contrast;
		snprintf(server->notice, sizeof server->notice,
			server->contrast ? "Contrast mode" : "Glass mode");
	} else if (server->press_brand && hit_brand(server, x, y)) {
		server->launcher = 1;
		server->filter[0] = '\0';
	} else if (server->press_app >= 0 && server->press_app == dock_app_at(server, x, y)) {
		launch_app(server, &APPS[server->press_app]);
	} else if (server->press_app >= 0 && server->launcher) {
		launch_app(server, &APPS[server->press_app]);
	}
	server->press_app = -1;
	server->press_contrast = 0;
	server->press_brand = 0;
	server->press_light = 0;
	server->press_view = NULL;
}

static void server_cursor_button(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	if (chrome_contains(server, server->cursor->x, server->cursor->y) ||
			server->press_app >= 0 || server->press_brand || server->press_contrast) {
		if (event->state == WLR_BUTTON_PRESSED) {
			press_chrome(server);
		} else {
			release_chrome(server);
		}
		wlr_seat_pointer_clear_focus(server->seat);
		return;
	}
	wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
	if (event->state == WLR_BUTTON_RELEASED) {
		reset_cursor_mode(server);
		return;
	}
	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct view *view = view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	uint32_t modifiers = 0;
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (keyboard != NULL) {
		modifiers = wlr_keyboard_get_modifiers(keyboard);
	}
	if (view != NULL && (modifiers & WLR_MODIFIER_ALT) && event->button == BTN_LEFT) {
		server->grabbed = view;
		server->cursor_mode = CURSOR_MOVE;
		server->grab_x = server->cursor->x - view->scene_tree->node.x;
		server->grab_y = server->cursor->y - view->scene_tree->node.y;
		focus_view(view, surface);
		return;
	}
	focus_view(view, surface);
}

static void server_cursor_axis(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	if (chrome_contains(server, server->cursor->x, server->cursor->y)) {
		return;
	}
	wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation,
		event->delta, event->delta_discrete, event->source);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
	(void)data;
	struct server *server = wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

static void view_map(struct wl_listener *listener, void *data) {
	(void)data;
	struct view *view = wl_container_of(listener, view, map);
	wl_list_insert(&view->server->views, &view->link);
	if (!view->placed) {
		int y = PANEL_H + TITLE_H + 36;
		int x = 180;
		wlr_scene_node_set_position(&view->scene_tree->node, x, y);
		view->placed = 1;
	}
	focus_view(view, view->xdg_toplevel->base->surface);
}

static void view_unmap(struct wl_listener *listener, void *data) {
	(void)data;
	struct view *view = wl_container_of(listener, view, unmap);
	if (view == view->server->grabbed) {
		reset_cursor_mode(view->server);
	}
	wl_list_remove(&view->link);
}

static void view_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct view *view = wl_container_of(listener, view, destroy);
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->commit.link);
	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_fullscreen.link);
	free(view);
}

static void view_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct view *view = wl_container_of(listener, view, commit);
	if (view->xdg_toplevel->base->initial_commit) {
		wlr_xdg_toplevel_set_size(view->xdg_toplevel, 0, 0);
	}
}

static void view_request_move(struct wl_listener *listener, void *data) {
	(void)data;
	struct view *view = wl_container_of(listener, view, request_move);
	view->server->grabbed = view;
	view->server->cursor_mode = CURSOR_MOVE;
	view->server->grab_x = view->server->cursor->x - view->scene_tree->node.x;
	view->server->grab_y = view->server->cursor->y - view->scene_tree->node.y;
}

static void view_request_resize(struct wl_listener *listener, void *data) {
	struct view *view = wl_container_of(listener, view, request_resize);
	struct wlr_xdg_toplevel_resize_event *event = data;
	view->server->grabbed = view;
	view->server->cursor_mode = CURSOR_RESIZE;
	view->server->resize_edges = event->edges;
}

static void view_request_maximize(struct wl_listener *listener, void *data) {
	(void)data;
	struct view *view = wl_container_of(listener, view, request_maximize);
	wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

static void view_request_fullscreen(struct wl_listener *listener, void *data) {
	(void)data;
	struct view *view = wl_container_of(listener, view, request_fullscreen);
	wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

static void server_new_xdg_surface(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, new_xdg_surface);
	struct wlr_xdg_surface *xdg_surface = data;
	if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
		struct wlr_xdg_surface *parent =
			wlr_xdg_surface_try_from_wlr_surface(xdg_surface->popup->parent);
		if (parent == NULL) {
			return;
		}
		struct wlr_scene_tree *parent_tree = parent->data;
		xdg_surface->data = wlr_scene_xdg_surface_create(parent_tree, xdg_surface);
		return;
	}
	if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		return;
	}
	struct view *view = calloc(1, sizeof(*view));
	view->server = server;
	view->xdg_toplevel = xdg_surface->toplevel;
	view->scene_tree = wlr_scene_xdg_surface_create(&server->scene->tree, xdg_surface);
	view->scene_tree->node.data = view;
	xdg_surface->data = view->scene_tree;
	view->map.notify = view_map;
	wl_signal_add(&xdg_surface->surface->events.map, &view->map);
	view->unmap.notify = view_unmap;
	wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);
	view->destroy.notify = view_destroy;
	wl_signal_add(&xdg_surface->events.destroy, &view->destroy);
	view->commit.notify = view_commit;
	wl_signal_add(&xdg_surface->surface->events.commit, &view->commit);
	view->request_move.notify = view_request_move;
	wl_signal_add(&xdg_surface->toplevel->events.request_move, &view->request_move);
	view->request_resize.notify = view_request_resize;
	wl_signal_add(&xdg_surface->toplevel->events.request_resize, &view->request_resize);
	view->request_maximize.notify = view_request_maximize;
	wl_signal_add(&xdg_surface->toplevel->events.request_maximize, &view->request_maximize);
	view->request_fullscreen.notify = view_request_fullscreen;
	wl_signal_add(&xdg_surface->toplevel->events.request_fullscreen, &view->request_fullscreen);
}

static int tick_frame(void *data) {
	struct server *server = data;
	struct output *output;
	wl_list_for_each(output, &server->outputs, link) {
		wlr_output_schedule_frame(output->wlr_output);
	}
	wl_event_source_timer_update(server->tick_timer, 200);
	return 0;
}

static int on_control(int fd, uint32_t mask, void *data) {
	(void)mask;
	struct server *server = data;
	int client = accept(fd, NULL, NULL);
	if (client < 0) {
		return 0;
	}
	char buf[128];
	ssize_t n = read(client, buf, sizeof buf - 1);
	if (n > 0) {
		buf[n] = '\0';
		if (strncmp(buf, "contrast toggle", 15) == 0) {
			server->contrast = !server->contrast;
		} else if (strncmp(buf, "contrast 1", 10) == 0) {
			server->contrast = 1;
		} else if (strncmp(buf, "contrast 0", 10) == 0) {
			server->contrast = 0;
		}
		write_status(server);
	}
	close(client);
	return 0;
}

static void setup_control(struct server *server) {
	mkdir("/run/dev-desktop", 0755);
	unlink("/run/dev-desktop/control");
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		return;
	}
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof addr.sun_path, "/run/dev-desktop/control");
	if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
		close(fd);
		return;
	}
	listen(fd, 4);
	fcntl(fd, F_SETFL, O_NONBLOCK);
	struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
	wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE, on_control, server);
	server->control_fd = fd;
}

static void add_headless_output(struct wlr_backend *backend, void *data) {
	struct wlr_backend **found = data;
	if (*found == NULL && wlr_backend_is_headless(backend)) {
		*found = backend;
	}
}

struct deco_hook {
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct wl_listener request;
	struct wl_listener destroy;
};

static void decoration_request(struct wl_listener *listener, void *data) {
	(void)data;
	struct deco_hook *hook = wl_container_of(listener, hook, request);
	wlr_xdg_toplevel_decoration_v1_set_mode(hook->decoration,
		WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void decoration_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct deco_hook *hook = wl_container_of(listener, hook, destroy);
	wl_list_remove(&hook->request.link);
	wl_list_remove(&hook->destroy.link);
	free(hook);
}

static void new_decoration(struct wl_listener *listener, void *data) {
	(void)listener;
	struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
	struct deco_hook *hook = calloc(1, sizeof(*hook));
	hook->decoration = decoration;
	hook->request.notify = decoration_request;
	wl_signal_add(&decoration->events.request_mode, &hook->request);
	hook->destroy.notify = decoration_destroy;
	wl_signal_add(&decoration->events.destroy, &hook->destroy);
	wlr_xdg_toplevel_decoration_v1_set_mode(decoration,
		WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void usage(const char *argv0) {
	fprintf(stderr, "Usage: %s [-s startup-command]\n", argv0);
}

int main(int argc, char *argv[]) {
	wlr_log_init(WLR_INFO, NULL);
	char *startup_cmd = NULL;
	int opt;
	while ((opt = getopt(argc, argv, "s:h")) != -1) {
		switch (opt) {
		case 's':
			startup_cmd = optarg;
			break;
		default:
			usage(argv[0]);
			return opt == 'h' ? 0 : 1;
		}
	}
	signal(SIGCHLD, SIG_IGN);

	struct server server = {0};
	server.press_app = -1;
	server.hover_app = -1;
	load_branding(&server);
	struct utsname uts;
	if (uname(&uts) == 0) {
		snprintf(server.arch, sizeof server.arch, "%s", uts.machine);
	} else {
		snprintf(server.arch, sizeof server.arch, "unknown");
	}
	const char *shot = getenv("DEV_DESK_SHOT");
	if (shot != NULL) {
		snprintf(server.shot_path, sizeof server.shot_path, "%s", shot);
	}
	if (getenv("DEV_DESK_CONTRAST") != NULL) {
		server.contrast = 1;
	}

	server.wl_display = wl_display_create();
	server.backend = wlr_backend_autocreate(server.wl_display, NULL);
	if (server.backend == NULL) {
		wlr_log(WLR_ERROR, "failed to create backend");
		return 1;
	}
	server.renderer = wlr_renderer_autocreate(server.backend);
	if (server.renderer == NULL) {
		wlr_log(WLR_ERROR, "failed to create renderer");
		return 1;
	}
	wlr_renderer_init_wl_display(server.renderer, server.wl_display);
	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	if (server.allocator == NULL) {
		wlr_log(WLR_ERROR, "failed to create allocator");
		return 1;
	}
	wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);
	wlr_viewporter_create(server.wl_display);
	wlr_screencopy_manager_v1_create(server.wl_display);

	server.output_layout = wlr_output_layout_create();
	wlr_xdg_output_manager_v1_create(server.wl_display, server.output_layout);
	wl_list_init(&server.outputs);
	server.new_output.notify = server_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	server.scene = wlr_scene_create();
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

	wl_list_init(&server.views);
	server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
	server.new_xdg_surface.notify = server_new_xdg_surface;
	wl_signal_add(&server.xdg_shell->events.new_surface, &server.new_xdg_surface);
	struct wlr_xdg_decoration_manager_v1 *decorations =
		wlr_xdg_decoration_manager_v1_create(server.wl_display);
	server.new_decoration.notify = new_decoration;
	wl_signal_add(&decorations->events.new_toplevel_decoration, &server.new_decoration);

	server.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
	server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	server.cursor_mode = CURSOR_PASSTHROUGH;
	server.cursor_motion.notify = server_cursor_motion;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
	server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);
	server.cursor_button.notify = server_cursor_button;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);
	server.cursor_axis.notify = server_cursor_axis;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
	server.cursor_frame.notify = server_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

	wl_list_init(&server.keyboards);
	server.new_input.notify = server_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);
	server.seat = wlr_seat_create(server.wl_display, "seat0");
	server.request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);
	server.request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server.seat->events.request_set_selection, &server.request_set_selection);

	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (socket == NULL) {
		return 1;
	}
	if (!wlr_backend_start(server.backend)) {
		wlr_log(WLR_ERROR, "failed to start backend");
		return 1;
	}
	if (getenv("DEV_DESK_HEADLESS") != NULL && wl_list_empty(&server.outputs)) {
		struct wlr_backend *headless = NULL;
		if (wlr_backend_is_multi(server.backend)) {
			wlr_multi_for_each_backend(server.backend, add_headless_output, &headless);
		} else if (wlr_backend_is_headless(server.backend)) {
			headless = server.backend;
		}
		if (headless != NULL) {
			wlr_headless_add_output(headless, 1280, 800);
		}
	}

	setenv("WAYLAND_DISPLAY", socket, 1);
	setup_control(&server);
	struct wl_event_loop *loop = wl_display_get_event_loop(server.wl_display);
	server.tick_timer = wl_event_loop_add_timer(loop, tick_frame, &server);
	wl_event_source_timer_update(server.tick_timer, 200);
	write_status(&server);

	if (startup_cmd != NULL) {
		if (fork() == 0) {
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (char *)NULL);
			_exit(127);
		}
	}
	wlr_log(WLR_INFO, "compositor on WAYLAND_DISPLAY=%s title=%s", socket, server.title);
	wl_display_run(server.wl_display);
	wl_display_destroy_clients(server.wl_display);
	wl_display_destroy(server.wl_display);
	return server.shot_path[0] != '\0' && !server.shot_written ? 2 : 0;
}
