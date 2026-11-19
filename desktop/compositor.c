/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * Native Wayland compositor and shell. Window buffers are composited by
 * wlroots. The panel, dock, and launcher sample that framebuffer and run a
 * live box blur. They are not a pre-blurred wallpaper.
 */
#define _GNU_SOURCE

#include "dragon.h"
#include "blur.h"

#include <cairo.h>
#include <dirent.h>
#include <glib.h>
#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <pixman.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/render/allocator.h>
#include <wlr/render/pixman.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include "xwayland-compat.h"
#include "macicon.h"
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_damage_ring.h>
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
#ifdef OLYMPIUM_TEST_INPUT
#include <wlr/types/wlr_virtual_pointer_v1.h>
#endif
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
	{"launchpad", "Launchpad", "", NULL},
	{"files", "Files", "/usr/bin/dev-files", NULL},
	{"firefox", "Firefox", "/usr/libexec/dev-desktop/firefox-launch", NULL},
	{"terminal", "Terminal", "/usr/bin/foot", NULL},
	{"settings", "Settings", "/usr/bin/dev-settings", NULL},
	{"olympium", "Olympium", "/usr/libexec/dev-desktop/olympium-launch", NULL},
	{"compat", "Compatibility", "/usr/bin/dev-settings", "--page=compat"},
	{"testapp", "Test App", "/usr/bin/olympium-testapp", NULL},
};

enum { APP_COUNT = sizeof APPS / sizeof APPS[0] };

enum { PAD_MAX = 256 };

/* 0 to 6 are the menus in the bar, 7 is the one under the dragon mark, and 8
 * is quick settings, under the two-sliders glyph at the right. */
enum { MENU_DRAGON = 7, MENU_QUICK = 8, MENU_COUNT = 9, MENU_MAX_ITEMS = 6 };

struct pad_app {
	char name[96];
	char cmd[768];
	char icon[16];
	char group[24];
	char icon_file[512];
	cairo_surface_t *image;
	int image_tried;
	int is_folder;
};

struct view {
	struct wl_list link;
	struct server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_xwayland_surface *xwayland_surface;
	struct wlr_scene_tree *scene_tree;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener set_title;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener request_configure;
	int placed;
	int is_xwayland;
	int mapped;
	int bound;
	struct wlr_texture *shadow;
	int shadow_w;
	int shadow_h;
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
	int shell_dirty;
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
	struct wlr_scene_buffer *wallpaper;
	struct wlr_scene_buffer *widgets;
	int chrome_inactive;
	struct view *clip_view;
	uint8_t *desk_data;
	int widgets_on;
	int widgets_contrast;
	time_t widgets_stamp;
	uint8_t *paper_data;
	int paper_w;
	int paper_h;
	int paper_stride;
	struct wlr_scene_rect *band;
	struct wlr_compositor *compositor;
	struct wlr_xwayland *xwayland;
	struct wl_listener new_xwayland_surface;
	struct wl_listener xwayland_ready;
	char startup_cmd[512];
	int startup_sent;
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
#ifdef OLYMPIUM_TEST_INPUT
	struct wl_listener new_virtual_pointer;
#endif
	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;
	struct wl_listener selection_changed;
	char *clip_data;
	size_t clip_len;
	char *clip_pending;
	size_t clip_pending_len;
	int clip_fd;
	struct wl_event_source *clip_reader;
	struct wl_event_source *clip_timer;
	int clip_want_read;
	struct wl_event_source *clip_helper_source;
	pid_t clip_helper_pid;
	int clip_helper_fd;
	char x_clip_state;
	int xwayland_up;
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
	int launcher;      /* the search panel is open */
	int search_x;      /* where the search glyph is in the menu bar */
	int quick_x;       /* where the quick-settings glyph is */
	int spot_cursor;   /* which result is selected */
	int blur_ok;
	int blur_fail;
	struct blur_slot *blur_cache;
	size_t blur_cache_bytes;
	uint64_t blur_tick;
	unsigned long blur_hits;
	unsigned long blur_misses;
	int blur_cache_off;
	int blur_cache_verify;
	unsigned frames;
	char blur_error[160];
	char title[96];
	char arch[32];
	char filter[64];
	char notice[160];
	int press_app;
	int press_contrast;
	int press_brand;
	int brand_menu_was_open;
	int hover_app;
	int press_light;
	struct view *press_view;
	char frame_title[128];
	struct wl_listener new_decoration;
	int open_menu;
	int menu_x[8];
	int menu_w[8];
	int demo_menu;
	int demo_menu_done;
	int launchpad;
	int pad_page;
	int pad_count;
	int pad_folder;
	int pad_cursor;
	int net_kind;
	int net_level;
	int demo_pad;
	int demo_pad_done;
	int nag;
	int nag_force;
	int nag_closed;
	int nag_demo_dismiss;
	unsigned dock_running;
	char pad_filter[64];
	struct pad_app pad[PAD_MAX];
	struct pinned_app {
		char name[48];
		char path[192];
	} pinned[4];
	int pinned_count;
	char shot_path[256];
	int shot_written;
	int control_fd;
	struct wl_event_source *shot_timer;
	struct wl_event_source *tick_timer;
	time_t shell_stamp;
};

/* Shell chrome is drawn outside the scene graph. Damage it explicitly when
 * input changes it; client content continues to use wlroots scene damage. */
static void shell_redraw(struct server *server) {
	struct output *output;
	wl_list_for_each(output, &server->outputs, link) {
		output->shell_dirty = 1;
		wlr_output_schedule_frame(output->wlr_output);
	}
}

/* Launchpad grid: five columns by three rows of large icons per page. */
enum { PAD_COLS = 5, PAD_ROWS = 3, PAD_PER = 15, PAD_CELL_W = 176, PAD_CELL_H = 150, PAD_TOP = 128 };

static const int PANEL_H = 25;
static const int TITLE_H = 34;
static const int DOCK_H = 78;
static const int DOCK_MARGIN = 6;
static const int DOCK_SLOT = 68;
static const char *UI_FONT = "Ubuntu 12";
static const char *UI_FONT_SM = "Ubuntu 11";

static int menu_item_count(int menu);
static void menu_box(const struct server *server, struct wlr_box *box);
static const char *MENU_ITEMS[][6];
static void draw_net(cairo_t *cr, double x, double y, int kind, int level);
static void draw_pad(struct server *server, cairo_t *cr);
static void probe_net(struct server *server);
static void pad_load(struct server *server);
static int pad_list(struct server *server, int *ids, int cap);
static void pad_activate(struct server *server, int index);
static void open_launchpad(struct server *server);
static void nag_box(const struct server *server, struct wlr_box *box);
static void nag_dismiss(struct server *server);
static void nag_eval(struct server *server);
static int nag_close_hit(const struct server *server, double x, double y);

static void focus_view(struct view *view, struct wlr_surface *surface);
static void widgets_refresh(struct server *server, int force);
static void draw_search(struct server *server, cairo_t *cr, const struct wlr_box *box);
static int spot_matches(struct server *server, int *ids, int max);
static void open_search(struct server *server);
static void close_search(struct server *server);
static void spot_activate(struct server *server);
static cairo_surface_t *pad_icon_image(struct pad_app *app);
static void widgets_store(int on);

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
	/* OLYMPIUM_STATUS names another file, for a test that must not share this
	 * one with a compositor that is still shutting down. */
	const char *path = getenv("OLYMPIUM_STATUS");
	if (path == NULL || path[0] != '/') {
		path = "/run/dev-desktop/status";
		mkdir("/run/dev-desktop", 0755);
	}
	FILE *f = fopen(path, "w");
	if (f == NULL) {
		return;
	}
	fprintf(f,
		"title=%s\narch=%s\ncontrast=%d\nblur=%s\nframes=%u\nblur_error=%s\nnotice=%s\n"
		"glass_cache_hits=%lu\nglass_cache_misses=%lu\n",
		server->title, server->arch, server->contrast,
		server->contrast ? "off-contrast" : (server->blur_ok ? "live" : "unavailable"),
		server->frames, server->blur_error, server->notice,
		server->blur_hits, server->blur_misses);
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
	server->launchpad = 0;
	server->launcher = 0;
	server->filter[0] = '\0';
}

static int dock_n(const struct server *server) {
	return APP_COUNT + server->pinned_count;
}

static void load_pins(struct server *server) {
	const char *desk = getenv("OLYMPIUM_DESK_HOME");
	char path[512];
	if (desk != NULL && desk[0] != '\0') {
		snprintf(path, sizeof path, "%s/pinned", desk);
	} else {
		const char *home = getenv("HOME");
		if (home == NULL || home[0] == '\0') {
			server->pinned_count = 0;
			return;
		}
		snprintf(path, sizeof path, "%s/.local/share/olympium-desk/pinned", home);
	}
	FILE *f = fopen(path, "r");
	server->pinned_count = 0;
	if (f == NULL) {
		return;
	}
	char line[256];
	while (server->pinned_count < 4 && fgets(line, sizeof line, f) != NULL) {
		line[strcspn(line, "\r\n")] = '\0';
		char *tab = strchr(line, '\t');
		if (tab == NULL || tab == line || tab[1] == '\0') {
			continue;
		}
		*tab = '\0';
		struct pinned_app *pin = &server->pinned[server->pinned_count];
		snprintf(pin->name, sizeof pin->name, "%s", line);
		snprintf(pin->path, sizeof pin->path, "%s", tab + 1);
		if (access(pin->path, X_OK) == 0) {
			server->pinned_count++;
		}
	}
	fclose(f);
}

static void dock_box(const struct server *server, struct wlr_box *box) {
	load_pins((struct server *)server);
	int width = 16 + dock_n(server) * DOCK_SLOT;
	if (width > server->screen_w - 48) {
		width = server->screen_w - 48;
	}
	box->width = width;
	box->height = DOCK_H;
	box->x = (server->screen_w - width) / 2;
	box->y = server->screen_h - DOCK_H - DOCK_MARGIN;
}

enum { SPOT_ROWS = 7, SPOT_FIELD_H = 58, SPOT_ROW_H = 46 };

/* The search panel: a field, and under it one row per result. */
static void launcher_box(const struct server *server, struct wlr_box *box) {
	int ids[SPOT_ROWS];
	int rows = spot_matches((struct server *)server, ids, SPOT_ROWS);
	box->width = 620;
	box->height = SPOT_FIELD_H + (rows > 0 ? rows * SPOT_ROW_H + 12 : 0);
	if (box->width > server->screen_w - 40) {
		box->width = server->screen_w - 40;
	}
	box->x = (server->screen_w - box->width) / 2;
	box->y = server->screen_h / 5;
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
	int index = (int)((x - dock.x - 8) / DOCK_SLOT);
	if (index < 0 || index >= dock_n(server)) {
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

static void rrect(cairo_t *cr, double x, double y, double w, double h, double r) {
	cairo_new_path(cr);
	cairo_arc(cr, x + r, y + r, r, 3.1416, 4.7124);
	cairo_arc(cr, x + w - r, y + r, r, 4.7124, 6.2832);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, 1.5708);
	cairo_arc(cr, x + r, y + h - r, r, 1.5708, 3.1416);
	cairo_close_path(cr);
}

/* The Olympium mark. The drawing is in dragon.h, shared with the boot splash. */
static void draw_dragon(cairo_t *cr, double x, double y, double size, double r, double g, double b) {
	draw_dragon_mark(cr, x, y, size, r, g, b);
}

static void net_wedge(cairo_t *cr, double radius) {
	cairo_new_path(cr);
	cairo_move_to(cr, 11, 19.5);
	cairo_arc(cr, 11, 19.5, radius, 3.927, 5.498);
	cairo_close_path(cr);
}

/* Original status glyph. A fan wedge that fills outward from its apex with
 * the live wireless signal. A wired link is a full wedge over three dots.
 * No link is a faint wedge with a slash. */
static void draw_net(cairo_t *cr, double x, double y, int kind, int level) {
	cairo_save(cr);
	cairo_translate(cr, x, y);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_source_rgba(cr, 0.10, 0.10, 0.12, 0.26);
	net_wedge(cr, 15);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 0.10, 0.10, 0.12, 1);
	if (kind == 1) {
		net_wedge(cr, 12);
		cairo_fill(cr);
		for (int i = 0; i < 3; i++) {
			cairo_arc(cr, 6.5 + i * 4.5, 22, 1.2, 0, 6.2832);
			cairo_fill(cr);
		}
	} else if (kind == 2) {
		int bars = level < 1 ? 1 : (level > 3 ? 3 : level);
		net_wedge(cr, 5.0 * bars);
		cairo_fill(cr);
	} else {
		cairo_set_source_rgba(cr, 0.10, 0.10, 0.12, 1);
		cairo_set_line_width(cr, 1.7);
		cairo_move_to(cr, 3, 5);
		cairo_line_to(cr, 19, 20);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

static void probe_net(struct server *server) {
	server->net_kind = 0;
	server->net_level = 0;
	DIR *dir = opendir("/sys/class/net");
	if (dir == NULL) {
		return;
	}
	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.' || strcmp(ent->d_name, "lo") == 0) {
			continue;
		}
		char path[320];
		snprintf(path, sizeof path, "/sys/class/net/%s/operstate", ent->d_name);
		FILE *f = fopen(path, "r");
		char state[32] = "";
		if (f != NULL) {
			if (fgets(state, sizeof state, f) == NULL) {
				state[0] = '\0';
			}
			fclose(f);
		}
		if (strncmp(state, "up", 2) != 0) {
			continue;
		}
		snprintf(path, sizeof path, "/sys/class/net/%s/wireless", ent->d_name);
		DIR *wifi = opendir(path);
		if (wifi != NULL) {
			closedir(wifi);
			server->net_kind = 2;
			server->net_level = 2;
			FILE *w = fopen("/proc/net/wireless", "r");
			if (w != NULL) {
				char line[256];
				while (fgets(line, sizeof line, w) != NULL) {
					if (strncmp(line, ent->d_name, strlen(ent->d_name)) == 0) {
						int qual = 0;
						if (sscanf(line, "%*s %*d %d", &qual) == 1) {
							int level = qual / 20;
							if (level < 1) {
								level = 1;
							}
							if (level > 3) {
								level = 3;
							}
							server->net_level = level;
						}
					}
				}
				fclose(w);
			}
			break;
		}
		server->net_kind = 1;
		server->net_level = 4;
		break;
	}
	closedir(dir);
}

static void icon_squircle(cairo_t *cr) {
	cairo_new_path(cr);
	cairo_arc(cr, 10, 10, 10, 3.1416, 4.7124);
	cairo_arc(cr, 38, 10, 10, 4.7124, 6.2832);
	cairo_arc(cr, 38, 38, 10, 0, 1.5708);
	cairo_arc(cr, 10, 38, 10, 1.5708, 3.1416);
	cairo_close_path(cr);
}

struct icon_style {
	const char *id;
	double top[3];
	double bottom[3];
};

/* Background gradient per icon. The glyph on top is drawn in draw_icon. */
static const struct icon_style ICON_STYLES[] = {
	{"files", {0.42, 0.76, 1.00}, {0.16, 0.50, 0.94}},
	{"firefox", {0.27, 0.62, 0.98}, {0.10, 0.34, 0.78}},
	{"terminal", {0.26, 0.28, 0.33}, {0.07, 0.08, 0.10}},
	{"shell", {0.20, 0.20, 0.24}, {0.03, 0.03, 0.05}},
	{"settings", {0.78, 0.80, 0.84}, {0.50, 0.53, 0.59}},
	{"olympium", {0.20, 0.36, 0.62}, {0.07, 0.14, 0.30}},
	{"launchpad", {1.00, 1.00, 1.00}, {0.86, 0.88, 0.92}},
	{"compat", {0.66, 0.50, 0.98}, {0.38, 0.26, 0.84}},
	{"testapp", {0.20, 0.55, 0.95}, {0.10, 0.32, 0.72}},
	{"activity", {0.22, 0.25, 0.30}, {0.06, 0.07, 0.09}},
	{"disks", {0.93, 0.94, 0.96}, {0.70, 0.73, 0.78}},
	{"network", {0.36, 0.78, 1.00}, {0.10, 0.48, 0.94}},
	{"devices", {0.46, 0.50, 0.58}, {0.24, 0.27, 0.33}},
	{"report", {1.00, 0.84, 0.36}, {0.97, 0.60, 0.14}},
	{"color", {1.00, 1.00, 1.00}, {0.90, 0.91, 0.94}},
	{"code", {0.34, 0.56, 0.92}, {0.16, 0.30, 0.66}},
	{"about", {1.00, 0.98, 0.92}, {0.93, 0.86, 0.70}},
	{"usb", {0.62, 0.56, 0.98}, {0.36, 0.30, 0.80}},
	{"log", {0.52, 0.56, 0.64}, {0.30, 0.33, 0.40}},
	{"services", {0.30, 0.86, 0.76}, {0.10, 0.60, 0.56}},
	{"install", {0.42, 0.90, 0.52}, {0.14, 0.66, 0.30}},
	{"generic", {0.48, 0.74, 1.00}, {0.22, 0.48, 0.90}},
};

static void icon_lines(cairo_t *cr, double x, double y, double w, int count, double gap, double thick) {
	for (int i = 0; i < count; i++) {
		rrect(cr, x, y + i * gap, w, thick, thick / 2.0);
		cairo_fill(cr);
	}
}

/* One finished 48-pixel application icon: rounded tile, vertical gradient,
 * glyph, and a soft highlight across the upper half. All shapes are original. */
static void draw_icon(cairo_t *cr, const char *id, double x, double y) {
	const struct icon_style *style = &ICON_STYLES[sizeof ICON_STYLES / sizeof ICON_STYLES[0] - 1];
	for (size_t i = 0; i < sizeof ICON_STYLES / sizeof ICON_STYLES[0]; i++) {
		if (strcmp(ICON_STYLES[i].id, id) == 0) {
			style = &ICON_STYLES[i];
			break;
		}
	}
	cairo_save(cr);
	cairo_translate(cr, x, y);
	cairo_save(cr);
	cairo_translate(cr, 0, 1.6);
	icon_squircle(cr);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.22);
	cairo_fill(cr);
	cairo_restore(cr);
	icon_squircle(cr);
	cairo_pattern_t *bg = cairo_pattern_create_linear(0, 0, 0, 48);
	cairo_pattern_add_color_stop_rgb(bg, 0, style->top[0], style->top[1], style->top[2]);
	cairo_pattern_add_color_stop_rgb(bg, 1, style->bottom[0], style->bottom[1], style->bottom[2]);
	cairo_set_source(cr, bg);
	cairo_fill(cr);
	cairo_pattern_destroy(bg);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	const char *kind = style->id;
	if (strcmp(kind, "files") == 0) {
		cairo_set_source_rgba(cr, 1, 1, 1, 0.78);
		rrect(cr, 9, 13, 16, 9, 3);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 1, 1, 1, 1);
		rrect(cr, 9, 17, 30, 20, 4);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0.20, 0.52, 0.94, 0.55);
		rrect(cr, 13, 30, 12, 2.6, 1.3);
		cairo_fill(cr);
	} else if (strcmp(kind, "firefox") == 0) {
		/* An original globe. It is not the Firefox mark. */
		cairo_set_source_rgba(cr, 1, 1, 1, 0.96);
		cairo_arc(cr, 24, 24, 13, 0, 6.2832);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0.12, 0.38, 0.78, 1);
		cairo_set_line_width(cr, 1.6);
		cairo_arc(cr, 24, 24, 9, 0, 6.2832);
		cairo_stroke(cr);
		cairo_move_to(cr, 15, 24);
		cairo_curve_to(cr, 20, 14, 28, 14, 33, 24);
		cairo_curve_to(cr, 28, 34, 20, 34, 15, 24);
		cairo_stroke(cr);
		cairo_move_to(cr, 24, 12);
		cairo_line_to(cr, 24, 36);
		cairo_stroke(cr);
	} else if (strcmp(kind, "terminal") == 0 || strcmp(kind, "shell") == 0) {
		if (strcmp(kind, "shell") == 0) {
			cairo_set_source_rgba(cr, 1.00, 0.76, 0.28, 1);
		} else {
			cairo_set_source_rgba(cr, 0.46, 0.96, 0.62, 1);
		}
		cairo_set_line_width(cr, 2.8);
		cairo_move_to(cr, 12, 16);
		cairo_line_to(cr, 21, 24);
		cairo_line_to(cr, 12, 32);
		cairo_stroke(cr);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.92);
		cairo_move_to(cr, 25, 32);
		cairo_line_to(cr, 36, 32);
		cairo_stroke(cr);
	} else if (strcmp(kind, "settings") == 0) {
		cairo_set_source_rgba(cr, 0.20, 0.22, 0.26, 1);
		for (int i = 0; i < 8; i++) {
			cairo_save(cr);
			cairo_translate(cr, 24, 24);
			cairo_rotate(cr, i * 0.7854);
			rrect(cr, -3, -16, 6, 8, 1.6);
			cairo_fill(cr);
			cairo_restore(cr);
		}
		cairo_arc(cr, 24, 24, 11.5, 0, 6.2832);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0.72, 0.75, 0.80, 1);
		cairo_arc(cr, 24, 24, 5, 0, 6.2832);
		cairo_fill(cr);
	} else if (strcmp(kind, "olympium") == 0) {
		draw_dragon(cr, 7, 6, 34, 1.00, 0.84, 0.40);
	} else if (strcmp(kind, "about") == 0) {
		draw_dragon(cr, 7, 6, 34, 0.62, 0.40, 0.08);
	} else if (strcmp(kind, "launchpad") == 0) {
		const double dots[][3] = {
			{0.98, 0.34, 0.30}, {1.00, 0.60, 0.16}, {1.00, 0.82, 0.18},
			{0.22, 0.78, 0.42}, {0.16, 0.60, 0.98}, {0.48, 0.38, 0.94},
			{0.96, 0.34, 0.62}, {0.16, 0.74, 0.80}, {0.56, 0.58, 0.64}
		};
		for (int i = 0; i < 9; i++) {
			cairo_set_source_rgba(cr, dots[i][0], dots[i][1], dots[i][2], 1);
			rrect(cr, 9.5 + (i % 3) * 10.5, 9.5 + (i / 3) * 10.5, 8, 8, 2.4);
			cairo_fill(cr);
		}
	} else if (strcmp(kind, "compat") == 0) {
		cairo_set_source_rgba(cr, 1, 1, 1, 0.55);
		rrect(cr, 10, 10, 20, 20, 5);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 1, 1, 1, 1);
		rrect(cr, 18, 18, 20, 20, 5);
		cairo_fill(cr);
	} else if (strcmp(kind, "testapp") == 0) {
		cairo_set_source_rgba(cr, 1, 1, 1, 0.96);
		rrect(cr, 12, 11, 24, 26, 4);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0.15, 0.45, 0.85, 1);
		rrect(cr, 12, 11, 24, 7, 3);
		cairo_fill(cr);
	} else if (strcmp(kind, "activity") == 0) {
		cairo_set_source_rgba(cr, 0.36, 0.96, 0.56, 1);
		cairo_set_line_width(cr, 2.4);
		cairo_move_to(cr, 8, 26);
		cairo_line_to(cr, 16, 26);
		cairo_line_to(cr, 20, 14);
		cairo_line_to(cr, 26, 34);
		cairo_line_to(cr, 30, 22);
		cairo_line_to(cr, 40, 22);
		cairo_stroke(cr);
	} else if (strcmp(kind, "disks") == 0) {
		cairo_set_source_rgba(cr, 0.30, 0.33, 0.38, 1);
		rrect(cr, 9, 16, 30, 17, 4);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0.40, 0.92, 0.56, 1);
		cairo_arc(cr, 33, 28, 2, 0, 6.2832);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.55);
		icon_lines(cr, 13, 21, 14, 2, 5, 2);
	} else if (strcmp(kind, "network") == 0) {
		cairo_set_source_rgba(cr, 1, 1, 1, 0.45);
		cairo_move_to(cr, 24, 37);
		cairo_arc(cr, 24, 37, 26, 3.927, 5.498);
		cairo_close_path(cr);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 1, 1, 1, 1);
		cairo_move_to(cr, 24, 37);
		cairo_arc(cr, 24, 37, 17, 3.927, 5.498);
		cairo_close_path(cr);
		cairo_fill(cr);
	} else if (strcmp(kind, "devices") == 0) {
		cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
		for (int i = 0; i < 3; i++) {
			rrect(cr, 15 + i * 7, 8, 3, 32, 1.5);
			cairo_fill(cr);
			rrect(cr, 8, 15 + i * 7, 32, 3, 1.5);
			cairo_fill(cr);
		}
		cairo_set_source_rgba(cr, 0.16, 0.18, 0.22, 1);
		rrect(cr, 13, 13, 22, 22, 4);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0.98, 0.78, 0.30, 1);
		rrect(cr, 19, 19, 10, 10, 2);
		cairo_fill(cr);
	} else if (strcmp(kind, "report") == 0) {
		cairo_set_source_rgba(cr, 1, 1, 1, 1);
		rrect(cr, 13, 9, 22, 30, 4);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0.92, 0.56, 0.12, 0.9);
		icon_lines(cr, 17, 16, 14, 3, 6, 2.2);
	} else if (strcmp(kind, "color") == 0) {
		const double quads[][3] = {
			{0.98, 0.36, 0.34}, {1.00, 0.76, 0.20}, {0.24, 0.76, 0.44}, {0.20, 0.56, 0.98}
		};
		for (int i = 0; i < 4; i++) {
			cairo_set_source_rgba(cr, quads[i][0], quads[i][1], quads[i][2], 1);
			rrect(cr, 10 + (i % 2) * 15, 10 + (i / 2) * 15, 13, 13, 3.5);
			cairo_fill(cr);
		}
	} else if (strcmp(kind, "code") == 0) {
		cairo_set_source_rgba(cr, 1.00, 0.86, 0.34, 1);
		cairo_set_line_width(cr, 2.6);
		cairo_move_to(cr, 17, 16);
		cairo_line_to(cr, 9, 24);
		cairo_line_to(cr, 17, 32);
		cairo_move_to(cr, 31, 16);
		cairo_line_to(cr, 39, 24);
		cairo_line_to(cr, 31, 32);
		cairo_stroke(cr);
		cairo_set_source_rgba(cr, 1, 1, 1, 1);
		cairo_move_to(cr, 27, 13);
		cairo_line_to(cr, 21, 35);
		cairo_stroke(cr);
	} else if (strcmp(kind, "usb") == 0) {
		cairo_set_source_rgba(cr, 1, 1, 1, 1);
		cairo_set_line_width(cr, 2.4);
		cairo_move_to(cr, 24, 38);
		cairo_line_to(cr, 24, 11);
		cairo_move_to(cr, 24, 30);
		cairo_line_to(cr, 15, 23);
		cairo_line_to(cr, 15, 18);
		cairo_move_to(cr, 24, 26);
		cairo_line_to(cr, 33, 20);
		cairo_line_to(cr, 33, 16);
		cairo_stroke(cr);
		cairo_arc(cr, 24, 38, 3, 0, 6.2832);
		cairo_fill(cr);
		cairo_arc(cr, 15, 17, 2.6, 0, 6.2832);
		cairo_fill(cr);
		rrect(cr, 30.5, 11.5, 5, 5, 1);
		cairo_fill(cr);
	} else if (strcmp(kind, "log") == 0) {
		cairo_set_source_rgba(cr, 1, 1, 1, 1);
		icon_lines(cr, 17, 14, 21, 4, 6, 2.4);
		for (int i = 0; i < 4; i++) {
			cairo_arc(cr, 11.5, 15.2 + i * 6, 1.7, 0, 6.2832);
			cairo_fill(cr);
		}
	} else if (strcmp(kind, "services") == 0) {
		cairo_set_source_rgba(cr, 1, 1, 1, 1);
		for (int i = 0; i < 3; i++) {
			rrect(cr, 10, 12 + i * 9, 28, 6, 3);
			cairo_fill(cr);
		}
		cairo_set_source_rgba(cr, 0.10, 0.56, 0.52, 1);
		cairo_arc(cr, 31, 15, 2, 0, 6.2832);
		cairo_arc(cr, 17, 24, 2, 0, 6.2832);
		cairo_arc(cr, 27, 33, 2, 0, 6.2832);
		cairo_fill(cr);
	} else if (strcmp(kind, "install") == 0) {
		cairo_set_source_rgba(cr, 1, 1, 1, 1);
		cairo_set_line_width(cr, 3);
		cairo_move_to(cr, 24, 10);
		cairo_line_to(cr, 24, 27);
		cairo_move_to(cr, 16, 20);
		cairo_line_to(cr, 24, 28);
		cairo_line_to(cr, 32, 20);
		cairo_stroke(cr);
		rrect(cr, 11, 33, 26, 4, 2);
		cairo_fill(cr);
	} else {
		cairo_set_source_rgba(cr, 1, 1, 1, 1);
		rrect(cr, 12, 12, 24, 24, 6);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, style->bottom[0], style->bottom[1], style->bottom[2], 1);
		cairo_arc(cr, 24, 24, 5, 0, 6.2832);
		cairo_fill(cr);
	}
	/* Upper highlight, clipped to the tile. */
	icon_squircle(cr);
	cairo_clip(cr);
	cairo_pattern_t *gloss = cairo_pattern_create_linear(0, 0, 0, 48);
	cairo_pattern_add_color_stop_rgba(gloss, 0.0, 1, 1, 1, 0.34);
	cairo_pattern_add_color_stop_rgba(gloss, 0.5, 1, 1, 1, 0.04);
	cairo_pattern_add_color_stop_rgba(gloss, 1.0, 0, 0, 0, 0.06);
	cairo_set_source(cr, gloss);
	cairo_paint(cr);
	cairo_pattern_destroy(gloss);
	cairo_restore(cr);
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
		int contrast, double radius, double veil) {
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
		if (veil > 0) {
			cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, veil);
			cairo_paint(cr);
			/* Specular sheen: bright along the top edge, fading by mid-height. */
			cairo_pattern_t *spec = cairo_pattern_create_linear(0, 0, 0, height);
			cairo_pattern_add_color_stop_rgba(spec, 0.0, 1, 1, 1, 0.72);
			cairo_pattern_add_color_stop_rgba(spec, 0.22, 1, 1, 1, 0.16);
			cairo_pattern_add_color_stop_rgba(spec, 0.55, 1, 1, 1, 0.0);
			cairo_pattern_add_color_stop_rgba(spec, 1.0, 0.75, 0.82, 0.90, 0.10);
			cairo_set_source(cr, spec);
			cairo_paint(cr);
			cairo_pattern_destroy(spec);
		}
		if (radius != 0) {
			cairo_set_source_rgba(cr, 1, 1, 1, 0.85);
			cairo_set_line_width(cr, 1.4);
			double r = radius < 0 ? -radius : radius;
			if (r > 2) {
				cairo_new_path(cr);
				cairo_arc(cr, r, r, r - 0.8, 3.1416, 1.5 * 3.1416);
				cairo_arc(cr, width - r, r, r - 0.8, 1.5 * 3.1416, 6.2832);
				if (radius < 0) {
					cairo_line_to(cr, width - 0.8, height - 0.8);
					cairo_line_to(cr, 0.8, height - 0.8);
				} else {
					cairo_arc(cr, width - r, height - r, r - 0.8, 0, 0.5 * 3.1416);
					cairo_arc(cr, r, height - r, r - 0.8, 0.5 * 3.1416, 3.1416);
				}
				cairo_close_path(cr);
				cairo_stroke(cr);
			}
		}
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
		draw_dragon(cr, 6, 3, 22, 0.72, 0.48, 0.12);
		const char *app = server->frame_title[0] != '\0' ? server->frame_title : server->title;
		server->menu_x[0] = 36;
		server->menu_w[0] = paint_text(cr, "Ubuntu Bold 13", app, 36, 4, 0.10, 0.10, 0.12);
		const char *menus[] = {"File", "Edit", "View", "Go", "Window", "Help"};
		double mx = server->menu_x[0] + server->menu_w[0] + 18;
		for (int i = 0; i < 6; i++) {
			server->menu_x[i + 1] = (int)mx;
			server->menu_w[i + 1] = paint_text(cr, "Ubuntu 13", menus[i], mx, 4, 0.12, 0.12, 0.14);
			mx += server->menu_w[i + 1] + 18;
		}
		time_t now = time(NULL);
		struct tm tm;
		localtime_r(&now, &tm);
		char clock[48];
		strftime(clock, sizeof clock, "%a %b %-d  %-I:%M %p", &tm);
		int clock_w = paint_text(cr, "Ubuntu Medium 13", clock, 0, -60, 0, 0, 0);
		double rx = server->screen_w - 14 - clock_w;
		paint_text(cr, "Ubuntu Medium 13", clock, rx, 4, 0.10, 0.10, 0.12);
		/* Quick-settings glyph: two sliders. A click opens its menu. */
		rx -= 30;
		server->quick_x = (int)rx;
		cairo_set_source_rgba(cr, 0.10, 0.10, 0.12, 1);
		cairo_set_line_width(cr, 1.5);
		rrect(cr, rx, 6, 16, 5.5, 2.7);
		cairo_stroke(cr);
		rrect(cr, rx, 14, 16, 5.5, 2.7);
		cairo_stroke(cr);
		cairo_arc(cr, rx + 4, 8.75, 2.2, 0, 6.2832);
		cairo_fill(cr);
		cairo_arc(cr, rx + 12, 16.75, 2.2, 0, 6.2832);
		cairo_fill(cr);
		/* Search glyph. A click on it opens the search panel. */
		rx -= 28;
		server->search_x = (int)rx;
		cairo_set_line_width(cr, 1.7);
		cairo_arc(cr, rx + 6.5, 11.5, 5, 0, 6.2832);
		cairo_stroke(cr);
		cairo_move_to(cr, rx + 10.4, 15.4);
		cairo_line_to(cr, rx + 14.5, 19.5);
		cairo_stroke(cr);
		rx -= 34;
		draw_net(cr, rx, 2, server->net_kind, server->net_level);
		return;
	}
	if (which == 6) {
		draw_pad(server, cr);
		return;
	}
	if (which == 7) {
		cairo_set_source_rgba(cr, 0.16, 0.16, 0.18, 1);
		cairo_arc(cr, box->width - 22, 20, 9, 0, 6.2832);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0.98, 0.98, 0.98, 1);
		cairo_set_line_width(cr, 1.6);
		cairo_move_to(cr, box->width - 26, 16);
		cairo_line_to(cr, box->width - 18, 24);
		cairo_move_to(cr, box->width - 18, 16);
		cairo_line_to(cr, box->width - 26, 24);
		cairo_stroke(cr);
		const double lights[][3] = {
			{1.0, 0.37, 0.34}, {0.996, 0.74, 0.18}, {0.16, 0.78, 0.25}
		};
		for (int i = 0; i < 3; i++) {
			cairo_set_source_rgba(cr, lights[i][0], lights[i][1], lights[i][2], 1);
			cairo_arc(cr, 20 + i * 16, 22, 5.5, 0, 6.2832);
			cairo_fill(cr);
		}
		int nag_w = paint_text(cr, "Ubuntu Bold 13", "Olympium", 0, -60, 0, 0, 0);
		paint_text(cr, "Ubuntu Bold 13", "Olympium", (box->width - nag_w) / 2.0, 13, 0.14, 0.14, 0.16);
		paint_text(cr, UI_FONT, "This is a commercial system.", 22, 58, 0.16, 0.16, 0.18);
		paint_text(cr, UI_FONT, "Please purchase a license.", 22, 80, 0.16, 0.16, 0.18);
		paint_text(cr, UI_FONT, "Close this reminder to keep using Olympium.", 22, 108, 0.28, 0.28, 0.32);
		paint_text(cr, UI_FONT_SM, "It returns every 5 days until a license is activated.", 22, 132, 0.28, 0.28, 0.32);
		paint_text(cr, UI_FONT_SM, "Same reminder for home, school, business, and bulk installs.", 22, 154, 0.28, 0.28, 0.32);
		cairo_set_source_rgba(cr, 0.16, 0.47, 0.96, 1);
		rrect(cr, box->width - 108, box->height - 52, 84, 28, 14);
		cairo_fill(cr);
		paint_text(cr, "Ubuntu Medium 13", "Close", box->width - 85, box->height - 47, 1, 1, 1);
		return;
	}
	if (which == 1) {
		load_pins(server);
		for (int i = 0; i < dock_n(server); i++) {
			double ix = 8 + i * DOCK_SLOT + (DOCK_SLOT - 48) / 2.0;
			double iy = 14;
			double scale = (server->hover_app == i) ? 1.18 : 1.0;
			cairo_save(cr);
			cairo_translate(cr, ix + 24, iy + 24);
			cairo_scale(cr, scale, scale);
			cairo_translate(cr, -24, -24);
			draw_icon(cr, i < APP_COUNT ? APPS[i].id : "installed", 0, 0);
			cairo_restore(cr);
			if (i >= APP_COUNT && server->pinned[i - APP_COUNT].name[0] != '\0') {
				char letter[4];
				snprintf(letter, sizeof letter, "%.1s", server->pinned[i - APP_COUNT].name);
				paint_text(cr, "Ubuntu Bold 14", letter, ix + 18, iy + 14, 0.12, 0.14, 0.18);
			}
			if (server->dock_running & (1u << i)) {
				cairo_set_source_rgba(cr, 0.20, 0.22, 0.26, 0.85);
				cairo_arc(cr, ix + 24, 70, 2.4, 0, 6.2832);
				cairo_fill(cr);
			}
		}
		return;
	}
	if (which == 5 && server->open_menu >= 0) {
		int n = menu_item_count(server->open_menu);
		int hover = server->demo_menu ? 0 : -1;
		if (point_in(box, server->cursor->x, server->cursor->y)) {
			hover = (int)((server->cursor->y - box->y - 6) / 28);
		}
		int quick = server->open_menu == MENU_QUICK;
		for (int i = 0; i < n; i++) {
			/* In quick settings the third line only reports the network. */
			int info = quick && i == 2;
			int lit = i == hover && !info;
			if (lit) {
				cairo_set_source_rgba(cr, 0.16, 0.47, 0.96, 0.94);
				rrect(cr, 5, 5 + i * 28, box->width - 10, 24, 6);
				cairo_fill(cr);
			}
			if (quick) {
				const char *label = MENU_ITEMS[server->open_menu][i];
				char network[64];
				int on = (i == 0 && server->contrast) || (i == 1 && server->widgets_on);
				if (info) {
					if (server->net_kind == 2) {
						snprintf(network, sizeof network, "Wi-Fi, signal %d of 3", server->net_level);
					} else {
						snprintf(network, sizeof network, "%s",
							server->net_kind == 1 ? "Wired network connected" : "No network");
					}
					label = network;
				}
				if (on) {
					/* A tick for a setting that is on. */
					cairo_set_source_rgba(cr, lit ? 1 : 0.16, lit ? 1 : 0.47, lit ? 1 : 0.96, 1);
					cairo_set_line_width(cr, 2);
					cairo_move_to(cr, 15, 17 + i * 28);
					cairo_line_to(cr, 19.5, 21.5 + i * 28);
					cairo_line_to(cr, 27, 11.5 + i * 28);
					cairo_stroke(cr);
				}
				double ink = info ? 0.46 : (lit ? 1 : 0.10);
				paint_text(cr, "Ubuntu 13", label, 36, 7 + i * 28, ink, ink, info ? 0.50 : (lit ? 1 : 0.12));
			} else {
				paint_text(cr, "Ubuntu 13", MENU_ITEMS[server->open_menu][i], 16, 7 + i * 28,
					lit ? 1 : 0.10, lit ? 1 : 0.10, lit ? 1 : 0.12);
			}
			if (i == n - 2 && n > 2) {
				cairo_set_source_rgba(cr, 0.45, 0.45, 0.48, 0.35);
				cairo_rectangle(cr, 14, 8 + (i + 1) * 28 - 3, box->width - 28, 1);
				cairo_fill(cr);
			}
		}
		return;
	}
	if (which == 4) {
		const char *label = server->frame_title[0] != '\0' ? server->frame_title : "";
		paint_text(cr, "Ubuntu Medium 12", label, 11, 4, 0.12, 0.12, 0.14);
		return;
	}
	if (which == 3) {
		const double lights[][3] = {
			{1.0, 0.37, 0.34},
			{0.996, 0.74, 0.18},
			{0.16, 0.78, 0.25}
		};
		for (int i = 0; i < 3; i++) {
			if (server->chrome_inactive) {
				/* A window that is not in front has grey controls and a
				 * paler title, so the front window is the one that reads. */
				cairo_set_source_rgba(cr, 0.80, 0.80, 0.83, 1);
			} else {
				cairo_set_source_rgba(cr, lights[i][0], lights[i][1], lights[i][2], 1);
			}
			cairo_arc(cr, 16 + i * 18, box->height / 2.0, 6.0, 0, 6.2832);
			cairo_fill(cr);
		}
		const char *title = server->frame_title[0] != '\0' ? server->frame_title : "Window";
		int title_w = paint_text(cr, "Ubuntu Bold 13", title, 0, -60, 0, 0, 0);
		double ink = server->chrome_inactive ? 0.52 : 0.14;
		paint_text(cr, "Ubuntu Bold 13", title, (box->width - title_w) / 2.0, 8,
			ink, ink, ink + 0.02);
		return;
	}
	draw_search(server, cr, box);
}

/*
 * The glass behind the menu bar, the dock and each title bar is a blur of
 * what the renderer drew there, laid under a white veil and cut to a rounded
 * shape. That is most of the cost of a frame, and when a window animates
 * somewhere else the pixels behind the bar are the same as in the last frame.
 * So the finished glass is remembered, keyed by the pixels it was made from
 * and the shape and veil it was made with. It depends on nothing else: no
 * window title, no hover, no clock, which are drawn over it afterwards. A
 * remembered glass is used only when the pixels are the same byte for byte.
 * OLYMPIUM_BLUR_CACHE=0 turns it off, and OLYMPIUM_BLUR_CACHE=verify makes
 * the glass again on every use and aborts when the two differ, which is how
 * the test shows it never serves a wrong picture.
 */
struct blur_slot {
	struct blur_slot *next;
	int w, h, mode;
	double radius, veil;
	size_t bytes;
	uint64_t used;
	uint8_t *src;
	uint8_t *glass;
};

enum { BLUR_CACHE_BUDGET = 40 * 1024 * 1024 };

static void blur_apply(uint8_t *pixels, int w, int h, int stride, int mode) {
	if (mode == 0) {
		dev_soft_bgra(pixels, w, h, stride);
	} else {
		dev_glass_bgra(pixels, w, h, stride, mode);
	}
}

static void blur_cache_drop_oldest(struct server *server) {
	struct blur_slot **oldest = NULL;
	for (struct blur_slot **at = &server->blur_cache; *at != NULL; at = &(*at)->next) {
		if (oldest == NULL || (*at)->used < (*oldest)->used) {
			oldest = at;
		}
	}
	if (oldest == NULL) {
		return;
	}
	struct blur_slot *gone = *oldest;
	*oldest = gone->next;
	server->blur_cache_bytes -= gone->bytes * 2;
	free(gone->src);
	free(gone->glass);
	free(gone);
}

/* Copies rows between surfaces whose strides may differ. */
static void copy_rows(uint8_t *dst, int dst_stride, const uint8_t *src, int src_stride, int w, int h) {
	if (dst_stride == src_stride) {
		memcpy(dst, src, (size_t)dst_stride * (size_t)h);
		return;
	}
	for (int y = 0; y < h; y++) {
		memcpy(dst + (size_t)y * dst_stride, src + (size_t)y * src_stride, (size_t)w * 4);
	}
}

static cairo_surface_t *glass_make(uint8_t *pixels, int w, int h, int stride, int mode,
		double radius, double veil) {
	blur_apply(pixels, w, h, stride, mode);
	return frosted_surface(pixels, w, h, stride, 0, radius, veil);
}

/* The finished glass for the pixels in `pixels` (w * 4 bytes a row), which
 * are changed. mode 0 is the heavy backdrop blur, otherwise the glass radius. */
static cairo_surface_t *glass_cached(struct server *server, uint8_t *pixels, int w, int h,
		int stride, int mode, double radius, double veil) {
	size_t bytes = (size_t)stride * (size_t)h;
	if (server->blur_cache_off || bytes * 2 > BLUR_CACHE_BUDGET) {
		return glass_make(pixels, w, h, stride, mode, radius, veil);
	}
	for (struct blur_slot *slot = server->blur_cache; slot != NULL; slot = slot->next) {
		if (slot->w != w || slot->h != h || slot->mode != mode || slot->bytes != bytes ||
				slot->radius != radius || slot->veil != veil ||
				memcmp(slot->src, pixels, bytes) != 0) {
			continue;
		}
		cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
		if (cairo_surface_status(out) != CAIRO_STATUS_SUCCESS) {
			cairo_surface_destroy(out);
			break;
		}
		slot->used = ++server->blur_tick;
		server->blur_hits++;
		cairo_surface_flush(out);
		copy_rows(cairo_image_surface_get_data(out), cairo_image_surface_get_stride(out),
			slot->glass, stride, w, h);
		cairo_surface_mark_dirty(out);
		if (server->blur_cache_verify) {
			cairo_surface_t *again = glass_make(pixels, w, h, stride, mode, radius, veil);
			cairo_surface_flush(again);
			const uint8_t *a = cairo_image_surface_get_data(again);
			const uint8_t *b = cairo_image_surface_get_data(out);
			int same = cairo_image_surface_get_stride(again) == cairo_image_surface_get_stride(out);
			for (int y = 0; same && y < h; y++) {
				same = memcmp(a + (size_t)y * cairo_image_surface_get_stride(again),
					b + (size_t)y * cairo_image_surface_get_stride(out), (size_t)w * 4) == 0;
			}
			if (!same) {
				wlr_log(WLR_ERROR, "glass cache mismatch: %dx%d mode %d", w, h, mode);
				abort();
			}
			cairo_surface_destroy(again);
		}
		return out;
	}
	server->blur_misses++;
	struct blur_slot *slot = calloc(1, sizeof(*slot));
	uint8_t *src = slot != NULL ? malloc(bytes) : NULL;
	uint8_t *glass = src != NULL ? malloc(bytes) : NULL;
	if (glass == NULL) {
		free(src);
		free(slot);
		return glass_make(pixels, w, h, stride, mode, radius, veil);
	}
	memcpy(src, pixels, bytes);
	cairo_surface_t *out = glass_make(pixels, w, h, stride, mode, radius, veil);
	cairo_surface_flush(out);
	copy_rows(glass, stride, cairo_image_surface_get_data(out),
		cairo_image_surface_get_stride(out), w, h);
	slot->w = w;
	slot->h = h;
	slot->mode = mode;
	slot->radius = radius;
	slot->veil = veil;
	slot->bytes = bytes;
	slot->src = src;
	slot->glass = glass;
	slot->used = ++server->blur_tick;
	while (server->blur_cache != NULL && server->blur_cache_bytes + bytes * 2 > BLUR_CACHE_BUDGET) {
		blur_cache_drop_oldest(server);
	}
	slot->next = server->blur_cache;
	server->blur_cache = slot;
	server->blur_cache_bytes += bytes * 2;
	return out;
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
	/* How much white sits over the bent picture. Menus and dialogs carry text
	 * on top of windows, so they are milkier than the menu bar and the dock. */
	double veil = 0.18;
	if (which == 1) {
		veil = 0.22;
	} else if (which == 3) {
		veil = 0.42;
	} else if (which == 5 || which == 7 || which == 2 || which == 4) {
		veil = 0.56;
	} else if (which == 6) {
		veil = 0.0;
	}
	cairo_surface_t *base;
	if (!server->contrast && sampled) {
		base = glass_cached(server, pixels, w, h, stride,
			which == 6 ? 0 : ((which == 5 || which == 7) ? 6 : 3), radius, veil);
	} else {
		base = frosted_surface(pixels, w, h, stride, 1, radius, veil);
	}
	cairo_t *cr = cairo_create(base);
	struct wlr_box box = {.x = x, .y = y, .width = w, .height = h};
	draw_chrome_on(server, cr, base, &box, which);
	cairo_destroy(cr);
	struct wlr_texture *texture = texture_from_cairo(server->renderer, base);
	cairo_surface_destroy(base);
	free(pixels);
	return texture;
}

static int view_title_box(struct view *view, struct wlr_box *box);

/* The whole frame of a window: title bar and content. */
static int view_frame_box(struct view *view, struct wlr_box *box) {
	if (!view_title_box(view, box)) {
		return 0;
	}
	int content_h = 0;
	if (view->xdg_toplevel != NULL) {
		struct wlr_box geo;
		wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);
		content_h = geo.height > 0 ? geo.height
			: view->xdg_toplevel->base->surface->current.height;
	} else if (view->is_xwayland && view->xwayland_surface != NULL) {
		content_h = view->xwayland_surface->height;
	}
	box->height += content_h;
	return 1;
}

static void render_texture_at(struct server *server, struct wlr_output *output,
		struct wlr_texture *texture, int x, int y) {
	if (texture == NULL) {
		return;
	}
	if (server->clip_view == NULL) {
		wlr_render_texture(server->renderer, texture, output->transform_matrix, x, y, 1.0f);
		return;
	}
	/* Shadows and title bars are painted after the windows themselves. One
	 * that belongs to a window further back must not land on a window in
	 * front of it, so the frames of the windows above are cut out first. */
	pixman_region32_t visible;
	pixman_region32_init_rect(&visible, x, y, texture->width, texture->height);
	struct view *above;
	wl_list_for_each(above, &server->views, link) {
		if (above == server->clip_view) {
			break;
		}
		struct wlr_box frame;
		if (view_frame_box(above, &frame)) {
			pixman_region32_t cut;
			pixman_region32_init_rect(&cut, frame.x, frame.y, frame.width, frame.height);
			pixman_region32_subtract(&visible, &visible, &cut);
			pixman_region32_fini(&cut);
		}
	}
	int count = 0;
	pixman_box32_t *rects = pixman_region32_rectangles(&visible, &count);
	for (int i = 0; i < count; i++) {
		struct wlr_box part = {
			.x = rects[i].x1, .y = rects[i].y1,
			.width = rects[i].x2 - rects[i].x1, .height = rects[i].y2 - rects[i].y1,
		};
		wlr_renderer_scissor(server->renderer, &part);
		wlr_render_texture(server->renderer, texture, output->transform_matrix, x, y, 1.0f);
	}
	wlr_renderer_scissor(server->renderer, NULL);
	pixman_region32_fini(&visible);
}

static void write_shot(struct server *server, int width, int height) {
	if (server->shot_path[0] == '\0' || server->shot_written) {
		return;
	}
	unsigned shot_frame = 40;
	const char *shot_at = getenv("DEV_DESK_SHOT_FRAME");
	if (shot_at != NULL && atoi(shot_at) > 0) {
		shot_frame = (unsigned)atoi(shot_at);
	}
	if (server->frames < shot_frame) {
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

/* The name the shell shows for a window. The Olympium runtime still titles
 * its own window with its earlier project name; the shell shows the product
 * name, the same one as its dock entry. Other windows keep their own title. */
static const char *view_name(const struct view *view) {
	if (view->is_xwayland) {
		const char *title = view->xwayland_surface != NULL ? view->xwayland_surface->title : NULL;
		return (title != NULL && title[0] != '\0') ? title : "Windows";
	}
	if (view->xdg_toplevel == NULL) {
		return NULL;
	}
	const char *app_id = view->xdg_toplevel->app_id;
	if (app_id != NULL && (strstr(app_id, "linkspan") != NULL || strstr(app_id, "olympium") != NULL)) {
		return "Olympium";
	}
	const char *title = view->xdg_toplevel->title;
	return (title != NULL && title[0] != '\0') ? title : NULL;
}

static int view_title_box(struct view *view, struct wlr_box *box) {
	if (view->scene_tree == NULL || !view->scene_tree->node.enabled) {
		return 0;
	}
	if (view->is_xwayland && view->xwayland_surface != NULL) {
		int width = view->xwayland_surface->width;
		int height = view->xwayland_surface->height;
		if (width < 80 || height < 40) {
			return 0;
		}
		box->x = view->scene_tree->node.x;
		box->y = view->scene_tree->node.y - TITLE_H;
		box->width = width;
		box->height = TITLE_H;
		return 1;
	}
	if (view->xdg_toplevel == NULL) {
		return 0;
	}
	struct wlr_box geo;
	wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);
	int width = geo.width > 0 ? geo.width : view->xdg_toplevel->base->surface->current.width;
	int height = geo.height > 0 ? geo.height : view->xdg_toplevel->base->surface->current.height;
	if (width < 80 || height < 40) {
		return 0;
	}
	/* The scene places the surface so that the tree origin is the window
	 * geometry origin, whatever margin the client keeps around it. */
	box->x = view->scene_tree->node.x;
	box->y = view->scene_tree->node.y - TITLE_H;
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

static void wallpaper_rgb(int y, int screen_h, double *r, double *g, double *b) {
	float t = screen_h > 1 ? (float)y / (float)(screen_h - 1) : 0;
	if (t < 0.55f) {
		float u = t / 0.55f;
		*r = 0.55 + u * 0.35;
		*g = 0.74 + u * 0.12;
		*b = 0.92 - u * 0.18;
	} else {
		float u = (t - 0.55f) / 0.45f;
		*r = 0.90 - u * 0.25;
		*g = 0.86 - u * 0.20;
		*b = 0.74 - u * 0.28;
	}
}

/* Cover the square corner of a client surface so the window reads as rounded. */
static void mask_corner(struct server *server, struct wlr_output *output,
		int x, int y, int radius, int which) {
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, radius, radius);
	cairo_t *cr = cairo_create(surface);
	if (server->paper_data != NULL) {
		/* Repaint the wallpaper that the square client corner covered. */
		cairo_surface_t *paper = cairo_image_surface_create_for_data(
			server->desk_data != NULL ? server->desk_data : server->paper_data,
			CAIRO_FORMAT_RGB24, server->paper_w, server->paper_h, server->paper_stride);
		cairo_set_source_surface(cr, paper, -x, -y);
		cairo_paint(cr);
		cairo_surface_destroy(paper);
	} else {
		double wr, wg, wb;
		wallpaper_rgb(y, server->screen_h, &wr, &wg, &wb);
		cairo_set_source_rgba(cr, wr, wg, wb, 1);
		cairo_paint(cr);
	}
	cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	if (which == 2) {
		cairo_arc(cr, radius, 0, radius, 0, 6.2832);
	} else {
		cairo_arc(cr, 0, 0, radius, 0, 6.2832);
	}
	cairo_fill(cr);
	cairo_destroy(cr);
	struct wlr_texture *texture = texture_from_cairo(server->renderer, surface);
	cairo_surface_destroy(surface);
	render_texture_at(server, output, texture, x, y);
	wlr_texture_destroy(texture);
}

static struct wlr_texture *make_shadow(struct server *server, int width, int height) {
	int pad = 36;
	int sw = width + pad * 2;
	int sh = height + pad * 2;
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
	cairo_t *cr = cairo_create(surface);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.42);
	double r = 16;
	double x = pad;
	double y = pad + 10;
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
	dev_blur_bgra(pixels, sw, sh, stride, 12);
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
	if (!output->shell_dirty && !output->wlr_output->needs_frame &&
			!pixman_region32_not_empty(&scene_output->damage_ring.current)) {
		return;
	}
	output->shell_dirty = 0;
	widgets_refresh(server, 0);
	/* The glass reads this frame's pixels back. Repaint the whole scene first,
	 * so the buffer never still holds glass from an earlier frame. */
	wlr_damage_ring_add_whole(&scene_output->damage_ring);
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
		if (front != NULL && view_name(front) != NULL) {
			snprintf(server->frame_title, sizeof server->frame_title, "%s", view_name(front));
		} else {
			snprintf(server->frame_title, sizeof server->frame_title, "%s", server->title);
		}
		wl_list_for_each_reverse(view, &server->views, link) {
			struct wlr_box title;
			if (!view_title_box(view, &title)) {
				continue;
			}
			int content_h = 0;
			if (view->xdg_toplevel != NULL) {
				struct wlr_box frame_geo;
				wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &frame_geo);
				content_h = frame_geo.height > 0 ? frame_geo.height
					: view->xdg_toplevel->base->surface->current.height;
			} else if (view->is_xwayland && view->xwayland_surface != NULL) {
				content_h = view->xwayland_surface->height;
			}
			int frame_h = title.height + content_h;
			/* The shadow only changes when the window is resized. */
			if (view->shadow == NULL || view->shadow_w != title.width || view->shadow_h != frame_h) {
				wlr_texture_destroy(view->shadow);
				view->shadow = make_shadow(server, title.width, frame_h);
				view->shadow_w = title.width;
				view->shadow_h = frame_h;
			}
			server->clip_view = view;
			render_texture_at(server, output->wlr_output, view->shadow, title.x - 36, title.y - 36);
			char saved[128];
			snprintf(saved, sizeof saved, "%s", server->frame_title);
			if (view_name(view) != NULL) {
				snprintf(server->frame_title, sizeof server->frame_title, "%s",
					view_name(view));
			}
			server->chrome_inactive = view != front;
			struct wlr_texture *bar = make_chrome(server, title.x, title.y,
				title.width, title.height, -12, 3);
			server->chrome_inactive = 0;
			render_texture_at(server, output->wlr_output, bar, title.x, title.y);
			wlr_texture_destroy(bar);
			mask_corner(server, output->wlr_output, title.x, title.y + frame_h - 14, 14, 2);
			mask_corner(server, output->wlr_output, title.x + title.width - 14,
				title.y + frame_h - 14, 14, 3);
			snprintf(server->frame_title, sizeof server->frame_title, "%s", saved);
			server->clip_view = NULL;
		}
		if (access("/run/dev-desktop/open-launchpad", F_OK) == 0) {
			unlink("/run/dev-desktop/open-launchpad");
			open_launchpad(server);
			FILE *filter = fopen("/run/dev-desktop/launchpad-filter", "r");
			if (filter != NULL) {
				if (fgets(server->pad_filter, sizeof server->pad_filter, filter) != NULL) {
					server->pad_filter[strcspn(server->pad_filter, "\r\n")] = '\0';
				}
				fclose(filter);
			}
			wlr_log(WLR_INFO, "launchpad open with %d entries", server->pad_count);
			for (int i = 0; i < server->pad_count; i++) {
				if (strcmp(server->pad[i].icon, "installed") == 0) {
					wlr_log(WLR_INFO, "launchpad entry: %s", server->pad[i].name);
				}
			}
			load_pins(server);
			for (int i = 0; i < server->pinned_count; i++) {
				wlr_log(WLR_INFO, "dock pin: %s", server->pinned[i].name);
			}
		}
		if (server->demo_pad && !server->demo_pad_done && server->frames > 180) {
			pad_load(server);
			server->launchpad = 1;
			server->pad_folder = -1;
			server->pad_cursor = -1;
			server->demo_pad_done = 1;
			wlr_log(WLR_INFO, "demo: launchpad open with %d entries", server->pad_count);
		}
		if (server->launchpad) {
			if (server->pad_count == 0) {
				pad_load(server);
			}
			struct wlr_texture *pad = make_chrome(server, 0, 0,
				server->screen_w, server->screen_h, 0, 6);
			render_texture_at(server, output->wlr_output, pad, 0, 0);
			wlr_texture_destroy(pad);
		}
		if (!server->launchpad) {
			panel = make_chrome(server, 0, 0, server->screen_w, PANEL_H, 0, 0);
		}
		/* A dock dot means that application has a window open right now. */
		server->dock_running = 0;
		wl_list_for_each(view, &server->views, link) {
			const char *vt = view_name(view);
			const char *va = view->xdg_toplevel != NULL ? view->xdg_toplevel->app_id : NULL;
			for (int i = 0; i < APP_COUNT; i++) {
				if (vt != NULL && strcmp(vt, APPS[i].label) == 0) {
					server->dock_running |= 1u << i;
				}
			}
			if (va != NULL && strstr(va, "foot") != NULL) {
				for (int i = 0; i < APP_COUNT; i++) {
					if (strcmp(APPS[i].id, "terminal") == 0) {
						server->dock_running |= 1u << i;
					}
				}
			}
		}
		struct wlr_box dockb;
		dock_box(server, &dockb);
		dock = make_chrome(server, dockb.x, dockb.y, dockb.width, dockb.height, 20, 1);
		render_texture_at(server, output->wlr_output, panel, 0, 0);
		render_texture_at(server, output->wlr_output, dock, dockb.x, dockb.y);
		if (server->hover_app >= 0 && server->hover_app < dock_n(server)) {
			char saved[128];
			snprintf(saved, sizeof saved, "%s", server->frame_title);
			const char *tip_label = server->hover_app < APP_COUNT
				? APPS[server->hover_app].label
				: server->pinned[server->hover_app - APP_COUNT].name;
			snprintf(server->frame_title, sizeof server->frame_title, "%s", tip_label);
			cairo_surface_t *probe = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 4, 4);
			cairo_t *probe_cr = cairo_create(probe);
			int tip_w = paint_text(probe_cr, "Ubuntu Medium 12", server->frame_title, 0, 0, 0, 0, 0) + 22;
			cairo_destroy(probe_cr);
			cairo_surface_destroy(probe);
			int tip_x = dockb.x + 8 + server->hover_app * DOCK_SLOT + DOCK_SLOT / 2 - tip_w / 2;
			struct wlr_texture *tip = make_chrome(server, tip_x, dockb.y - 34, tip_w, 26, 13, 4);
			snprintf(server->frame_title, sizeof server->frame_title, "%s", saved);
			render_texture_at(server, output->wlr_output, tip, tip_x, dockb.y - 34);
			wlr_texture_destroy(tip);
		}
		if (server->demo_menu && !server->demo_menu_done && server->frames > 240) {
			server->open_menu = 1;
			server->demo_menu_done = 1;
			wlr_log(WLR_INFO, "demo: menu open");
		}
		if (server->open_menu >= 0) {
			struct wlr_box drop;
			menu_box(server, &drop);
			struct wlr_texture *drop_shadow = make_shadow(server, drop.width, drop.height);
			render_texture_at(server, output->wlr_output, drop_shadow, drop.x - 36, drop.y - 36);
			wlr_texture_destroy(drop_shadow);
			struct wlr_texture *drop_tex = make_chrome(server, drop.x, drop.y,
				drop.width, drop.height, 10, 5);
			render_texture_at(server, output->wlr_output, drop_tex, drop.x, drop.y);
			wlr_texture_destroy(drop_tex);
		}
		if (server->nag_demo_dismiss && server->nag && server->frames > 600) {
			nag_dismiss(server);
		}
		if (server->nag) {
			struct wlr_box nb;
			nag_box(server, &nb);
			struct wlr_texture *nag_shadow = make_shadow(server, nb.width, nb.height);
			render_texture_at(server, output->wlr_output, nag_shadow, nb.x - 36, nb.y - 36);
			wlr_texture_destroy(nag_shadow);
			struct wlr_texture *nag = make_chrome(server, nb.x, nb.y,
				nb.width, nb.height, 16, 7);
			render_texture_at(server, output->wlr_output, nag, nb.x, nb.y);
			wlr_texture_destroy(nag);
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
	if (wlr_output_commit_state(output->wlr_output, &state)) {
		/* build_state does not consume damage; mirror scene_output_commit's
		 * successful-commit bookkeeping or every frame remains damaged. */
		wlr_damage_ring_rotate(&scene_output->damage_ring);
	} else {
		output->shell_dirty = 1;
		wlr_output_schedule_frame(output->wlr_output);
	}
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

struct paper_buffer {
	struct wlr_buffer base;
	void *data;
	size_t stride;
};

static void paper_destroy(struct wlr_buffer *buffer) {
	struct paper_buffer *paper = (struct paper_buffer *)buffer;
	free(paper->data);
	free(paper);
}

static bool paper_begin(struct wlr_buffer *buffer, uint32_t flags, void **data,
		uint32_t *format, size_t *stride) {
	struct paper_buffer *paper = (struct paper_buffer *)buffer;
	(void)flags;
	*data = paper->data;
	*format = DRM_FORMAT_ARGB8888;
	*stride = paper->stride;
	return true;
}

static void paper_end(struct wlr_buffer *buffer) {
	(void)buffer;
}

static const struct wlr_buffer_impl paper_impl = {
	.destroy = paper_destroy,
	.begin_data_ptr_access = paper_begin,
	.end_data_ptr_access = paper_end,
};

/* Original abstract wallpaper: a pale morning sky over flowing colour ribbons.
 * The soft layer is blurred once at start-up. The ribbon edges stay sharp so
 * the glass above them has detail to bend. */
static void paint_wallpaper_soft(cairo_t *cr, int width, int height) {
	cairo_pattern_t *sky = cairo_pattern_create_linear(0, 0, 0, height);
	cairo_pattern_add_color_stop_rgb(sky, 0.0, 0.66, 0.83, 0.98);
	cairo_pattern_add_color_stop_rgb(sky, 0.30, 0.80, 0.86, 0.99);
	cairo_pattern_add_color_stop_rgb(sky, 0.52, 0.97, 0.84, 0.90);
	cairo_pattern_add_color_stop_rgb(sky, 1.0, 0.99, 0.74, 0.58);
	cairo_set_source(cr, sky);
	cairo_paint(cr);
	cairo_pattern_destroy(sky);
	const double lobes[][6] = {
		{0.18, 0.30, 0.30, 0.52, 0.62, 0.98},
		{0.80, 0.24, 0.26, 0.99, 0.78, 0.62},
		{0.52, 0.46, 0.24, 0.80, 0.56, 0.96},
		{0.10, 0.74, 0.28, 0.30, 0.74, 0.90},
		{0.90, 0.70, 0.30, 0.98, 0.50, 0.62},
	};
	for (int i = 0; i < 5; i++) {
		cairo_set_source_rgba(cr, lobes[i][3], lobes[i][4], lobes[i][5], 0.70);
		cairo_arc(cr, width * lobes[i][0], height * lobes[i][1], height * lobes[i][2], 0, 6.2832);
		cairo_fill(cr);
	}
}

static void ribbon_path(cairo_t *cr, int width, double base, double amp, double freq, double phase) {
	cairo_new_path(cr);
	for (int x = 0; x <= width + 8; x += 8) {
		double t = (double)x / (double)width;
		double y = base + amp * sin(t * freq + phase) + amp * 0.45 * sin(t * freq * 2.3 + phase * 1.7);
		if (x == 0) {
			cairo_move_to(cr, x, y);
		} else {
			cairo_line_to(cr, x, y);
		}
	}
}

static void paint_wallpaper_ribbons(cairo_t *cr, int width, int height) {
	const double bands[][8] = {
		/* base, amp, freq, phase, r, g, b, alpha */
		{0.50, 0.060, 5.2, 0.4, 0.36, 0.60, 0.98, 0.62},
		{0.60, 0.070, 4.4, 2.1, 0.46, 0.44, 0.94, 0.66},
		{0.70, 0.055, 6.0, 3.7, 0.78, 0.42, 0.86, 0.70},
		{0.79, 0.060, 4.8, 5.2, 0.98, 0.50, 0.58, 0.76},
		{0.88, 0.045, 5.6, 0.9, 1.00, 0.72, 0.42, 0.84},
	};
	for (int i = 0; i < 5; i++) {
		double base = height * bands[i][0];
		double amp = height * bands[i][1];
		ribbon_path(cr, width, base, amp, bands[i][2], bands[i][3]);
		cairo_line_to(cr, width + 8, height);
		cairo_line_to(cr, 0, height);
		cairo_close_path(cr);
		cairo_pattern_t *fill = cairo_pattern_create_linear(0, base - amp * 1.5, 0, height);
		cairo_pattern_add_color_stop_rgba(fill, 0.0, bands[i][4], bands[i][5], bands[i][6], bands[i][7]);
		cairo_pattern_add_color_stop_rgba(fill, 1.0, bands[i][4] * 0.82, bands[i][5] * 0.80,
			bands[i][6] * 0.90, bands[i][7]);
		cairo_set_source(cr, fill);
		cairo_fill(cr);
		cairo_pattern_destroy(fill);
		ribbon_path(cr, width, base, amp, bands[i][2], bands[i][3]);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.55);
		cairo_set_line_width(cr, 1.6);
		cairo_stroke(cr);
		ribbon_path(cr, width, base + 5, amp, bands[i][2], bands[i][3]);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
		cairo_set_line_width(cr, 7);
		cairo_stroke(cr);
	}
}

static void ensure_wallpaper(struct server *server, int width, int height) {
	if (server->wallpaper != NULL || width <= 0 || height <= 0) {
		return;
	}
	struct paper_buffer *paper = calloc(1, sizeof(*paper));
	if (paper == NULL) {
		return;
	}
	paper->stride = (size_t)width * 4;
	paper->data = calloc((size_t)height, paper->stride);
	if (paper->data == NULL) {
		free(paper);
		return;
	}
	cairo_surface_t *surface = cairo_image_surface_create_for_data(paper->data,
		CAIRO_FORMAT_ARGB32, width, height, (int)paper->stride);
	cairo_t *cr = cairo_create(surface);
	paint_wallpaper_soft(cr, width, height);
	cairo_destroy(cr);
	cairo_surface_flush(surface);
	dev_blur_bgra(paper->data, width, height, (int)paper->stride, 40);
	dev_blur_bgra(paper->data, width, height, (int)paper->stride, 40);
	cairo_surface_mark_dirty(surface);
	cr = cairo_create(surface);
	paint_wallpaper_ribbons(cr, width, height);
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	server->paper_data = paper->data;
	server->paper_w = width;
	server->paper_h = height;
	server->paper_stride = (int)paper->stride;
	wlr_buffer_init(&paper->base, &paper_impl, width, height);
	server->wallpaper = wlr_scene_buffer_create(&server->scene->tree, &paper->base);
	wlr_scene_node_lower_to_bottom(&server->wallpaper->node);
	wlr_buffer_drop(&paper->base);
}

/* Desktop widgets: a clock, this month's calendar, and memory, disk, and
 * battery. They sit on the wallpaper, under every window, and are glass over
 * the wallpaper behind them. They are drawn here, not by a client, so they
 * cost nothing while nothing changes: the layer is rebuilt every ten seconds
 * and when the setting changes. "widgets" in the desk directory holding
 * "off" turns them off, as does View, Hide Widgets. */
#define WIDGET_W 232
#define WIDGET_GAP 16

static void widgets_flag_path(char *out, size_t n) {
	const char *desk = getenv("OLYMPIUM_DESK_HOME");
	if (desk != NULL && desk[0] != '\0') {
		snprintf(out, n, "%s/widgets", desk);
		return;
	}
	const char *home = getenv("HOME");
	snprintf(out, n, "%s/.local/share/olympium-desk/widgets",
		home != NULL && home[0] != '\0' ? home : "/tmp");
}

static int widgets_enabled(void) {
	char path[512];
	char word[16] = "";
	widgets_flag_path(path, sizeof path);
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		return 1;
	}
	if (fgets(word, sizeof word, f) == NULL) {
		word[0] = '\0';
	}
	fclose(f);
	return strncmp(word, "off", 3) != 0;
}

static void widgets_store(int on) {
	char path[512];
	widgets_flag_path(path, sizeof path);
	FILE *f = fopen(path, "w");
	if (f != NULL) {
		fputs(on ? "on\n" : "off\n", f);
		fclose(f);
	}
}

static void widget_bar(cairo_t *cr, double x, double y, double w, double part,
		double r, double g, double b) {
	if (part < 0) {
		part = 0;
	}
	if (part > 1) {
		part = 1;
	}
	rrect(cr, x, y, w, 6, 3);
	cairo_set_source_rgba(cr, 0.10, 0.10, 0.14, 0.14);
	cairo_fill(cr);
	if (part > 0.01) {
		rrect(cr, x, y, w * part < 6 ? 6 : w * part, 6, 3);
		cairo_set_source_rgba(cr, r, g, b, 0.95);
		cairo_fill(cr);
	}
}

static void widget_clock(cairo_t *cr, int w, int h, const struct tm *tm) {
	(void)h;
	char text[64];
	strftime(text, sizeof text, "%A", tm);
	paint_text(cr, "Ubuntu Bold 11", text, 18, 14, 0.86, 0.22, 0.20);
	int hour = tm->tm_hour % 12;
	snprintf(text, sizeof text, "%d:%02d", hour == 0 ? 12 : hour, tm->tm_min);
	int tw = paint_text(cr, "Ubuntu Light 40", text, 16, 26, 0.10, 0.10, 0.13);
	paint_text(cr, "Ubuntu Medium 13", tm->tm_hour < 12 ? "AM" : "PM", 16 + tw + 6, 58,
		0.30, 0.31, 0.36);
	strftime(text, sizeof text, "%B %-d, %Y", tm);
	paint_text(cr, "Ubuntu 12", text, 18, 86, 0.26, 0.27, 0.32);
	(void)w;
}

static void widget_calendar(cairo_t *cr, int w, int h, const struct tm *tm) {
	(void)h;
	char text[32];
	strftime(text, sizeof text, "%B", tm);
	paint_text(cr, "Ubuntu Bold 11", text, 18, 12, 0.86, 0.22, 0.20);
	const char *days[] = {"S", "M", "T", "W", "T", "F", "S"};
	double cell = (w - 28) / 7.0;
	for (int i = 0; i < 7; i++) {
		int tw = paint_text(cr, "Ubuntu Medium 9", days[i], 0, -40, 0, 0, 0);
		paint_text(cr, "Ubuntu Medium 9", days[i], 14 + i * cell + (cell - tw) / 2, 34,
			0.42, 0.43, 0.48);
	}
	struct tm first = *tm;
	first.tm_mday = 1;
	mktime(&first);
	static const int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int year = tm->tm_year + 1900;
	int count = lengths[tm->tm_mon];
	if (tm->tm_mon == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
		count = 29;
	}
	for (int day = 1; day <= count; day++) {
		int slot = first.tm_wday + day - 1;
		double cx = 14 + (slot % 7) * cell + cell / 2;
		double cy = 62 + (slot / 7) * 21;
		snprintf(text, sizeof text, "%d", day);
		int today = day == tm->tm_mday;
		if (today) {
			cairo_arc(cr, cx, cy, 10, 0, 6.2832);
			cairo_set_source_rgba(cr, 0.90, 0.22, 0.20, 1);
			cairo_fill(cr);
		}
		const char *font = today ? "Ubuntu Bold 10" : "Ubuntu 10";
		int tw = paint_text(cr, font, text, 0, -40, 0, 0, 0);
		double shade = today ? 1.0 : 0.14;
		paint_text(cr, font, text, cx - tw / 2.0, cy - 8, shade, shade, today ? 1.0 : 0.17);
	}
}

static void widget_system(cairo_t *cr, int w, int h) {
	(void)h;
	paint_text(cr, "Ubuntu Bold 11", "This Computer", 18, 12, 0.86, 0.22, 0.20);
	long total = 0;
	long avail = 0;
	FILE *f = fopen("/proc/meminfo", "r");
	if (f != NULL) {
		char line[128];
		while (fgets(line, sizeof line, f) != NULL) {
			sscanf(line, "MemTotal: %ld kB", &total);
			sscanf(line, "MemAvailable: %ld kB", &avail);
		}
		fclose(f);
	}
	char text[96];
	double y = 36;
	if (total > 0) {
		double used = (double)(total - avail) / (double)total;
		snprintf(text, sizeof text, "Memory   %.1f of %.1f GB", (total - avail) / 1048576.0,
			total / 1048576.0);
		paint_text(cr, "Ubuntu 11", text, 18, y, 0.14, 0.14, 0.17);
		widget_bar(cr, 18, y + 20, w - 36, used, 0.20, 0.52, 0.96);
		y += 38;
	}
	struct statvfs vfs;
	if (statvfs("/", &vfs) == 0 && vfs.f_blocks > 0) {
		double size = (double)vfs.f_blocks * (double)vfs.f_frsize;
		double free_bytes = (double)vfs.f_bavail * (double)vfs.f_frsize;
		snprintf(text, sizeof text, "Disk   %.1f GB free", free_bytes / 1073741824.0);
		paint_text(cr, "Ubuntu 11", text, 18, y, 0.14, 0.14, 0.17);
		widget_bar(cr, 18, y + 20, w - 36, 1.0 - free_bytes / size, 0.30, 0.72, 0.40);
		y += 38;
	}
	int battery = -1;
	for (int i = 0; i < 2 && battery < 0; i++) {
		char path[64];
		snprintf(path, sizeof path, "/sys/class/power_supply/BAT%d/capacity", i);
		f = fopen(path, "r");
		if (f != NULL) {
			if (fscanf(f, "%d", &battery) != 1) {
				battery = -1;
			}
			fclose(f);
		}
	}
	if (battery >= 0) {
		snprintf(text, sizeof text, "Battery   %d%%", battery);
		paint_text(cr, "Ubuntu 11", text, 18, y, 0.14, 0.14, 0.17);
		widget_bar(cr, 18, y + 20, w - 36, battery / 100.0, 0.96, 0.70, 0.18);
	} else {
		paint_text(cr, "Ubuntu 11", "Power   no battery", 18, y, 0.14, 0.14, 0.17);
	}
}

static void widgets_refresh(struct server *server, int force) {
	if (server->paper_data == NULL || server->paper_w <= 0) {
		return;
	}
	time_t now = time(NULL);
	if (!force && server->widgets != NULL && now / 10 == server->widgets_stamp / 10 &&
			server->widgets_contrast == server->contrast) {
		return;
	}
	server->widgets_stamp = now;
	server->widgets_contrast = server->contrast;
	int width = server->paper_w;
	int height = server->paper_h;
	int stride = server->paper_stride;
	if (server->desk_data == NULL) {
		server->desk_data = malloc((size_t)stride * (size_t)height);
		if (server->desk_data == NULL) {
			return;
		}
	}
	memcpy(server->desk_data, server->paper_data, (size_t)stride * (size_t)height);
	if (!server->widgets_on) {
		if (server->widgets != NULL) {
			wlr_scene_node_set_enabled(&server->widgets->node, false);
		}
		return;
	}
	struct paper_buffer *layer = calloc(1, sizeof(*layer));
	if (layer == NULL) {
		return;
	}
	layer->stride = (size_t)stride;
	layer->data = calloc((size_t)height, layer->stride);
	if (layer->data == NULL) {
		free(layer);
		return;
	}
	cairo_surface_t *target = cairo_image_surface_create_for_data(layer->data,
		CAIRO_FORMAT_ARGB32, width, height, stride);
	cairo_t *out = cairo_create(target);
	struct tm tm;
	localtime_r(&now, &tm);
	const int heights[] = {116, 196, 142};
	int x = width - WIDGET_W - 24;
	int y = PANEL_H + 22;
	for (int i = 0; i < 3; i++) {
		int h = heights[i];
		if (x < 8 || y + h > height - DOCK_H - 24) {
			break;
		}
		uint8_t *back = malloc((size_t)WIDGET_W * 4 * (size_t)h);
		if (back == NULL) {
			break;
		}
		for (int row = 0; row < h; row++) {
			memcpy(back + (size_t)row * WIDGET_W * 4,
				server->paper_data + (size_t)(y + row) * stride + (size_t)x * 4,
				(size_t)WIDGET_W * 4);
		}
		dev_blur_bgra(back, WIDGET_W, h, WIDGET_W * 4, 14);
		cairo_surface_t *glass = frosted_surface(back, WIDGET_W, h, WIDGET_W * 4,
			server->contrast, 22, 0.46);
		cairo_t *cr = cairo_create(glass);
		if (i == 0) {
			widget_clock(cr, WIDGET_W, h, &tm);
		} else if (i == 1) {
			widget_calendar(cr, WIDGET_W, h, &tm);
		} else {
			widget_system(cr, WIDGET_W, h);
		}
		cairo_destroy(cr);
		/* A soft shadow so the glass lifts off the wallpaper. */
		for (int ring = 6; ring >= 1; ring--) {
			rrect(out, x - ring, y - ring + 3, WIDGET_W + 2 * ring, h + 2 * ring, 22 + ring);
			cairo_set_source_rgba(out, 0.08, 0.10, 0.20, 0.022);
			cairo_fill(out);
		}
		cairo_set_source_surface(out, glass, x, y);
		cairo_paint(out);
		cairo_surface_destroy(glass);
		free(back);
		y += h + WIDGET_GAP;
	}
	cairo_destroy(out);
	cairo_surface_flush(target);
	/* Window corners are repainted from this copy of the desktop. */
	cairo_surface_t *desk = cairo_image_surface_create_for_data(server->desk_data,
		CAIRO_FORMAT_RGB24, width, height, stride);
	cairo_t *dcr = cairo_create(desk);
	cairo_set_source_surface(dcr, target, 0, 0);
	cairo_paint(dcr);
	cairo_destroy(dcr);
	cairo_surface_destroy(desk);
	cairo_surface_destroy(target);
	wlr_buffer_init(&layer->base, &paper_impl, width, height);
	if (server->widgets == NULL) {
		server->widgets = wlr_scene_buffer_create(&server->scene->tree, &layer->base);
		wlr_scene_node_lower_to_bottom(&server->widgets->node);
		if (server->wallpaper != NULL) {
			wlr_scene_node_lower_to_bottom(&server->wallpaper->node);
		}
	} else {
		wlr_scene_buffer_set_buffer(server->widgets, &layer->base);
	}
	wlr_scene_node_set_enabled(&server->widgets->node, true);
	wlr_buffer_drop(&layer->base);
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
	output->shell_dirty = 1;
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
		struct wlr_xwayland_surface *previous_xwayland =
			wlr_xwayland_surface_try_from_wlr_surface(previous);
		if (previous_xwayland != NULL) {
			wlr_xwayland_surface_activate(previous_xwayland, false);
		}
	}
	wlr_scene_node_raise_to_top(&view->scene_tree->node);
	wl_list_remove(&view->link);
	wl_list_insert(&server->views, &view->link);
	if (view->xdg_toplevel != NULL) {
		wlr_xdg_toplevel_set_activated(view->xdg_toplevel, true);
	} else if (view->xwayland_surface != NULL) {
		wlr_xwayland_surface_activate(view->xwayland_surface, true);
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard != NULL && surface != NULL) {
		wlr_seat_keyboard_notify_enter(seat, surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	} else if (surface != NULL) {
		/* No keyboard is attached. The seat still has to know which window
		 * is in front: the clipboard is offered to the focused client, and
		 * without this a touch-only machine could never paste. */
		wlr_seat_keyboard_notify_enter(seat, surface, NULL, 0, NULL);
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
	if (server->nag) {
		struct wlr_box nb;
		nag_box(server, &nb);
		if (point_in(&nb, x, y)) {
			return 1;
		}
	}
	if (server->launchpad) {
		return 1;
	}
	if (y < PANEL_H) {
		return 1;
	}
	struct wlr_box dock;
	dock_box(server, &dock);
	if (point_in(&dock, x, y)) {
		return 1;
	}
	if (server->open_menu >= 0) {
		struct wlr_box drop;
		menu_box(server, &drop);
		if (point_in(&drop, x, y)) {
			return 1;
		}
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

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
	struct keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct server *server = keyboard->server;
	shell_redraw(server);
	struct wlr_keyboard_key_event *event = data;
	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);
	int handled = 0;
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (server->nag) {
			for (int i = 0; i < nsyms; i++) {
				if (syms[i] == XKB_KEY_Escape) {
					nag_dismiss(server);
					handled = 1;
				}
			}
		}
		if (!handled && server->launchpad) {
			for (int i = 0; i < nsyms; i++) {
				if (syms[i] == XKB_KEY_Escape) {
					if (server->pad_filter[0] != '\0') {
						server->pad_filter[0] = '\0';
					} else if (server->pad_folder >= 0) {
						server->pad_folder = -1;
						server->pad_page = 0;
					} else {
						server->launchpad = 0;
					}
					handled = 1;
				} else if (syms[i] == XKB_KEY_Return) {
					int ids[PAD_MAX];
					int n = pad_list(server, ids, PAD_MAX);
					int at = server->pad_page * PAD_PER + server->pad_cursor;
					if (server->pad_cursor >= 0 && at < n) {
						pad_activate(server, ids[at]);
					}
					handled = 1;
				} else if (syms[i] == XKB_KEY_Left) {
					if (server->pad_cursor > 0) {
						server->pad_cursor--;
					}
					handled = 1;
				} else if (syms[i] == XKB_KEY_Right) {
					if (server->pad_cursor < PAD_PER - 1) {
						server->pad_cursor++;
					}
					handled = 1;
				} else if (syms[i] == XKB_KEY_Down) {
					if (server->pad_cursor + PAD_COLS < PAD_PER) {
						server->pad_cursor += PAD_COLS;
					}
					handled = 1;
				} else if (syms[i] == XKB_KEY_Up) {
					if (server->pad_cursor >= PAD_COLS) {
						server->pad_cursor -= PAD_COLS;
					}
					handled = 1;
				} else if (syms[i] == XKB_KEY_Next) {
					server->pad_page++;
					server->pad_cursor = -1;
					handled = 1;
				} else if (syms[i] == XKB_KEY_Prior) {
					if (server->pad_page > 0) {
						server->pad_page--;
					}
					server->pad_cursor = -1;
					handled = 1;
				} else if (syms[i] == XKB_KEY_BackSpace) {
					size_t n = strlen(server->pad_filter);
					if (n > 0) {
						server->pad_filter[n - 1] = '\0';
					}
					handled = 1;
				} else {
					char utf[8];
					int len = xkb_keysym_to_utf8(syms[i], utf, sizeof utf);
					if (len > 1 && strlen(server->pad_filter) + (size_t)len < sizeof server->pad_filter) {
						strcat(server->pad_filter, utf);
						server->pad_page = 0;
						handled = 1;
					}
				}
			}
		}
		for (int i = 0; i < nsyms && !handled; i++) {
			if (syms[i] == XKB_KEY_Super_L || syms[i] == XKB_KEY_Super_R) {
				if (server->launchpad) {
					server->launchpad = 0;
				} else {
					open_launchpad(server);
				}
				handled = 1;
			}
		}
		/* Control or Super with Space opens and closes the search panel. */
		uint32_t held = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
		for (int i = 0; i < nsyms && !handled; i++) {
			if (syms[i] == XKB_KEY_space && (held & (WLR_MODIFIER_CTRL | WLR_MODIFIER_LOGO)) != 0) {
				if (server->launcher) {
					close_search(server);
				} else {
					open_search(server);
				}
				handled = 1;
			}
		}
		if (!handled && server->launcher) {
			for (int i = 0; i < nsyms; i++) {
				if (syms[i] == XKB_KEY_Escape) {
					close_search(server);
					handled = 1;
				} else if (syms[i] == XKB_KEY_Return) {
					spot_activate(server);
					handled = 1;
				} else if (syms[i] == XKB_KEY_Down) {
					if (server->spot_cursor < SPOT_ROWS - 1) {
						server->spot_cursor++;
					}
					handled = 1;
				} else if (syms[i] == XKB_KEY_Up) {
					if (server->spot_cursor > 0) {
						server->spot_cursor--;
					}
					handled = 1;
				} else if (syms[i] == XKB_KEY_BackSpace) {
					/* One whole character, which may be several bytes. */
					size_t n = strlen(server->filter);
					while (n > 0 && ((unsigned char)server->filter[n - 1] & 0xC0) == 0x80) {
						n--;
					}
					if (n > 0) {
						n--;
					}
					server->filter[n] = '\0';
					server->spot_cursor = 0;
					handled = 1;
				} else {
					char utf[8];
					int len = xkb_keysym_to_utf8(syms[i], utf, sizeof utf);
					if (len > 1 && (unsigned char)utf[0] >= 0x20 &&
							strlen(server->filter) + (size_t)len < sizeof server->filter) {
						strcat(server->filter, utf);
						server->spot_cursor = 0;
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

#ifdef OLYMPIUM_TEST_INPUT
/* Only the separately compiled input-validation compositor exposes this
 * protocol. Normal desktop binaries do not expose this protocol. */
static void server_new_virtual_pointer(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, new_virtual_pointer);
	struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
	server_new_pointer(server, &event->new_pointer->pointer.base);
}
#endif

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

/* Clipboard keeper. On Wayland and on X the clipboard is not stored
 * anywhere: the program that copied hands the bytes over when someone
 * pastes, so closing that program empties the clipboard. That is not what a
 * person expects, and it is worse across systems, where closing a Windows
 * program would lose what was copied from it. The desktop therefore reads
 * text shortly after it is copied and, when the owner goes away, offers the
 * same text itself. Text only, up to CLIP_MAX bytes. Nothing is written to
 * disk. */
#define CLIP_MAX (4 * 1024 * 1024)
#define CLIP_SETTLE_MS 250

static const char *const CLIP_MIMES[] = {
	"text/plain;charset=utf-8", "UTF8_STRING", "text/plain", "STRING", "TEXT",
};

struct clip_keeper {
	struct wlr_data_source base;
	char *data;
	size_t len;
};

static void keeper_write(int fd, const char *data, size_t len) {
	size_t done = 0;
	while (done < len) {
		ssize_t n = write(fd, data + done, len - done);
		if (n < 0 && errno == EINTR) {
			continue;
		}
		if (n <= 0) {
			break;
		}
		done += (size_t)n;
	}
}

static void keeper_send(struct wlr_data_source *source, const char *mime, int32_t fd) {
	(void)mime;
	struct clip_keeper *keeper = wl_container_of(source, keeper, base);
	/* The reader may be this same process (Xwayland's bridge reads from the
	 * event loop), so never block here. A pipe takes 64 KiB without a
	 * reader; anything longer is written by a child. */
	int flags = fcntl(fd, F_GETFL);
	if (keeper->len <= 60000) {
		if (flags >= 0) {
			fcntl(fd, F_SETFL, flags | O_NONBLOCK);
		}
		keeper_write(fd, keeper->data, keeper->len);
	} else if (fork() == 0) {
		if (flags >= 0) {
			fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
		}
		keeper_write(fd, keeper->data, keeper->len);
		_exit(0);
	}
	close(fd);
}

static void keeper_destroy(struct wlr_data_source *source) {
	struct clip_keeper *keeper = wl_container_of(source, keeper, base);
	free(keeper->data);
	free(keeper);
}

static const struct wlr_data_source_impl keeper_impl = {
	.send = keeper_send,
	.destroy = keeper_destroy,
};

static void clip_stop_reading(struct server *server) {
	if (server->clip_reader != NULL) {
		wl_event_source_remove(server->clip_reader);
		server->clip_reader = NULL;
	}
	if (server->clip_fd >= 0) {
		close(server->clip_fd);
		server->clip_fd = -1;
	}
	free(server->clip_pending);
	server->clip_pending = NULL;
	server->clip_pending_len = 0;
}

static int clip_readable(int fd, uint32_t mask, void *data) {
	struct server *server = data;
	(void)mask;
	char buf[8192];
	ssize_t n = read(fd, buf, sizeof buf);
	if (n > 0) {
		if (server->clip_pending_len + (size_t)n > CLIP_MAX) {
			/* Too long to keep. The owner still serves it while it lives. */
			clip_stop_reading(server);
			return 0;
		}
		char *grown = realloc(server->clip_pending, server->clip_pending_len + (size_t)n);
		if (grown == NULL) {
			clip_stop_reading(server);
			return 0;
		}
		memcpy(grown + server->clip_pending_len, buf, (size_t)n);
		server->clip_pending = grown;
		server->clip_pending_len += (size_t)n;
		return 0;
	}
	if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
		return 0;
	}
	if (server->clip_pending_len > 0) {
		free(server->clip_data);
		server->clip_data = server->clip_pending;
		server->clip_len = server->clip_pending_len;
		server->clip_pending = NULL;
		server->clip_pending_len = 0;
		wlr_log(WLR_INFO, "clipboard kept: %zu bytes", server->clip_len);
	}
	clip_stop_reading(server);
	return 0;
}

static void clip_offer(struct server *server) {
	if (server->seat->selection_source != NULL ||
			server->clip_data == NULL || server->clip_len == 0) {
		return;
	}
	struct clip_keeper *keeper = calloc(1, sizeof(*keeper));
	if (keeper == NULL) {
		return;
	}
	keeper->data = malloc(server->clip_len);
	if (keeper->data == NULL) {
		free(keeper);
		return;
	}
	memcpy(keeper->data, server->clip_data, server->clip_len);
	keeper->len = server->clip_len;
	wlr_data_source_init(&keeper->base, &keeper_impl);
	for (size_t i = 0; i < sizeof CLIP_MIMES / sizeof CLIP_MIMES[0]; i++) {
		char **slot = wl_array_add(&keeper->base.mime_types, sizeof(char *));
		if (slot != NULL) {
			*slot = strdup(CLIP_MIMES[i]);
		}
	}
	wlr_seat_set_selection(server->seat, &keeper->base,
		wl_display_next_serial(server->wl_display));
	wlr_log(WLR_INFO, "clipboard offered by the desktop: %zu bytes", keeper->len);
}

/* dev-clip-helper watches the X clipboard and writes one character per
 * change: 0 nobody owns it, x an X program does, b the bridge does, s the
 * bridge may hold a stale answer. See clip-helper.c for why this is a
 * separate process. */
static void clip_helper_gone(struct server *server) {
	if (server->clip_helper_source != NULL) {
		wl_event_source_remove(server->clip_helper_source);
		server->clip_helper_source = NULL;
	}
	if (server->clip_helper_fd >= 0) {
		close(server->clip_helper_fd);
		server->clip_helper_fd = -1;
	}
	if (server->clip_helper_pid > 0) {
		kill(server->clip_helper_pid, SIGTERM);
		waitpid(server->clip_helper_pid, NULL, 0);
		server->clip_helper_pid = 0;
	}
	server->x_clip_state = '?';
}

static int clip_helper_readable(int fd, uint32_t mask, void *data) {
	struct server *server = data;
	char buf[64];
	ssize_t n = read(fd, buf, sizeof buf);
	if (n <= 0) {
		if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
			return 0;
		}
		wlr_log(WLR_INFO, "clipboard helper stopped");
		clip_helper_gone(server);
		return 0;
	}
	(void)mask;
	for (ssize_t i = 0; i < n; i++) {
		char c = buf[i];
		if (c == 's') {
			/* The bridge may now answer a paste by asking itself. Empty
			 * the clipboard, which makes the bridge let go, and forget
			 * the kept text so that nothing older comes back. */
			wlr_log(WLR_INFO, "clipboard reset: the X bridge was left with a stale owner");
			clip_stop_reading(server);
			free(server->clip_data);
			server->clip_data = NULL;
			server->clip_len = 0;
			wlr_seat_set_selection(server->seat, NULL,
				wl_display_next_serial(server->wl_display));
		} else if (c == '0' || c == 'x' || c == 'b') {
			server->x_clip_state = c;
			if (c == '0' && server->clip_timer != NULL &&
					server->seat->selection_source == NULL && server->clip_data != NULL) {
				/* The X side has let go. The kept text can be offered. */
				wl_event_source_timer_update(server->clip_timer, CLIP_SETTLE_MS);
			}
		}
	}
	return 0;
}

static void clip_helper_start(struct server *server) {
	clip_helper_gone(server);
	char beside[512] = "";
	ssize_t len = readlink("/proc/self/exe", beside, sizeof beside - 32);
	if (len > 0) {
		beside[len] = '\0';
		char *slash = strrchr(beside, '/');
		if (slash != NULL) {
			snprintf(slash + 1, sizeof beside - (size_t)(slash + 1 - beside), "dev-clip-helper");
		}
	}
	const char *candidates[] = {
		getenv("OLYMPIUM_CLIP_HELPER"),
		"/usr/libexec/dev-desktop/dev-clip-helper",
		beside,
	};
	const char *path = NULL;
	for (size_t i = 0; i < sizeof candidates / sizeof candidates[0]; i++) {
		if (candidates[i] != NULL && candidates[i][0] != '\0' && access(candidates[i], X_OK) == 0) {
			path = candidates[i];
			break;
		}
	}
	if (path == NULL) {
		wlr_log(WLR_INFO, "clipboard helper not installed; kept text is not offered while X runs");
		return;
	}
	int fds[2];
	if (pipe2(fds, O_CLOEXEC) != 0) {
		return;
	}
	pid_t pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return;
	}
	if (pid == 0) {
		dup2(fds[1], 1);
		execl(path, path, (char *)NULL);
		_exit(127);
	}
	close(fds[1]);
	fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
	server->clip_helper_pid = pid;
	server->clip_helper_fd = fds[0];
	server->clip_helper_source = wl_event_loop_add_fd(
		wl_display_get_event_loop(server->wl_display), fds[0],
		WL_EVENT_READABLE | WL_EVENT_HANGUP, clip_helper_readable, server);
	wlr_log(WLR_INFO, "clipboard helper started: %s", path);
}

/* Runs CLIP_SETTLE_MS after the clipboard last changed. Programs, Wine
 * above all, set and clear the clipboard several times in a row; nothing is
 * done until they have stopped. */
static int clip_settled(void *data) {
	struct server *server = data;
	struct wlr_data_source *source = server->seat->selection_source;
	if (source == NULL) {
		/* The owner went away and nobody copied since. Offer its text,
		 * once the X side agrees that nobody owns the clipboard. */
		if (server->clip_data == NULL || server->clip_len == 0) {
			return 0;
		}
		/* With an X server running, only while nobody owns the X
		 * clipboard. The helper's next report arms this timer again. */
		if (!server->xwayland_up || server->x_clip_state == '0') {
			clip_offer(server);
		}
		return 0;
	}
	if (source->impl == &keeper_impl || !server->clip_want_read) {
		return 0;
	}
	server->clip_want_read = 0;
	const char *want = NULL;
	for (size_t i = 0; want == NULL && i < sizeof CLIP_MIMES / sizeof CLIP_MIMES[0]; i++) {
		char **mime;
		wl_array_for_each(mime, &source->mime_types) {
			if (strcmp(*mime, CLIP_MIMES[i]) == 0) {
				want = CLIP_MIMES[i];
				break;
			}
		}
	}
	clip_stop_reading(server);
	if (want == NULL) {
		/* Not text. Do not bring old text back after it. */
		free(server->clip_data);
		server->clip_data = NULL;
		server->clip_len = 0;
		return 0;
	}
	int fds[2];
	if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) {
		return 0;
	}
	wlr_data_source_send(source, want, fds[1]);
	server->clip_fd = fds[0];
	server->clip_reader = wl_event_loop_add_fd(wl_display_get_event_loop(server->wl_display),
		fds[0], WL_EVENT_READABLE | WL_EVENT_HANGUP, clip_readable, server);
	return 0;
}

static void seat_selection_changed(struct wl_listener *listener, void *data) {
	(void)data;
	struct server *server = wl_container_of(listener, server, selection_changed);
	struct wlr_data_source *source = server->seat->selection_source;
	if (source != NULL && source->impl == &keeper_impl) {
		return;
	}
	if (source != NULL) {
		server->clip_want_read = 1;
	}
	/* Nothing is done from inside this notification: the seat and Xwayland
	 * are still in the middle of changing owners. */
	wl_event_source_timer_update(server->clip_timer, CLIP_SETTLE_MS);
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
	shell_redraw(server);
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

/* Index 0 is the application-name menu, 1 to 6 are File to Help, and 7 is
 * the menu under the dragon mark. */

static const char *MENU_ITEMS[][6] = {
	{"About Olympium", "Settings\u2026", "Hide Window", NULL, NULL, NULL},
	{"New Files Window", "Open Terminal", "Close Window", NULL, NULL, NULL},
	{"Find in Launchpad", NULL, NULL, NULL, NULL, NULL},
	{"Show Hidden Files", "Reduce Transparency", "Show or Hide Widgets", NULL, NULL, NULL},
	{"Home", "Documents", "Applications", "Computer", NULL, NULL},
	{"Minimize", "Zoom", "Cycle Through Windows", NULL, NULL, NULL},
	{"Olympium Help", "Compatibility", NULL, NULL, NULL, NULL},
	{"About Olympium", "System Settings\u2026", "Launchpad", "Restart\u2026", "Shut Down\u2026", NULL},
	{"Reduce Transparency", "Widgets", "Network", "System Settings\u2026", NULL, NULL},
};

static int menu_item_count(int menu) {
	int n = 0;
	if (menu < 0 || menu >= MENU_COUNT) {
		return 0;
	}
	while (n < MENU_MAX_ITEMS && MENU_ITEMS[menu][n] != NULL) {
		n++;
	}
	return n;
}

static void menu_box(const struct server *server, struct wlr_box *box) {
	int n = menu_item_count(server->open_menu);
	box->width = 230;
	if (server->open_menu == MENU_DRAGON) {
		box->x = 6;
	} else if (server->open_menu == MENU_QUICK) {
		/* Under its glyph, with its right edge kept on the screen. */
		box->width = 250;
		box->x = server->quick_x + 22 - box->width;
		if (box->x + box->width > server->screen_w - 6) {
			box->x = server->screen_w - 6 - box->width;
		}
	} else {
		box->x = server->menu_x[server->open_menu] - 8;
	}
	box->y = PANEL_H + 2;
	box->height = 10 + n * 28;
}

static void launch_files_at(const char *path) {
	if (fork() == 0) {
		setsid();
		execl("/usr/bin/dev-files", "dev-files", path, (char *)NULL);
		_exit(127);
	}
}

static struct view *front_view(struct server *server) {
	struct view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->scene_tree->node.enabled) {
			return view;
		}
	}
	return NULL;
}

static void spawn2(const char *path, const char *arg) {
	if (fork() == 0) {
		setsid();
		execl(path, path, arg, (char *)NULL);
		_exit(127);
	}
}

static void open_launchpad(struct server *server) {
	pad_load(server);
	server->pad_page = 0;
	server->pad_folder = -1;
	server->pad_cursor = -1;
	server->pad_filter[0] = '\0';
	server->launchpad = 1;
}

static const struct app_entry *app_by_id(const char *id) {
	for (int i = 0; i < APP_COUNT; i++) {
		if (strcmp(APPS[i].id, id) == 0) {
			return &APPS[i];
		}
	}
	return &APPS[0];
}

static void menu_activate(struct server *server, int menu, int item) {
	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		home = "/root";
	}
	struct view *front = front_view(server);
	server->open_menu = -1;
	if (menu == MENU_DRAGON) {
		if (item == 0) {
			spawn2("/usr/bin/dev-settings", "--page=about");
		} else if (item == 1) {
			launch_app(server, app_by_id("settings"));
		} else if (item == 2) {
			open_launchpad(server);
		} else if (item == 3) {
			spawn2("/usr/bin/systemctl", "reboot");
		} else if (item == 4) {
			spawn2("/usr/bin/systemctl", "poweroff");
		}
	} else if (menu == 0) {
		if (item == 0) {
			spawn2("/usr/bin/dev-settings", "--page=about");
		} else if (item == 1) {
			launch_app(server, app_by_id("settings"));
		} else if (item == 2 && front != NULL) {
			wlr_scene_node_set_enabled(&front->scene_tree->node, false);
		}
	} else if (menu == 1) {
		if (item == 0) {
			launch_files_at(home);
		} else if (item == 1) {
			launch_app(server, app_by_id("terminal"));
		} else if (item == 2 && front != NULL) {
			if (front->xdg_toplevel != NULL) {
				wlr_xdg_toplevel_send_close(front->xdg_toplevel);
			} else if (front->xwayland_surface != NULL) {
				wlr_xwayland_surface_close(front->xwayland_surface);
			}
		}
	} else if (menu == 2) {
		open_launchpad(server);
	} else if (menu == 3) {
		if (item == 0) {
			setenv("OLYMPIUM_SHOW_HIDDEN", "1", 1);
			launch_files_at(home);
		} else if (item == 1) {
			server->contrast = !server->contrast;
		} else if (item == 2) {
			server->widgets_on = !server->widgets_on;
			widgets_store(server->widgets_on);
			widgets_refresh(server, 1);
			wlr_log(WLR_INFO, "widgets %s", server->widgets_on ? "on" : "off");
		}
	} else if (menu == 4) {
		const char *places[] = {home, "/usr/share/doc", "/usr/bin", "/"};
		if (item >= 0 && item < 4) {
			launch_files_at(places[item]);
		}
	} else if (menu == 5) {
		if (item == 0 && front != NULL) {
			wlr_scene_node_set_enabled(&front->scene_tree->node, false);
		} else if (item == 1 && front != NULL) {
			int width = server->screen_w - 80;
			int height = server->screen_h - PANEL_H - DOCK_H - TITLE_H - 36;
			if (front->xdg_toplevel != NULL) {
				wlr_xdg_toplevel_set_size(front->xdg_toplevel, width, height);
			} else if (front->xwayland_surface != NULL) {
				wlr_xwayland_surface_configure(front->xwayland_surface, 40,
					PANEL_H + TITLE_H + 8, (uint16_t)width, (uint16_t)height);
			}
			wlr_scene_node_set_position(&front->scene_tree->node, 40, PANEL_H + TITLE_H + 8);
		} else if (item == 2 && !wl_list_empty(&server->views)) {
			struct view *next = wl_container_of(server->views.prev, next, link);
			wlr_scene_node_set_enabled(&next->scene_tree->node, true);
			struct wlr_surface *next_surface = next->xdg_toplevel != NULL
				? next->xdg_toplevel->base->surface
				: (next->xwayland_surface != NULL ? next->xwayland_surface->surface : NULL);
			focus_view(next, next_surface);
		}
	} else if (menu == 6) {
		spawn2("/usr/bin/dev-settings", item == 0 ? "--page=olympium" : "--page=compat");
	} else if (menu == MENU_QUICK) {
		if (item == 0) {
			server->contrast = !server->contrast;
			wlr_log(WLR_INFO, "reduce transparency %s", server->contrast ? "on" : "off");
		} else if (item == 1) {
			server->widgets_on = !server->widgets_on;
			widgets_store(server->widgets_on);
			widgets_refresh(server, 1);
			wlr_log(WLR_INFO, "widgets %s", server->widgets_on ? "on" : "off");
		} else if (item == 3) {
			launch_app(server, app_by_id("settings"));
		}
	}
}

static struct pad_app *pad_add(struct server *server, const char *name, const char *cmd,
		const char *icon, const char *group, int folder) {
	if (server->pad_count >= PAD_MAX || name == NULL || name[0] == '\0') {
		return NULL;
	}
	struct pad_app *app = &server->pad[server->pad_count++];
	memset(app, 0, sizeof(*app));
	snprintf(app->name, sizeof app->name, "%s", name);
	snprintf(app->cmd, sizeof app->cmd, "%s", cmd != NULL ? cmd : "");
	snprintf(app->icon, sizeof app->icon, "%s", icon != NULL ? icon : "generic");
	snprintf(app->group, sizeof app->group, "%s", group != NULL ? group : "");
	app->is_folder = folder;
	return app;
}

/* Add an entry only when the program it starts is really on this system. */
static void pad_tool(struct server *server, const char *name, const char *program,
		const char *cmd, const char *icon, const char *group) {
	if (access(program, X_OK) != 0) {
		return;
	}
	pad_add(server, name, cmd, icon, group, 0);
}

/* Finds a program the way a shell would. */
static int program_exists(const char *program) {
	if (program[0] == '\0') {
		return 0;
	}
	if (strchr(program, '/') != NULL) {
		return access(program, X_OK) == 0;
	}
	const char *path = getenv("PATH");
	if (path == NULL || path[0] == '\0') {
		path = "/usr/local/bin:/usr/bin:/bin:/usr/games";
	}
	char full[600];
	while (*path != '\0') {
		size_t len = strcspn(path, ":");
		if (len > 0 && len < 400) {
			snprintf(full, sizeof full, "%.*s/%s", (int)len, path, program);
			if (access(full, X_OK) == 0) {
				return 1;
			}
		}
		path += len;
		if (*path == ':') {
			path++;
		}
	}
	return 0;
}

/* Turns the Exec line of a desktop entry into a shell command. The value is
 * a desktop-entry string first (\\ is a backslash, \s a space) and field
 * codes such as %U stand for files the launcher was not given. What is left
 * is the command with all its arguments: an entry such as "libreoffice
 * --writer" or Wine's "env WINEPREFIX=... wine C:\\...\\Name.lnk" is not the
 * same program without them. Returns 0 when the program is not installed. */
static int exec_to_command(const char *exec, char *out, size_t n) {
	size_t k = 0;
	for (const char *p = exec; *p != '\0' && k + 2 < n; p++) {
		if (*p == '\\' && p[1] != '\0') {
			p++;
			char c = *p == 's' ? ' ' : *p == 'n' ? '\n' : *p == 't' ? '\t' : *p == 'r' ? '\r' : *p;
			out[k++] = c;
		} else if (*p == '%' && p[1] != '\0') {
			p++;
			if (*p == '%') {
				out[k++] = '%';
			}
		} else {
			out[k++] = *p;
		}
	}
	while (k > 0 && (out[k - 1] == ' ' || out[k - 1] == '\t')) {
		k--;
	}
	out[k] = '\0';
	/* The program is the first word that is not "env" or NAME=value. */
	const char *p = out;
	char word[400];
	for (;;) {
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		if (*p == '\0') {
			return 0;
		}
		size_t w = 0;
		char quote = 0;
		while (*p != '\0' && (quote != 0 || (*p != ' ' && *p != '\t'))) {
			if (quote == 0 && (*p == '"' || *p == '\'')) {
				quote = *p;
			} else if (quote != 0 && *p == quote) {
				quote = 0;
			} else if (*p == '\\' && quote != '\'' && p[1] != '\0') {
				p++;
				if (w + 1 < sizeof word) {
					word[w++] = *p;
				}
			} else if (w + 1 < sizeof word) {
				word[w++] = *p;
			}
			p++;
		}
		word[w] = '\0';
		if (strcmp(word, "env") == 0) {
			continue;
		}
		const char *eq = strchr(word, '=');
		if (eq != NULL && eq != word && strchr(word, '/') > eq) {
			continue;
		}
		if (eq != NULL && eq != word && strchr(word, '/') == NULL) {
			continue;
		}
		return program_exists(word);
	}
}

/* Where an icon named in a desktop entry is on disk. Bigger sizes first:
 * the tile is drawn at 72 pixels and scaled down cleanly. */
static int find_icon_file(const char *icon, char *out, size_t n) {
	if (icon[0] == '\0') {
		return 0;
	}
	if (icon[0] == '/') {
		snprintf(out, n, "%s", icon);
		return access(out, R_OK) == 0;
	}
	char roots[4][400];
	int root_count = 0;
	const char *data = getenv("XDG_DATA_HOME");
	const char *home = getenv("HOME");
	const char *desk = getenv("OLYMPIUM_DESK_HOME");
	if (data != NULL && data[0] != '\0') {
		snprintf(roots[root_count++], sizeof roots[0], "%s/icons", data);
	} else if (home != NULL && home[0] != '\0') {
		snprintf(roots[root_count++], sizeof roots[0], "%s/.local/share/icons", home);
	}
	if (desk != NULL && desk[0] != '\0') {
		snprintf(roots[root_count++], sizeof roots[0], "%s/icons", desk);
	}
	snprintf(roots[root_count++], sizeof roots[0], "/usr/share/icons");
	static const char *const themes[] = {"hicolor", "Adwaita", "Papirus", "breeze", "gnome"};
	static const char *const sizes[] = {
		"256x256", "128x128", "96x96", "72x72", "64x64", "scalable", "512x512", "48x48", "32x32",
	};
	static const char *const exts[] = {"png", "svg"};
	for (int r = 0; r < root_count; r++) {
		for (size_t t = 0; t < sizeof themes / sizeof themes[0]; t++) {
			for (size_t z = 0; z < sizeof sizes / sizeof sizes[0]; z++) {
				for (size_t e = 0; e < 2; e++) {
					snprintf(out, n, "%s/%s/%s/apps/%s.%s", roots[r], themes[t], sizes[z], icon, exts[e]);
					if (access(out, R_OK) == 0) {
						return 1;
					}
				}
			}
		}
	}
	static const char *const flat[] = {"png", "svg", "xpm"};
	for (size_t e = 0; e < 3; e++) {
		snprintf(out, n, "/usr/share/pixmaps/%s.%s", icon, flat[e]);
		if (access(out, R_OK) == 0) {
			return 1;
		}
	}
	/* Some entries name the file with its extension. */
	snprintf(out, n, "/usr/share/pixmaps/%s", icon);
	return strchr(icon, '.') != NULL && access(out, R_OK) == 0;
}

/* Pictures the system's packages installed, and pictures this desktop drew
 * itself from pixels, are read as they are. Anything else is not. */
static int icon_file_is_ours(const char *path) {
	if (strncmp(path, "/usr/", 5) == 0) {
		return 1;
	}
	const char *desk = getenv("OLYMPIUM_DESK_HOME");
	char icons[600];
	if (desk != NULL && desk[0] != '\0') {
		snprintf(icons, sizeof icons, "%s/icons/", desk);
		if (strncmp(path, icons, strlen(icons)) == 0) {
			return 1;
		}
	}
	return 0;
}

/* Loads a program's own icon at 72 pixels: PNG, SVG, XPM, or a Mac .icns.
 * Tried once per entry. NULL leaves the drawn stand-in tile. */
static cairo_surface_t *pad_icon_image(struct pad_app *app) {
	if (app->image != NULL || app->image_tried || app->icon_file[0] == '\0') {
		return app->image;
	}
	app->image_tried = 1;
	const char *file = app->icon_file;
	char drawn[700];
	if (!icon_file_is_ours(file)) {
		/* A picture from somewhere a program can write, such as the icons
		 * Wine makes for Windows shortcuts. The compositor does not decode
		 * it. A confined child does, and what is loaded here is the picture
		 * written from the pixels that child handed back. */
		const char *run = getenv("XDG_RUNTIME_DIR");
		unsigned long key = 5381;
		for (const unsigned char *p = (const unsigned char *)file; *p != '\0'; p++) {
			key = key * 33 + *p;
		}
		struct stat st;
		if (stat(file, &st) != 0) {
			return NULL;
		}
		key ^= (unsigned long)st.st_mtime ^ ((unsigned long)st.st_size << 7);
		char dir[600];
		snprintf(dir, sizeof dir, "%s/olympium-icons", run != NULL && run[0] != '\0' ? run : "/run/dev-desktop");
		mkdir(dir, 0700);
		snprintf(drawn, sizeof drawn, "%s/%08lx.png", dir, key & 0xFFFFFFFFul);
		if (access(drawn, R_OK) != 0 && mac_icon_render(file, drawn) != 0) {
			return NULL;
		}
		file = drawn;
	}
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(file, 72, 72, TRUE, NULL);
	if (pixbuf == NULL) {
		return NULL;
	}
	int w = gdk_pixbuf_get_width(pixbuf);
	int h = gdk_pixbuf_get_height(pixbuf);
	int channels = gdk_pixbuf_get_n_channels(pixbuf);
	int stride = gdk_pixbuf_get_rowstride(pixbuf);
	const guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
	cairo_surface_t *image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 72, 72);
	unsigned char *dst = cairo_image_surface_get_data(image);
	int dst_stride = cairo_image_surface_get_stride(image);
	int off_x = (72 - w) / 2;
	int off_y = (72 - h) / 2;
	for (int y = 0; y < h && y + off_y < 72; y++) {
		uint32_t *row = (uint32_t *)(void *)(dst + (size_t)(y + off_y) * dst_stride);
		for (int x = 0; x < w && x + off_x < 72; x++) {
			const guchar *px = pixels + (size_t)y * stride + (size_t)x * channels;
			uint32_t alpha = channels == 4 ? px[3] : 255;
			/* Cairo wants colour multiplied by alpha. */
			uint32_t r = px[0] * alpha / 255;
			uint32_t g = px[1] * alpha / 255;
			uint32_t b = px[2] * alpha / 255;
			row[x + off_x] = (alpha << 24) | (r << 16) | (g << 8) | b;
		}
	}
	cairo_surface_mark_dirty(image);
	g_object_unref(pixbuf);
	app->image = image;
	return image;
}

static void pad_scan_desktops(struct server *server, const char *dirpath, int depth) {
	DIR *dir = opendir(dirpath);
	if (dir == NULL) {
		return;
	}
	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.') {
			continue;
		}
		char path[768];
		snprintf(path, sizeof path, "%s/%s", dirpath, ent->d_name);
		size_t namelen = strlen(ent->d_name);
		if (namelen < 9 || strcmp(ent->d_name + namelen - 8, ".desktop") != 0) {
			/* Menus are folders of entries. Wine writes one folder per
			 * Windows program group, several levels down. */
			struct stat st;
			if (depth < 6 && stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
				pad_scan_desktops(server, path, depth + 1);
			}
			continue;
		}
		FILE *f = fopen(path, "r");
		if (f == NULL) {
			continue;
		}
		char name[96] = "";
		char exec[700] = "";
		char icon[300] = "";
		char try_exec[300] = "";
		int hidden = 0;
		int in_term = 0;
		int is_app = 0;
		int in_entry = 0;
		char line[1024];
		while (fgets(line, sizeof line, f) != NULL) {
			line[strcspn(line, "\r\n")] = '\0';
			if (line[0] == '[') {
				/* Only the entry itself. Later groups are extra actions
				 * with names and commands of their own. */
				in_entry = strcmp(line, "[Desktop Entry]") == 0;
				continue;
			}
			if (!in_entry) {
				continue;
			}
			if (strcmp(line, "NoDisplay=true") == 0 || strcmp(line, "Hidden=true") == 0) {
				hidden = 1;
			} else if (strcmp(line, "Terminal=true") == 0) {
				in_term = 1;
			} else if (strcmp(line, "Type=Application") == 0) {
				is_app = 1;
			} else if (strncmp(line, "Name=", 5) == 0 && name[0] == '\0') {
				snprintf(name, sizeof name, "%.95s", line + 5);
			} else if (strncmp(line, "Exec=", 5) == 0 && exec[0] == '\0') {
				snprintf(exec, sizeof exec, "%.690s", line + 5);
			} else if (strncmp(line, "Icon=", 5) == 0 && icon[0] == '\0') {
				snprintf(icon, sizeof icon, "%.290s", line + 5);
			} else if (strncmp(line, "TryExec=", 8) == 0) {
				snprintf(try_exec, sizeof try_exec, "%.290s", line + 8);
			}
		}
		fclose(f);
		char command[700];
		if (hidden || !is_app || name[0] == '\0' || exec[0] == '\0' ||
				!exec_to_command(exec, command, sizeof command) ||
				(try_exec[0] != '\0' && !program_exists(try_exec))) {
			continue;
		}
		if (strncmp(name, "Foot", 4) == 0 || strncmp(name, "Python", 6) == 0) {
			continue;
		}
		char cmd[768];
		if (in_term) {
			snprintf(cmd, sizeof cmd, "/usr/bin/foot -e %s", command);
		} else {
			snprintf(cmd, sizeof cmd, "%s", command);
		}
		/* The same entry found twice is listed once. Two programs that
		 * happen to share a name, a Mac application and a Linux one say,
		 * are two programs and are both listed. */
		int already = 0;
		for (int j = 0; j < server->pad_count; j++) {
			if (strcmp(server->pad[j].name, name) == 0 && strcmp(server->pad[j].cmd, cmd) == 0) {
				already = 1;
				break;
			}
		}
		if (already) {
			continue;
		}
		struct pad_app *app = pad_add(server, name, cmd, "installed", "", 0);
		if (app != NULL && !find_icon_file(icon, app->icon_file, sizeof app->icon_file)) {
			app->icon_file[0] = '\0';
		}
	}
	closedir(dir);
}

static int pad_by_name(const void *a, const void *b) {
	return strcasecmp(((const struct pad_app *)a)->name, ((const struct pad_app *)b)->name);
}

static void pad_load(struct server *server) {
	for (int i = 0; i < server->pad_count; i++) {
		if (server->pad[i].image != NULL) {
			cairo_surface_destroy(server->pad[i].image);
			server->pad[i].image = NULL;
		}
	}
	server->pad_count = 0;
	for (int i = 0; i < APP_COUNT; i++) {
		if (strcmp(APPS[i].id, "launchpad") == 0) {
			continue;
		}
		char cmd[240];
		if (APPS[i].arg != NULL) {
			snprintf(cmd, sizeof cmd, "%s %s", APPS[i].path, APPS[i].arg);
		} else {
			snprintf(cmd, sizeof cmd, "%s", APPS[i].path);
		}
		pad_tool(server, APPS[i].label, APPS[i].path, cmd, APPS[i].id, "");
	}
	int core = server->pad_count;
	pad_tool(server, "Activity", "/usr/bin/top", "/usr/bin/foot -T Activity -e top", "activity", "");
	pad_tool(server, "Disks", "/usr/bin/lsblk",
		"/usr/bin/foot -T Disks -e sh -c 'lsblk -o NAME,SIZE,TYPE,MOUNTPOINT; read _'", "disks", "");
	pad_tool(server, "Network", "/usr/bin/ip",
		"/usr/bin/foot -T Network -e sh -c 'ip -brief addr; ip route; read _'", "network", "");
	pad_tool(server, "Devices", "/usr/bin/lspci",
		"/usr/bin/foot -T Devices -e sh -c 'lspci -nn; read _'", "devices", "");
	pad_tool(server, "Drivers", "/usr/bin/driver-report",
		"/usr/bin/foot -T Drivers -e sh -c 'driver-report; read _'", "report", "");
	pad_tool(server, "Color Field", "/usr/bin/dev-swatch", "/usr/bin/dev-swatch", "color", "");
	pad_tool(server, "About", "/usr/bin/dev-settings", "/usr/bin/dev-settings --page=olympium", "about", "");
	pad_tool(server, "Shell", "/usr/bin/bash", "/usr/bin/foot -T Shell -e bash -l", "shell", "");
	pad_tool(server, "Python", "/usr/bin/python3", "/usr/bin/foot -T Python -e python3", "code", "");
	pad_tool(server, "Log", "/usr/bin/journalctl",
		"/usr/bin/foot -T Log -e journalctl -b", "log", "");
	pad_tool(server, "USB", "/usr/bin/lsusb",
		"/usr/bin/foot -T USB -e sh -c 'lsusb; read _'", "usb", "Utilities");
	pad_tool(server, "Services", "/usr/bin/systemctl",
		"/usr/bin/foot -T Services -e sh -c 'systemctl list-units --type=service; read _'",
		"services", "Utilities");
	pad_tool(server, "Driver Install", "/usr/bin/driver-install",
		"/usr/bin/foot -T Drivers -e sh -c 'driver-install --detect; read _'", "install", "Utilities");
	pad_tool(server, "Home", "/usr/bin/dev-files", "/usr/bin/dev-files", "files", "Places");
	pad_tool(server, "Documents", "/usr/bin/dev-files", "/usr/bin/dev-files /usr/share/doc", "files", "Places");
	pad_tool(server, "Applications", "/usr/bin/dev-files", "/usr/bin/dev-files /usr/bin", "files", "Places");
	pad_tool(server, "Computer", "/usr/bin/dev-files", "/usr/bin/dev-files /", "disks", "Places");
	/* Installed desktop entries, each with its whole command line and its
	 * own icon. Terminal programs open inside the terminal. The user's
	 * directories include wine/Programs, where Wine puts an entry for every
	 * shortcut a Windows installer creates. */
	int scanned = server->pad_count;
	pad_scan_desktops(server, "/usr/share/applications", 0);
	pad_scan_desktops(server, "/usr/local/share/applications", 0);
	const char *home = getenv("HOME");
	const char *data_home = getenv("XDG_DATA_HOME");
	char user_apps[512] = "";
	if (data_home != NULL && data_home[0] != '\0') {
		snprintf(user_apps, sizeof user_apps, "%s/applications", data_home);
	} else if (home != NULL && home[0] != '\0') {
		snprintf(user_apps, sizeof user_apps, "%s/.local/share/applications", home);
	} else {
		struct passwd *pw = getpwuid(getuid());
		if (pw != NULL && pw->pw_dir != NULL) {
			snprintf(user_apps, sizeof user_apps, "%s/.local/share/applications", pw->pw_dir);
		}
	}
	if (user_apps[0] != '\0') {
		pad_scan_desktops(server, user_apps, 0);
	}
	const char *desk_home = getenv("OLYMPIUM_DESK_HOME");
	if (desk_home != NULL && desk_home[0] != '\0') {
		char desk_apps[512];
		snprintf(desk_apps, sizeof desk_apps, "%s/applications", desk_home);
		pad_scan_desktops(server, desk_apps, 0);
	}
	if (server->pad_count > scanned) {
		qsort(&server->pad[scanned], (size_t)(server->pad_count - scanned),
			sizeof(server->pad[0]), pad_by_name);
	}
	const char *folders[] = {"Utilities", "Places"};
	for (int k = 0; k < 2; k++) {
		int count = 0;
		for (int j = 0; j < server->pad_count; j++) {
			if (strcmp(server->pad[j].group, folders[k]) == 0) {
				count++;
			}
		}
		if (count > 0 && server->pad_count < PAD_MAX) {
			/* Folders sit right after the core applications, on the first page. */
			pad_add(server, folders[k], "", "folder", folders[k], 1);
			int last = server->pad_count - 1;
			struct pad_app folder = server->pad[last];
			memmove(&server->pad[core + 1], &server->pad[core],
				(size_t)(last - core) * sizeof(server->pad[0]));
			server->pad[core] = folder;
			core++;
		}
	}
}

static int pad_visible(const struct server *server, int index) {
	const struct pad_app *app = &server->pad[index];
	if (server->pad_filter[0] != '\0') {
		return !app->is_folder && strcasestr(app->name, server->pad_filter) != NULL;
	}
	if (server->pad_folder >= 0) {
		return !app->is_folder && strcmp(app->group, server->pad[server->pad_folder].group) == 0;
	}
	if (!app->is_folder && app->group[0] != '\0') {
		for (int i = 0; i < server->pad_count; i++) {
			if (server->pad[i].is_folder && strcmp(server->pad[i].group, app->group) == 0) {
				return 0;
			}
		}
	}
	return 1;
}

static int pad_list(struct server *server, int *out, int max) {
	int n = 0;
	for (int i = 0; i < server->pad_count && n < max; i++) {
		if (pad_visible(server, i)) {
			out[n++] = i;
		}
	}
	return n;
}

/* A centred label with a soft shadow. A name wider than max_w ends in an
 * ellipsis instead of running into the next tile. */
static void pad_label_fit(cairo_t *cr, const char *font, const char *text, double cx, double y,
		double max_w) {
	PangoLayout *layout = pango_cairo_create_layout(cr);
	pango_layout_set_text(layout, text, -1);
	PangoFontDescription *desc = pango_font_description_from_string(font);
	pango_layout_set_font_description(layout, desc);
	if (max_w > 0) {
		pango_layout_set_width(layout, (int)(max_w * PANGO_SCALE));
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
		pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
	}
	int w = 0;
	int h = 0;
	pango_layout_get_pixel_size(layout, &w, &h);
	double x = max_w > 0 ? cx - max_w / 2.0 : cx - w / 2.0;
	cairo_set_source_rgba(cr, 0.08, 0.10, 0.18, 1);
	cairo_move_to(cr, x + 1, y + 1);
	pango_cairo_show_layout(cr, layout);
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, x, y);
	pango_cairo_show_layout(cr, layout);
	pango_font_description_free(desc);
	g_object_unref(layout);
}

static void pad_label(cairo_t *cr, const char *font, const char *text, double cx, double y) {
	pad_label_fit(cr, font, text, cx, y, 0);
}

static void draw_pad(struct server *server, cairo_t *cr) {
	cairo_set_source_rgba(cr, 0.05, 0.07, 0.14, 0.30);
	cairo_paint(cr);
	double search_w = 280;
	double search_x = (server->screen_w - search_w) / 2.0;
	rrect(cr, search_x, 34, search_w, 30, 9);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.24);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.55);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
	cairo_set_line_width(cr, 1.6);
	cairo_arc(cr, search_x + 17, 48, 5, 0, 6.2832);
	cairo_stroke(cr);
	cairo_move_to(cr, search_x + 21, 52);
	cairo_line_to(cr, search_x + 25, 56);
	cairo_stroke(cr);
	const char *query = server->pad_filter[0] != '\0' ? server->pad_filter : "Search";
	paint_text(cr, "Ubuntu 13", query, search_x + 34, 40, 1, 1, 1);
	if (server->pad_folder >= 0) {
		pad_label(cr, "Ubuntu Bold 20", server->pad[server->pad_folder].name,
			server->screen_w / 2.0, 84);
	}
	int ids[PAD_MAX];
	int n = pad_list(server, ids, PAD_MAX);
	int pages = n == 0 ? 1 : (n + PAD_PER - 1) / PAD_PER;
	if (server->pad_page >= pages) {
		server->pad_page = pages - 1;
	}
	if (server->pad_page < 0) {
		server->pad_page = 0;
	}
	int origin_x = (server->screen_w - PAD_COLS * PAD_CELL_W) / 2;
	int start = server->pad_page * PAD_PER;
	for (int i = 0; i < PAD_PER && start + i < n; i++) {
		double x = origin_x + (i % PAD_COLS) * PAD_CELL_W;
		double y = PAD_TOP + (i / PAD_COLS) * PAD_CELL_H;
		double ix = x + (PAD_CELL_W - 72) / 2.0;
		const struct pad_app *app = &server->pad[ids[start + i]];
		if (i == server->pad_cursor) {
			rrect(cr, x + 30, y - 10, PAD_CELL_W - 60, 122, 16);
			cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
			cairo_fill(cr);
		}
		if (app->is_folder) {
			rrect(cr, ix, y, 72, 72, 17);
			cairo_set_source_rgba(cr, 1, 1, 1, 0.30);
			cairo_fill_preserve(cr);
			cairo_set_source_rgba(cr, 1, 1, 1, 0.65);
			cairo_set_line_width(cr, 1.2);
			cairo_stroke(cr);
			int shown = 0;
			for (int j = 0; j < server->pad_count && shown < 4; j++) {
				const struct pad_app *child = &server->pad[j];
				if (child->is_folder || strcmp(child->group, app->group) != 0) {
					continue;
				}
				cairo_save(cr);
				cairo_translate(cr, ix + 10 + (shown % 2) * 28, y + 10 + (shown / 2) * 28);
				cairo_scale(cr, 0.5, 0.5);
				draw_icon(cr, child->icon, 0, 0);
				cairo_restore(cr);
				shown++;
			}
		} else if (pad_icon_image(&server->pad[ids[start + i]]) != NULL) {
			/* The program's own icon, on a soft shadow so it sits on the
			 * blurred backdrop the way the drawn tiles do. */
			cairo_set_source_rgba(cr, 0.05, 0.07, 0.16, 0.16);
			cairo_mask_surface(cr, app->image, ix, y + 2);
			cairo_set_source_surface(cr, app->image, ix, y);
			cairo_paint(cr);
		} else {
			cairo_save(cr);
			cairo_translate(cr, ix, y);
			cairo_scale(cr, 1.5, 1.5);
			draw_icon(cr, app->icon, 0, 0);
			cairo_restore(cr);
		}
		pad_label_fit(cr, "Ubuntu Medium 12", app->name, x + PAD_CELL_W / 2.0, y + 84,
			PAD_CELL_W - 16);
	}
	double dots_y = server->screen_h - DOCK_H - DOCK_MARGIN - 22;
	for (int p = 0; p < pages && p < 8; p++) {
		cairo_set_source_rgba(cr, 1, 1, 1, p == server->pad_page ? 0.95 : 0.40);
		cairo_arc(cr, server->screen_w / 2.0 - (pages - 1) * 9 + p * 18, dots_y, 3.6, 0, 6.2832);
		cairo_fill(cr);
	}
}

static int pad_hit(struct server *server, double x, double y, int *index) {
	int ids[PAD_MAX];
	int n = pad_list(server, ids, PAD_MAX);
	int origin_x = (server->screen_w - PAD_COLS * PAD_CELL_W) / 2;
	int start = server->pad_page * PAD_PER;
	for (int i = 0; i < PAD_PER && start + i < n; i++) {
		double ix = origin_x + (i % PAD_COLS) * PAD_CELL_W + 30;
		double iy = PAD_TOP + (i / PAD_COLS) * PAD_CELL_H - 10;
		if (x >= ix && x < ix + PAD_CELL_W - 60 && y >= iy && y < iy + 122) {
			*index = ids[start + i];
			server->pad_cursor = i;
			return 1;
		}
	}
	return 0;
}

/* A click on a page dot. Returns 1 when the page changed or the dot row was hit. */
static int pad_dot_hit(struct server *server, double x, double y) {
	int ids[PAD_MAX];
	int n = pad_list(server, ids, PAD_MAX);
	int pages = n == 0 ? 1 : (n + PAD_PER - 1) / PAD_PER;
	double dots_y = server->screen_h - DOCK_H - DOCK_MARGIN - 22;
	if (y < dots_y - 12 || y > dots_y + 12) {
		return 0;
	}
	for (int p = 0; p < pages && p < 8; p++) {
		double cx = server->screen_w / 2.0 - (pages - 1) * 9 + p * 18;
		if (x >= cx - 9 && x < cx + 9) {
			server->pad_page = p;
			server->pad_cursor = -1;
			return 1;
		}
	}
	return 0;
}

static void pad_activate(struct server *server, int index) {
	struct pad_app *app = &server->pad[index];
	if (app->is_folder) {
		server->pad_folder = index;
		server->pad_page = 0;
		server->pad_cursor = -1;
		return;
	}
	if (app->cmd[0] == '\0') {
		return;
	}
	if (fork() == 0) {
		setsid();
		/* Ubuntu's Wine 9.0 aborts when TMPDIR is set, and the shortcuts
		 * Wine writes for Windows programs start wine itself. See
		 * wine_environment in install.c. */
		if (strstr(app->cmd, "wine") != NULL) {
			unsetenv("TMPDIR");
		}
		execl("/bin/sh", "sh", "-c", app->cmd, (char *)NULL);
		_exit(127);
	}
	server->launchpad = 0;
	server->pad_filter[0] = '\0';
}

/* Search: the glyph in the menu bar, or Control or Super with Space. It looks
 * through the same list Launchpad shows, so a Linux program, a Windows
 * shortcut and a Mac application are all found by name. Names that start with
 * what was typed come first. It finds programs. It does not look in files. */
static int spot_matches(struct server *server, int *ids, int max) {
	int n = 0;
	if (server->filter[0] == '\0') {
		return 0;
	}
	size_t len = strlen(server->filter);
	for (int pass = 0; pass < 2 && n < max; pass++) {
		for (int i = 0; i < server->pad_count && n < max; i++) {
			const struct pad_app *app = &server->pad[i];
			if (app->is_folder || app->cmd[0] == '\0') {
				continue;
			}
			int starts = strncasecmp(app->name, server->filter, len) == 0;
			if ((pass == 0 && starts) || (pass == 1 && !starts && strcasestr(app->name, server->filter) != NULL)) {
				ids[n++] = i;
			}
		}
	}
	return n;
}

static void open_search(struct server *server) {
	pad_load(server);
	server->launchpad = 0;
	server->open_menu = -1;
	server->filter[0] = '\0';
	server->spot_cursor = 0;
	server->launcher = 1;
}

static void close_search(struct server *server) {
	server->launcher = 0;
	server->filter[0] = '\0';
	server->spot_cursor = 0;
}

/* Starts the selected result, as Return does. */
static void spot_activate(struct server *server) {
	int ids[SPOT_ROWS];
	int n = spot_matches(server, ids, SPOT_ROWS);
	if (n == 0) {
		return;
	}
	int at = server->spot_cursor < n ? server->spot_cursor : 0;
	int index = ids[at];
	wlr_log(WLR_INFO, "search start: %s", server->pad[index].name);
	close_search(server);
	pad_activate(server, index);
}

static void draw_search(struct server *server, cairo_t *cr, const struct wlr_box *box) {
	/* The magnifier, then what was typed, large, the way a search field
	 * that is the whole panel reads. */
	cairo_set_source_rgba(cr, 0.30, 0.31, 0.36, 1);
	cairo_set_line_width(cr, 2.4);
	cairo_arc(cr, 30, 27, 9, 0, 6.2832);
	cairo_stroke(cr);
	cairo_move_to(cr, 37, 34);
	cairo_line_to(cr, 44, 41);
	cairo_stroke(cr);
	if (server->filter[0] != '\0') {
		int w = paint_text(cr, "Ubuntu 20", server->filter, 58, 13, 0.10, 0.10, 0.13);
		cairo_set_source_rgba(cr, 0.20, 0.48, 0.96, 1);
		cairo_rectangle(cr, 58 + w + 2, 15, 2, 28);
		cairo_fill(cr);
	} else {
		paint_text(cr, "Ubuntu 20", "Search", 58, 13, 0.52, 0.53, 0.58);
	}
	int ids[SPOT_ROWS];
	int n = spot_matches(server, ids, SPOT_ROWS);
	if (n == 0) {
		return;
	}
	if (server->spot_cursor >= n) {
		server->spot_cursor = n - 1;
	}
	cairo_set_source_rgba(cr, 0.10, 0.10, 0.14, 0.14);
	cairo_rectangle(cr, 0, SPOT_FIELD_H - 1, box->width, 1);
	cairo_fill(cr);
	for (int i = 0; i < n; i++) {
		struct pad_app *app = &server->pad[ids[i]];
		double y = SPOT_FIELD_H + 6 + i * SPOT_ROW_H;
		int chosen = i == server->spot_cursor;
		if (chosen) {
			rrect(cr, 8, y, box->width - 16, SPOT_ROW_H - 4, 9);
			cairo_set_source_rgba(cr, 0.16, 0.47, 0.96, 0.95);
			cairo_fill(cr);
		}
		cairo_save(cr);
		cairo_translate(cr, 20, y + 5);
		if (pad_icon_image(app) != NULL) {
			cairo_scale(cr, 32.0 / 72.0, 32.0 / 72.0);
			cairo_set_source_surface(cr, app->image, 0, 0);
			cairo_paint(cr);
		} else {
			cairo_scale(cr, 32.0 / 48.0, 32.0 / 48.0);
			draw_icon(cr, app->icon, 0, 0);
		}
		cairo_restore(cr);
		double ink = chosen ? 1.0 : 0.10;
		paint_text(cr, "Ubuntu Medium 14", app->name, 66, y + 10, ink, ink, chosen ? 1.0 : 0.13);
		int tag_w = paint_text(cr, "Ubuntu 11", "Application", 0, -80, 0, 0, 0);
		double tag = chosen ? 0.92 : 0.50;
		paint_text(cr, "Ubuntu 11", "Application", box->width - 22 - tag_w, y + 13, tag, tag, tag + 0.04);
	}
}

static void nag_box(const struct server *server, struct wlr_box *box) {
	box->width = 460;
	box->height = 230;
	box->x = (server->screen_w - box->width) / 2;
	box->y = (server->screen_h - box->height) / 2 - 24;
}

static int license_active(void) {
	FILE *f = fopen("/var/lib/olympium/license", "r");
	if (f == NULL) {
		return 0;
	}
	char buf[128];
	if (fgets(buf, sizeof buf, f) == NULL) {
		fclose(f);
		return 0;
	}
	fclose(f);
	for (char *p = buf; *p != '\0'; p++) {
		if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
			return 1;
		}
	}
	return 0;
}

static time_t read_stamp(const char *path) {
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		return 0;
	}
	long long value = 0;
	if (fscanf(f, "%lld", &value) != 1 || value < 0) {
		value = 0;
	}
	fclose(f);
	return (time_t)value;
}

static int write_stamp(const char *path, time_t value) {
	mkdir("/var/lib/olympium", 0755);
	FILE *f = fopen(path, "w");
	if (f == NULL) {
		return -1;
	}
	fprintf(f, "%lld\n", (long long)value);
	fclose(f);
	return 0;
}

static void nag_dismiss(struct server *server) {
	server->nag = 0;
	server->nag_closed = 1;
	wlr_log(WLR_INFO, "reminder closed");
	write_stamp("/var/lib/olympium/nag-dismissed", time(NULL));
}

static void nag_eval(struct server *server) {
	const long five_days = 5L * 24L * 60L * 60L;
	time_t now = time(NULL);
	if (server->nag_closed || license_active()) {
		server->nag = 0;
		return;
	}
	time_t first = read_stamp("/var/lib/olympium/first-use");
	if (first <= 0) {
		if (write_stamp("/var/lib/olympium/first-use", now) != 0) {
			server->nag = server->nag_force ? 1 : 0;
			return;
		}
		first = now;
	}
	if (server->nag_force) {
		server->nag = 1;
		return;
	}
	if (now - first < five_days) {
		server->nag = 0;
		return;
	}
	time_t dismissed = read_stamp("/var/lib/olympium/nag-dismissed");
	if (dismissed > 0 && now - dismissed < five_days) {
		server->nag = 0;
		return;
	}
	server->nag = 1;
}

static int nag_close_hit(const struct server *server, double x, double y) {
	struct wlr_box box;
	nag_box(server, &box);
	if (x >= box.x + box.width - 36 && x < box.x + box.width - 8 &&
			y >= box.y + 8 && y < box.y + 36) {
		return 1;
	}
	if (x >= box.x + 12 && x < box.x + 29 && y >= box.y + 13 && y < box.y + 31) {
		return 1;
	}
	if (x >= box.x + box.width - 116 && x < box.x + box.width - 16 &&
			y >= box.y + box.height - 58 && y < box.y + box.height - 16) {
		return 1;
	}
	return 0;
}

static void press_chrome(struct server *server) {
	double x = server->cursor->x;
	double y = server->cursor->y;
	/* 7 is MENU_DRAGON, the menu under the dragon mark. */
	server->brand_menu_was_open = server->open_menu == 7;
	server->press_app = -1;
	server->press_contrast = 0;
	server->press_brand = 0;
	server->press_light = 0;
	server->press_view = NULL;
	if (server->nag) {
		struct wlr_box nb;
		nag_box(server, &nb);
		if (nag_close_hit(server, x, y)) {
			nag_dismiss(server);
		} else if (point_in(&nb, x, y)) {
			return;
		}
	}
	if (server->launchpad) {
		int index = -1;
		struct wlr_box dockb;
		dock_box(server, &dockb);
		if (pad_hit(server, x, y, &index)) {
			pad_activate(server, index);
		} else if (pad_dot_hit(server, x, y)) {
			return;
		} else if (point_in(&dockb, x, y)) {
			server->press_app = dock_app_at(server, x, y);
		} else if (y > 70 && server->pad_folder >= 0) {
			server->pad_folder = -1;
			server->pad_page = 0;
		} else if (y > 70) {
			server->launchpad = 0;
			server->pad_filter[0] = '\0';
		}
		return;
	}
	if (server->open_menu >= 0) {
		struct wlr_box drop;
		menu_box(server, &drop);
		if (point_in(&drop, x, y)) {
			int item = (int)((y - drop.y - 6) / 28);
			if (item >= 0 && item < menu_item_count(server->open_menu)) {
				menu_activate(server, server->open_menu, item);
			}
			return;
		}
	}
	if (y >= 0 && y < PANEL_H) {
		if (server->search_x > 0 && x >= server->search_x - 6 && x < server->search_x + 22) {
			if (server->launcher) {
				close_search(server);
			} else {
				open_search(server);
			}
			return;
		}
		if (server->quick_x > 0 && x >= server->quick_x - 6 && x < server->quick_x + 22) {
			close_search(server);
			server->open_menu = server->open_menu == MENU_QUICK ? -1 : MENU_QUICK;
			return;
		}
		for (int i = 0; i < 7; i++) {
			if (x >= server->menu_x[i] && x < server->menu_x[i] + server->menu_w[i]) {
				server->open_menu = server->open_menu == i ? -1 : i;
				return;
			}
		}
	}
	server->open_menu = -1;
	if (server->launcher) {
		struct wlr_box box;
		launcher_box(server, &box);
		if (point_in(&box, x, y)) {
			int ids[SPOT_ROWS];
			int n = spot_matches(server, ids, SPOT_ROWS);
			double top = box.y + SPOT_FIELD_H + 6;
			if (y >= top) {
				int row = (int)((y - top) / SPOT_ROW_H);
				if (row >= 0 && row < n) {
					server->spot_cursor = row;
					spot_activate(server);
				}
			}
			return;
		}
		close_search(server);
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
				if (server->press_view->xdg_toplevel != NULL) {
					wlr_xdg_toplevel_send_close(server->press_view->xdg_toplevel);
				} else if (server->press_view->xwayland_surface != NULL) {
					wlr_xwayland_surface_close(server->press_view->xwayland_surface);
				}
			} else if (server->press_light == 2) {
				wlr_scene_node_set_enabled(&server->press_view->scene_tree->node, false);
			} else if (server->press_light == 3) {
				int width = server->screen_w - 80;
				int height = server->screen_h - PANEL_H - DOCK_H - TITLE_H - 36;
				if (server->press_view->xdg_toplevel != NULL) {
					wlr_xdg_toplevel_set_size(server->press_view->xdg_toplevel, width, height);
				} else if (server->press_view->xwayland_surface != NULL) {
					wlr_xwayland_surface_configure(server->press_view->xwayland_surface, 40,
						PANEL_H + 8, (uint16_t)width, (uint16_t)height);
				}
				wlr_scene_node_set_position(&server->press_view->scene_tree->node,
					40, PANEL_H + 8);
			}
		}
	} else if (server->press_contrast && hit_contrast(server, x, y)) {
		server->contrast = !server->contrast;
		snprintf(server->notice, sizeof server->notice,
			server->contrast ? "Contrast mode" : "Glass mode");
	} else if (server->press_brand && hit_brand(server, x, y)) {
		server->open_menu = server->brand_menu_was_open ? -1 : MENU_DRAGON;
	} else if (server->press_app >= APP_COUNT &&
			server->press_app == dock_app_at(server, x, y)) {
		int pin = server->press_app - APP_COUNT;
		if (pin >= 0 && pin < server->pinned_count) {
			server->launchpad = 0;
			spawn2(server->pinned[pin].path, NULL);
			snprintf(server->notice, sizeof server->notice, "Launched %s",
				server->pinned[pin].name);
		}
	} else if (server->press_app >= 0 && server->press_app == dock_app_at(server, x, y)) {
		struct view *open = NULL;
		struct view *each;
		wl_list_for_each(each, &server->views, link) {
			const char *vt = view_name(each);
			const char *va = each->xdg_toplevel != NULL ? each->xdg_toplevel->app_id : NULL;
			if ((vt != NULL && strcmp(vt, APPS[server->press_app].label) == 0) ||
					(va != NULL && strstr(va, "foot") != NULL &&
					strcmp(APPS[server->press_app].id, "terminal") == 0)) {
				open = each;
				break;
			}
		}
		if (strcmp(APPS[server->press_app].id, "launchpad") == 0) {
			if (server->launchpad) {
				server->launchpad = 0;
			} else {
				open_launchpad(server);
			}
		} else if (open != NULL) {
			/* The application already has a window: bring it back and forward. */
			server->launchpad = 0;
			wlr_scene_node_set_enabled(&open->scene_tree->node, true);
			focus_view(open, open->xdg_toplevel != NULL ? open->xdg_toplevel->base->surface
				: (open->xwayland_surface != NULL ? open->xwayland_surface->surface : NULL));
		} else {
			launch_app(server, &APPS[server->press_app]);
		}
	}
	server->press_app = -1;
	server->press_contrast = 0;
	server->press_brand = 0;
	server->press_light = 0;
	server->press_view = NULL;
}

static void server_cursor_button(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, cursor_button);
	shell_redraw(server);
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
		/* Centre a new window a little above the middle, and step each
		 * later window down and right so title bars stay reachable. */
		struct server *server = view->server;
		struct wlr_box geo;
		wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);
		/* A portrait app can be taller than the space above the Dock.
		 * Fit its initial requested geometry before placing it, keeping
		 * its aspect ratio. Later user resizes are left to the client. */
		int available_w = server->screen_w - 16;
		int available_h = server->screen_h - PANEL_H - TITLE_H - DOCK_H - DOCK_MARGIN - 16;
		if (available_w > 0 && available_h > 0 && geo.width > 0 && geo.height > 0 &&
				(geo.width > available_w || geo.height > available_h)) {
			double scale = fmin((double)available_w / geo.width, (double)available_h / geo.height);
			geo.width = (int)fmax(1, floor(geo.width * scale));
			geo.height = (int)fmax(1, floor(geo.height * scale));
			wlr_xdg_toplevel_set_size(view->xdg_toplevel, geo.width, geo.height);
		}
		int step = (wl_list_length(&server->views) - 1) * 26;
		int free_h = server->screen_h - PANEL_H - DOCK_H - DOCK_MARGIN - TITLE_H - geo.height;
		int x = (server->screen_w - geo.width) / 2 + step;
		int y = PANEL_H + TITLE_H + (free_h > 24 ? free_h / 3 : 8) + step;
		if (x < 8) {
			x = 8;
		}
		if (available_w >= geo.width && x + geo.width > server->screen_w - 8) {
			x = server->screen_w - geo.width - 8;
		}
		int bottom = server->screen_h - DOCK_H - DOCK_MARGIN - 8;
		if (available_h >= geo.height && y + geo.height > bottom) {
			y = bottom - geo.height;
		}
		wlr_scene_node_set_position(&view->scene_tree->node, x, y);
		view->placed = 1;
	}
	wlr_log(WLR_INFO, "window mapped: %s (app %s)",
		view_name(view) != NULL ? view_name(view) : "untitled",
		view->xdg_toplevel->app_id != NULL ? view->xdg_toplevel->app_id : "none");
	focus_view(view, view->xdg_toplevel->base->surface);
}

static void view_forget_input(struct view *view) {
	struct server *server = view->server;
	if (server->press_view == view) {
		server->press_view = NULL;
		server->press_light = 0;
	}
	if (server->grabbed == view) {
		reset_cursor_mode(server);
	}
	shell_redraw(server);
}

static void view_set_title(struct wl_listener *listener, void *data) {
	(void)data;
	struct view *view = wl_container_of(listener, view, set_title);
	shell_redraw(view->server);
}

static void view_unmap(struct wl_listener *listener, void *data) {
	(void)data;
	struct view *view = wl_container_of(listener, view, unmap);
	view_forget_input(view);
	if (!view->is_xwayland || view->mapped) {
		wl_list_remove(&view->link);
	}
	view->mapped = 0;
}

static void view_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct view *view = wl_container_of(listener, view, destroy);
	view_forget_input(view);
	wl_list_remove(&view->set_title.link);
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->commit.link);
	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_fullscreen.link);
	wlr_texture_destroy(view->shadow);
	free(view);
}

static void view_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct view *view = wl_container_of(listener, view, commit);
	if (view->xdg_toplevel->base->initial_commit) {
		/* The shell draws the title bar, shadow, and corners. Telling the
		 * client it is tiled makes toolkits that draw their own frame drop
		 * their shadow margin, so there is one frame, not two. */
		if (wl_resource_get_version(view->xdg_toplevel->resource) >= 2) {
			wlr_xdg_toplevel_set_tiled(view->xdg_toplevel,
				WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
		}
		wlr_xdg_toplevel_set_size(view->xdg_toplevel, 0, 0);
		if (wl_resource_get_version(view->xdg_toplevel->resource) >= 4) {
			int width = view->server->screen_w - 16;
			int height = view->server->screen_h - PANEL_H - TITLE_H - DOCK_H - DOCK_MARGIN - 16;
			if (width > 0 && height > 0) {
				wlr_xdg_toplevel_set_bounds(view->xdg_toplevel, width, height);
			}
		}
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

/* Where a program finds this session's X display when DISPLAY was not in the
 * environment it was started with. The session's startup script, and all it
 * starts, can begin before Xwayland is ready, and DISPLAY is only known once
 * it is. The file holds the display's name, or "none" when this session will
 * not have one. While it is absent the answer is not known yet. dev-install
 * reads it before it starts a Windows program. */
static void x_display_note(const char *value) {
	const char *run = getenv("XDG_RUNTIME_DIR");
	char path[600];
	char tmp[640];
	snprintf(path, sizeof path, "%s/olympium-xdisplay",
		run != NULL && run[0] != '\0' ? run : "/run/dev-desktop");
	if (value == NULL) {
		unlink(path);
		return;
	}
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	FILE *f = fopen(tmp, "w");
	if (f == NULL) {
		return;
	}
	fprintf(f, "%s\n", value);
	fclose(f);
	if (rename(tmp, path) != 0) {
		unlink(tmp);
	}
}

static void launch_startup(struct server *server, const char *why) {
	if (server->startup_sent || server->startup_cmd[0] == '\0') {
		return;
	}
	server->startup_sent = 1;
	wlr_log(WLR_INFO, "session start (%s)", why);
	if (fork() == 0) {
		execl("/bin/sh", "/bin/sh", "-c", server->startup_cmd, (char *)NULL);
		_exit(127);
	}
}

static int session_fallback(void *data) {
	launch_startup(data, "xwayland not ready");
	return 0;
}

static void xwayland_ready(struct wl_listener *listener, void *data) {
	(void)data;
	struct server *server = wl_container_of(listener, server, xwayland_ready);
	if (server->xwayland != NULL && server->xwayland->display_name != NULL) {
		setenv("DISPLAY", server->xwayland->display_name, 1);
		x_display_note(server->xwayland->display_name);
		wlr_log(WLR_INFO, "xwayland on DISPLAY=%s", server->xwayland->display_name);
		server->xwayland_up = 1;
		clip_helper_start(server);
	}
	launch_startup(server, "xwayland ready");
}

static void xwayland_request_configure(struct wl_listener *listener, void *data) {
	struct view *view = wl_container_of(listener, view, request_configure);
	struct wlr_xwayland_surface_configure_event *event = data;
	wlr_xwayland_surface_configure(view->xwayland_surface, event->x, event->y,
		event->width, event->height);
	if (view->scene_tree != NULL) {
		wlr_scene_node_set_position(&view->scene_tree->node, event->x, event->y);
	}
}

static void xwayland_map(struct wl_listener *listener, void *data) {
	(void)data;
	struct view *view = wl_container_of(listener, view, map);
	struct wlr_xwayland_surface *surface = view->xwayland_surface;
	if (surface == NULL || surface->surface == NULL) {
		return;
	}
	if (view->scene_tree == NULL) {
		view->scene_tree = wlr_scene_tree_create(&view->server->scene->tree);
		view->scene_tree->node.data = view;
		wlr_scene_surface_create(view->scene_tree, surface->surface);
	}
	if (!view->mapped) {
		wl_list_insert(&view->server->views, &view->link);
		view->mapped = 1;
	}
	int width = surface->width > 0 ? surface->width : 480;
	int height = surface->height > 0 ? surface->height : 280;
	int x = surface->x;
	int y = surface->y;
	if (!surface->override_redirect) {
		int step = (wl_list_length(&view->server->views) - 1) * 26;
		x = (view->server->screen_w - width) / 2 + step;
		y = PANEL_H + TITLE_H + 48 + step;
		if (x < 8) {
			x = 8;
		}
		wlr_xwayland_surface_configure(surface, (int16_t)x, (int16_t)y,
			(uint16_t)width, (uint16_t)height);
	}
	wlr_scene_node_set_position(&view->scene_tree->node, x, y);
	wlr_log(WLR_INFO, "window mapped: %s (app %s)",
		surface->title != NULL && surface->title[0] != '\0' ? surface->title : "Windows",
		surface->class != NULL ? surface->class : "wine");
	focus_view(view, surface->surface);
}

static void xwayland_associate(struct wl_listener *listener, void *data) {
	(void)data;
	struct view *view = wl_container_of(listener, view, associate);
	struct wlr_xwayland_surface *surface = view->xwayland_surface;
	if (surface == NULL || surface->surface == NULL || view->bound) {
		return;
	}
	view->bound = 1;
	view->map.notify = xwayland_map;
	wl_signal_add(&surface->surface->events.map, &view->map);
	view->unmap.notify = view_unmap;
	wl_signal_add(&surface->surface->events.unmap, &view->unmap);
	if (surface->surface->mapped) {
		xwayland_map(&view->map, NULL);
	}
}

/* An X window loses its Wayland surface when it is withdrawn, and the surface
 * is freed soon after, while the X window itself can live on and be shown
 * again with a new surface. The two listeners on the old surface have to come
 * off here. Left on, they stay linked into freed memory, and taking them off
 * later, when the X window is destroyed, writes into whatever that memory has
 * become. wlroots has already unmapped the surface when it sends this. */
static void xwayland_dissociate(struct wl_listener *listener, void *data) {
	(void)data;
	struct view *view = wl_container_of(listener, view, dissociate);
	view_forget_input(view);
	if (!view->bound) {
		return;
	}
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	view->bound = 0;
	if (view->mapped) {
		wl_list_remove(&view->link);
		view->mapped = 0;
	}
	/* The scene node showed the surface that is going away. A window that is
	 * shown again gets a new one for its new surface. */
	if (view->scene_tree != NULL) {
		wlr_scene_node_destroy(&view->scene_tree->node);
		view->scene_tree = NULL;
	}
}

static void xwayland_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct view *view = wl_container_of(listener, view, destroy);
	view_forget_input(view);
	if (view->mapped) {
		wl_list_remove(&view->link);
		view->mapped = 0;
	}
	if (view->bound) {
		wl_list_remove(&view->map.link);
		wl_list_remove(&view->unmap.link);
	}
	wl_list_remove(&view->set_title.link);
	wl_list_remove(&view->associate.link);
	wl_list_remove(&view->dissociate.link);
	wl_list_remove(&view->request_configure.link);
	wl_list_remove(&view->destroy.link);
	if (view->scene_tree != NULL) {
		wlr_scene_node_destroy(&view->scene_tree->node);
	}
	wlr_texture_destroy(view->shadow);
	free(view);
}

static void server_new_xwayland_surface(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, new_xwayland_surface);
	struct wlr_xwayland_surface *surface = data;
	struct view *view = calloc(1, sizeof(*view));
	view->server = server;
	view->is_xwayland = 1;
	view->xwayland_surface = surface;
	view->set_title.notify = view_set_title;
	wl_signal_add(&surface->events.set_title, &view->set_title);
	surface->data = view;
	view->associate.notify = xwayland_associate;
	wl_signal_add(&surface->events.associate, &view->associate);
	view->dissociate.notify = xwayland_dissociate;
	wl_signal_add(&surface->events.dissociate, &view->dissociate);
	view->request_configure.notify = xwayland_request_configure;
	wl_signal_add(&surface->events.request_configure, &view->request_configure);
	view->destroy.notify = xwayland_destroy;
	wl_signal_add(&surface->events.destroy, &view->destroy);
	if (surface->surface != NULL) {
		xwayland_associate(&view->associate, NULL);
	}
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
	view->set_title.notify = view_set_title;
	wl_signal_add(&xdg_surface->toplevel->events.set_title, &view->set_title);
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
	time_t now = time(NULL);
	/* These values change on wall-clock time, never on rendered-frame count.
	 * Poll the legacy file control at 5 Hz without repainting an idle screen. */
	if (now / 10 != server->shell_stamp / 10) {
		int kind = server->net_kind, level = server->net_level;
		probe_net(server);
		widgets_refresh(server, 0);
		if (now / 60 != server->shell_stamp / 60 ||
				kind != server->net_kind || level != server->net_level) {
			shell_redraw(server);
		}
	}
	server->shell_stamp = now;
	if (access("/run/dev-desktop/open-launchpad", F_OK) == 0 ||
			(server->shot_path[0] != '\0' && !server->shot_written) ||
			(server->demo_menu && !server->demo_menu_done) ||
			(server->demo_pad && !server->demo_pad_done) ||
			(server->nag_demo_dismiss && server->nag)) {
		shell_redraw(server);
	}
	write_status(server);
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
	char buf[256];
	ssize_t n = read(client, buf, sizeof buf - 1);
	if (n > 0) {
		buf[n] = '\0';
		if (strncmp(buf, "contrast toggle", 15) == 0) {
			server->contrast = !server->contrast;
		} else if (strncmp(buf, "contrast 1", 10) == 0) {
			server->contrast = 1;
		} else if (strncmp(buf, "contrast 0", 10) == 0) {
			server->contrast = 0;
		} else if (strncmp(buf, "menu open ", 10) == 0) {
			int menu = atoi(buf + 10);
			server->open_menu = menu >= 0 && menu < MENU_COUNT ? menu : -1;
		} else if (strncmp(buf, "menu pick ", 10) == 0 && server->open_menu >= 0) {
			/* What a click on that line of the open menu does. */
			int item = atoi(buf + 10);
			if (item >= 0 && item < menu_item_count(server->open_menu)) {
				menu_activate(server, server->open_menu, item);
			}
		} else if (strncmp(buf, "search open", 11) == 0) {
			open_search(server);
		} else if (strncmp(buf, "search close", 12) == 0) {
			close_search(server);
		} else if (strncmp(buf, "search type ", 12) == 0 && server->launcher) {
			/* What typing these letters into the panel would find. */
			char *text = buf + 12;
			text[strcspn(text, "\r\n")] = '\0';
			snprintf(server->filter, sizeof server->filter, "%s", text);
			server->spot_cursor = 0;
			int ids[SPOT_ROWS];
			int found = spot_matches(server, ids, SPOT_ROWS);
			for (int i = 0; i < found; i++) {
				wlr_log(WLR_INFO, "search result %d: %s", i + 1, server->pad[ids[i]].name);
			}
			wlr_log(WLR_INFO, "search for \"%s\": %d result%s", server->filter, found, found == 1 ? "" : "s");
		} else if (strncmp(buf, "search down", 11) == 0 && server->launcher) {
			if (server->spot_cursor < SPOT_ROWS - 1) {
				server->spot_cursor++;
			}
		} else if (strncmp(buf, "search enter", 12) == 0 && server->launcher) {
			spot_activate(server);
		} else if (strncmp(buf, "launchpad list", 14) == 0) {
			/* What Launchpad would show now, without opening it. */
			pad_load(server);
			for (int i = 0; i < server->pad_count; i++) {
				if (!server->pad[i].is_folder) {
					wlr_log(WLR_INFO, "launchpad has: %s [%s] icon %s", server->pad[i].name,
						server->pad[i].icon,
						server->pad[i].icon_file[0] != '\0' ? server->pad[i].icon_file : "drawn");
				}
			}
			wlr_log(WLR_INFO, "launchpad list done: %d entries", server->pad_count);
		} else if (strncmp(buf, "launchpad start ", 16) == 0) {
			/* Start an entry by name, exactly as a click on its tile does. */
			char *name = buf + 16;
			name[strcspn(name, "\r\n")] = '\0';
			pad_load(server);
			int found = 0;
			for (int i = 0; i < server->pad_count; i++) {
				if (!server->pad[i].is_folder && strcmp(server->pad[i].name, name) == 0) {
					wlr_log(WLR_INFO, "launchpad start: %s", name);
					pad_activate(server, i);
					found = 1;
					break;
				}
			}
			if (!found) {
				wlr_log(WLR_INFO, "launchpad start: no entry named %s", name);
			}
		}
		shell_redraw(server);
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
	fprintf(stderr, "Usage: %s [-n] [-s startup-command]\n", argv0);
}

int main(int argc, char *argv[]) {
	wlr_log_init(getenv("DEV_DESK_DEBUG") != NULL ? WLR_DEBUG : WLR_INFO, NULL);
	char *startup_cmd = NULL;
	int native_startup = 0;
	int opt;
	while ((opt = getopt(argc, argv, "s:nh")) != -1) {
		switch (opt) {
		case 'n':
			native_startup = 1;
			break;
		case 's':
			startup_cmd = optarg;
			break;
		default:
			usage(argv[0]);
			return opt == 'h' ? 0 : 1;
		}
	}
	/* Leave SIGCHLD alone. Ignoring it auto-reaps Xwayland, and wlroots
	 * then treats the server as dead. */

	struct server server = {0};
	server.press_app = -1;
	server.hover_app = -1;
	server.open_menu = -1;
	server.pad_folder = -1;
	/* Demo flags come from the kernel command line. OLYMPIUM_DEMO adds the
	 * same flags for a headless preview on a build machine. */
	char flags[1024] = "";
	FILE *cmd = fopen("/proc/cmdline", "r");
	if (cmd != NULL) {
		if (fgets(flags, 512, cmd) == NULL) {
			flags[0] = '\0';
		}
		fclose(cmd);
	}
	const char *demo = getenv("OLYMPIUM_DEMO");
	if (demo != NULL) {
		size_t used = strlen(flags);
		snprintf(flags + used, sizeof flags - used, " %s", demo);
	}
	if (strstr(flags, "olympium.openmenu=1") != NULL) {
		server.demo_menu = 1;
	}
	if (strstr(flags, "olympium.openlaunch=1") != NULL) {
		server.demo_pad = 1;
	}
	if (strstr(flags, "olympium.nag=1") != NULL) {
		server.nag_force = 1;
	}
	if (strstr(flags, "olympium.nagdismiss=1") != NULL) {
		server.nag_demo_dismiss = 1;
	}
	nag_eval(&server);
	load_branding(&server);
	server.widgets_on = widgets_enabled() && strstr(flags, "olympium.widgets=0") == NULL;
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
	server.compositor = wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);
	wlr_viewporter_create(server.wl_display);
	wlr_screencopy_manager_v1_create(server.wl_display);

	server.output_layout = wlr_output_layout_create();
	wlr_xdg_output_manager_v1_create(server.wl_display, server.output_layout);
	wl_list_init(&server.outputs);
	server.new_output.notify = server_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	/* The shell draws its glass into the frame after the scene. If the scene
	 * handed the wallpaper buffer straight to the display, that drawing would
	 * land in the wallpaper itself and build up frame after frame. */
	setenv("WLR_SCENE_DISABLE_DIRECT_SCANOUT", "1", 1);
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
#ifdef OLYMPIUM_TEST_INPUT
	if (getenv("DEV_DESK_HEADLESS") != NULL) {
		struct wlr_virtual_pointer_manager_v1 *manager =
			wlr_virtual_pointer_manager_v1_create(server.wl_display);
		server.new_virtual_pointer.notify = server_new_virtual_pointer;
		wl_signal_add(&manager->events.new_virtual_pointer, &server.new_virtual_pointer);
	}
#endif
	server.request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);
	server.request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server.seat->events.request_set_selection, &server.request_set_selection);
	{
		const char *cache = getenv("OLYMPIUM_BLUR_CACHE");
		server.blur_cache_off = cache != NULL && strcmp(cache, "0") == 0;
		server.blur_cache_verify = cache != NULL && strcmp(cache, "verify") == 0;
	}
	server.clip_fd = -1;
	server.clip_helper_fd = -1;
	server.x_clip_state = '?';
	if (getenv("OLYMPIUM_CLIP_KEEP") == NULL || strcmp(getenv("OLYMPIUM_CLIP_KEEP"), "0") != 0) {
		server.clip_timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server.wl_display), clip_settled, &server);
		server.selection_changed.notify = seat_selection_changed;
		wl_signal_add(&server.seat->events.set_selection, &server.selection_changed);
	}
	if (wlr_renderer_is_pixman(server.renderer)) {
		setenv("OLYMPIUM_MAC_SOFTWARE", "1", 0);
		/* X11 shared-memory IDs do not cross Darling's IPC namespace. Keep
		 * software rendering on the normal X11 socket transport instead. */
		const char *helper = getenv("OLYMPIUM_XWAYLAND_SOFTWARE");
		if (helper == NULL) helper = "/usr/libexec/dev-desktop/Xwayland-software";
		if (getenv("WLR_XWAYLAND") == NULL && access(helper, X_OK) == 0) {
			setenv("WLR_XWAYLAND", helper, 1);
		}
	}
	server.xwayland = wlr_xwayland_create(server.wl_display, server.compositor, false);
	if (server.xwayland != NULL) {
		wlr_xwayland_set_seat(server.xwayland, server.seat);
		server.new_xwayland_surface.notify = server_new_xwayland_surface;
		wl_signal_add(&server.xwayland->events.new_surface, &server.new_xwayland_surface);
		server.xwayland_ready.notify = xwayland_ready;
		wl_signal_add(&server.xwayland->events.ready, &server.xwayland_ready);
		/* Not known yet. A note left by an earlier session is not this one's. */
		x_display_note(NULL);
	} else {
		wlr_log(WLR_ERROR, "xwayland was not created");
		x_display_note("none");
	}

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
	if (getenv("DEV_DESK_HEADLESS") != NULL && wl_list_empty(&server.keyboards)) {
		/* A screenless test run has no input devices. Clients are only
		 * allowed to set the clipboard with a serial from an input event,
		 * so the test session gets a keyboard that never presses a key. */
		static struct wlr_keyboard test_keyboard;
		wlr_keyboard_init(&test_keyboard, NULL, "headless-keyboard");
		server_new_keyboard(&server, &test_keyboard.base);
		wlr_seat_set_capabilities(server.seat,
			WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
	}

	setenv("WAYLAND_DISPLAY", socket, 1);
	setup_control(&server);
	struct wl_event_loop *loop = wl_display_get_event_loop(server.wl_display);
	server.tick_timer = wl_event_loop_add_timer(loop, tick_frame, &server);
	wl_event_source_timer_update(server.tick_timer, 200);
	write_status(&server);

	if (startup_cmd != NULL) {
		snprintf(server.startup_cmd, sizeof server.startup_cmd, "%s", startup_cmd);
		if (native_startup) {
			/* Native Wayland clients can connect now. Windows launchers use
			 * olympium-xdisplay when Xwayland finishes starting later. */
			launch_startup(&server, "wayland ready");
		} else if (server.xwayland == NULL) {
			launch_startup(&server, "no xwayland");
		} else {
			struct wl_event_loop *startup_loop = wl_display_get_event_loop(server.wl_display);
			struct wl_event_source *fallback = wl_event_loop_add_timer(startup_loop, session_fallback, &server);
			wl_event_source_timer_update(fallback, 4000);
		}
	}
	wlr_log(WLR_INFO, "compositor on WAYLAND_DISPLAY=%s title=%s", socket, server.title);
	wl_display_run(server.wl_display);
	x_display_note(NULL);
	wl_display_destroy_clients(server.wl_display);
	wl_display_destroy(server.wl_display);
	return server.shot_path[0] != '\0' && !server.shot_written ? 2 : 0;
}
