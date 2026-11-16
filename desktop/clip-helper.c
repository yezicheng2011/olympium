/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * Watches the X clipboard for the compositor. X programs here are Wine
 * windows and anything else under Xwayland.
 *
 * It does two things the compositor cannot do itself, because the
 * compositor must never wait on the X server and wlroots does not expose
 * the state of its clipboard bridge.
 *
 * 1. It reports who owns the X clipboard, one character per change on
 *    standard output:
 *      0  nobody
 *      x  an X program
 *      b  the bridge, on behalf of a Wayland program or of the desktop
 *      s  an X program owned it for a moment and the bridge took it right
 *         back: the bridge may now hold a stale answer from that program
 *    The compositor only offers kept text while the answer is 0. Offering
 *    while an X program has just taken the clipboard takes it away again,
 *    and the bridge then asks itself for the data without end.
 *
 * 2. It nudges an X program that has just taken the clipboard. The bridge
 *    asks the new owner what it holds in the same instant the owner
 *    changes. Wine is often still inside the call that took ownership, reads
 *    that question into its queue, and then sleeps on its connection with
 *    the question unanswered until something else arrives. A second request
 *    a moment later is that something else. Without it about one copy in
 *    ten from a Windows program never reached the desktop.
 *
 * It reads no clipboard contents. It asks only for the list of formats and
 * throws the answer away.
 */
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>

static xcb_connection_t *g_conn;
static xcb_window_t g_root;
static xcb_window_t g_window;
static xcb_atom_t g_clipboard;
static xcb_atom_t g_targets;
static xcb_atom_t g_property;
static xcb_atom_t g_wm_check;
static uint32_t g_id_mask;

static long now_ms(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static xcb_atom_t atom(const char *name) {
	xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(g_conn,
		xcb_intern_atom(g_conn, 0, (uint16_t)strlen(name), name), NULL);
	xcb_atom_t value = reply != NULL ? reply->atom : XCB_ATOM_NONE;
	free(reply);
	return value;
}

static void say(char state) {
	char line[2] = {state, '\n'};
	if (write(1, line, 2) != 2) {
		/* The compositor is gone. */
		_exit(0);
	}
}

/* The window manager under Xwayland is the bridge. Every window a client
 * creates carries that client's id in its high bits, so the owner is the
 * bridge when it shares those bits with the window manager's own window. */
static int owned_by_bridge(xcb_window_t owner) {
	xcb_get_property_reply_t *reply = xcb_get_property_reply(g_conn,
		xcb_get_property(g_conn, 0, g_root, g_wm_check, XCB_ATOM_WINDOW, 0, 1), NULL);
	int bridge = 0;
	if (reply != NULL && xcb_get_property_value_length(reply) >= 4) {
		xcb_window_t wm = *(xcb_window_t *)xcb_get_property_value(reply);
		bridge = wm != XCB_WINDOW_NONE && (wm & ~g_id_mask) == (owner & ~g_id_mask);
	}
	free(reply);
	return bridge;
}

static char state_of(xcb_window_t owner) {
	if (owner == XCB_WINDOW_NONE) {
		return '0';
	}
	if ((owner & ~g_id_mask) == (g_window & ~g_id_mask)) {
		return '0';
	}
	return owned_by_bridge(owner) ? 'b' : 'x';
}

static void nudge(void) {
	xcb_convert_selection(g_conn, g_window, g_clipboard, g_targets, g_property,
		XCB_CURRENT_TIME);
	xcb_flush(g_conn);
}

int main(void) {
	prctl(PR_SET_PDEATHSIG, SIGTERM);
	signal(SIGPIPE, SIG_IGN);
	int screen_number = 0;
	g_conn = xcb_connect(NULL, &screen_number);
	if (g_conn == NULL || xcb_connection_has_error(g_conn)) {
		fprintf(stderr, "dev-clip-helper: no X display\n");
		return 1;
	}
	const xcb_setup_t *setup = xcb_get_setup(g_conn);
	g_id_mask = setup->resource_id_mask;
	xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
	for (int i = 0; i < screen_number && it.rem > 0; i++) {
		xcb_screen_next(&it);
	}
	if (it.rem == 0) {
		return 1;
	}
	g_root = it.data->root;
	g_clipboard = atom("CLIPBOARD");
	g_targets = atom("TARGETS");
	g_property = atom("_OLYMPIUM_CLIP_NUDGE");
	g_wm_check = atom("_NET_SUPPORTING_WM_CHECK");
	g_window = xcb_generate_id(g_conn);
	xcb_create_window(g_conn, XCB_COPY_FROM_PARENT, g_window, g_root, -10, -10, 1, 1, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, it.data->root_visual, 0, NULL);
	const xcb_query_extension_reply_t *xfixes = xcb_get_extension_data(g_conn, &xcb_xfixes_id);
	if (xfixes == NULL || !xfixes->present) {
		fprintf(stderr, "dev-clip-helper: the X server has no XFixes\n");
		return 1;
	}
	free(xcb_xfixes_query_version_reply(g_conn,
		xcb_xfixes_query_version(g_conn, 5, 0), NULL));
	xcb_xfixes_select_selection_input(g_conn, g_window, g_clipboard,
		XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE);
	xcb_flush(g_conn);

	xcb_get_selection_owner_reply_t *first = xcb_get_selection_owner_reply(g_conn,
		xcb_get_selection_owner(g_conn, g_clipboard), NULL);
	char state = state_of(first != NULL ? first->owner : XCB_WINDOW_NONE);
	free(first);
	say(state);

	long claimed_at = 0;
	long nudge_at[2] = {0, 0};
	if (state == 'x') {
		nudge_at[0] = now_ms() + 120;
		nudge_at[1] = now_ms() + 500;
	}
	struct pollfd pfd = {.fd = xcb_get_file_descriptor(g_conn), .events = POLLIN};
	for (;;) {
		int wait = -1;
		long now = now_ms();
		for (int i = 0; i < 2; i++) {
			if (nudge_at[i] != 0) {
				long left = nudge_at[i] - now;
				if (left <= 0) {
					nudge_at[i] = 0;
					nudge();
					wait = 0;
				} else if (wait < 0 || left < wait) {
					wait = (int)left;
				}
			}
		}
		if (poll(&pfd, 1, wait) < 0) {
			continue;
		}
		xcb_generic_event_t *event;
		while ((event = xcb_poll_for_event(g_conn)) != NULL) {
			uint8_t type = event->response_type & 0x7f;
			if (type == xfixes->first_event + XCB_XFIXES_SELECTION_NOTIFY) {
				xcb_xfixes_selection_notify_event_t *change =
					(xcb_xfixes_selection_notify_event_t *)event;
				char next = state_of(change->owner);
				now = now_ms();
				if (next == 'x') {
					claimed_at = now;
					nudge_at[0] = now + 120;
					nudge_at[1] = now + 500;
				} else {
					nudge_at[0] = 0;
					nudge_at[1] = 0;
				}
				if (next == 'b' && state == 'x' && now - claimed_at < 60) {
					say('s');
				}
				state = next;
				say(state);
			} else if (type == XCB_SELECTION_NOTIFY) {
				/* The answer to a nudge. Nothing in it is used. */
				xcb_delete_property(g_conn, g_window, g_property);
				xcb_flush(g_conn);
			}
			free(event);
		}
		if (xcb_connection_has_error(g_conn)) {
			return 0;
		}
	}
}
