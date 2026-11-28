/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * The compositor shows X programs, which is what a Wine window is, through
 * Xwayland. A wlroots that was built without Xwayland has no such header.
 * This file lets the same compositor source build against either one. With
 * the stand-ins below no X server is started, wlr_xwayland_create returns
 * NULL, and the session starts without DISPLAY. Nothing here draws a window.
 */
#ifndef XWAYLAND_COMPAT_H
#define XWAYLAND_COMPAT_H

#include <wlr/config.h>

#if WLR_HAS_XWAYLAND && !defined(DEV_DESK_NO_XWAYLAND)
#include <wlr/xwayland/xwayland.h>
#define DEV_DESK_XWAYLAND 1
#else
#define DEV_DESK_XWAYLAND 0
#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>

struct wlr_surface;
struct wlr_seat;
struct wlr_compositor;

struct wlr_xwayland_surface_configure_event {
	int16_t x, y;
	uint16_t width, height;
};

struct wlr_xwayland_surface {
	struct wlr_surface *surface;
	int16_t x, y;
	uint16_t width, height;
	bool override_redirect;
	char *title;
	char *class;
	void *data;
	struct {
		struct wl_signal associate;
		struct wl_signal dissociate;
		struct wl_signal request_configure;
		struct wl_signal set_title;
		struct wl_signal destroy;
	} events;
};

struct wlr_xwayland {
	const char *display_name;
	struct {
		struct wl_signal new_surface;
		struct wl_signal ready;
	} events;
};

static inline struct wlr_xwayland *wlr_xwayland_create(struct wl_display *display,
		struct wlr_compositor *compositor, bool lazy) {
	(void)display;
	(void)compositor;
	(void)lazy;
	return NULL;
}
static inline void wlr_xwayland_set_seat(struct wlr_xwayland *xwayland, struct wlr_seat *seat) {
	(void)xwayland;
	(void)seat;
}
static inline struct wlr_xwayland_surface *wlr_xwayland_surface_try_from_wlr_surface(
		struct wlr_surface *surface) {
	(void)surface;
	return NULL;
}
static inline void wlr_xwayland_surface_activate(struct wlr_xwayland_surface *surface, bool on) {
	(void)surface;
	(void)on;
}
static inline void wlr_xwayland_surface_close(struct wlr_xwayland_surface *surface) {
	(void)surface;
}
static inline void wlr_xwayland_surface_configure(struct wlr_xwayland_surface *surface,
		int16_t x, int16_t y, uint16_t width, uint16_t height) {
	(void)surface;
	(void)x;
	(void)y;
	(void)width;
	(void)height;
}
#endif

#endif
