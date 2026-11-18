/* Test-only Wayland pointer. Uses the compositor's normal pointer event path. */
#include <errno.h>
#include <linux/input-event-codes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "virtual-pointer-client.h"

static struct zwlr_virtual_pointer_manager_v1 *manager;
static void global(void *data, struct wl_registry *registry, uint32_t name,
        const char *interface, uint32_t version) {
    (void)data; (void)version;
    if (!strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name))
        manager = wl_registry_bind(registry, name,
            &zwlr_virtual_pointer_manager_v1_interface, 1);
}
static void removed(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}
static uint32_t now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000u + ts.tv_nsec / 1000000u;
}
static unsigned number(const char *arg) {
    char *end;
    errno = 0;
    unsigned long value = strtoul(arg, &end, 10);
    if (errno || !*arg || *end || value > 100000) {
        fprintf(stderr, "Invalid coordinate: %s\n", arg);
        exit(2);
    }
    return value;
}
int main(int argc, char **argv) {
    int drag = argc > 1 && !strcmp(argv[1], "--drag");
    int first = 3 + drag;
    if (argc - first < (drag ? 4 : 2) || (argc - first) % 2) {
        fprintf(stderr, "usage: %s [--drag] WIDTH HEIGHT X Y [X Y ...]\n", argv[0]);
        return 2;
    }
    unsigned width = number(argv[1 + drag]), height = number(argv[2 + drag]);
    if (!width || !height) return 2;
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) { perror("wl_display_connect"); return 1; }
    struct wl_registry *registry = wl_display_get_registry(display);
    const struct wl_registry_listener listener = {global, removed};
    wl_registry_add_listener(registry, &listener, NULL);
    if (wl_display_roundtrip(display) < 0 || !manager) {
        fprintf(stderr, "Test input protocol unavailable\n");
        return 1;
    }
    struct zwlr_virtual_pointer_v1 *pointer =
        zwlr_virtual_pointer_manager_v1_create_virtual_pointer(manager, NULL);
    for (int i = first; i < argc; i += 2) {
        unsigned x = number(argv[i]), y = number(argv[i+1]);
        if (x >= width || y >= height) return 2;
        zwlr_virtual_pointer_v1_motion_absolute(pointer, now(), x, y, width, height);
        zwlr_virtual_pointer_v1_frame(pointer);
        if (wl_display_roundtrip(display) < 0) return 1;
        usleep(150000);
        if (!drag || i == first) {
            zwlr_virtual_pointer_v1_button(pointer, now(), BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
            zwlr_virtual_pointer_v1_frame(pointer);
            if (wl_display_roundtrip(display) < 0) return 1;
            usleep(150000);
        }
        if (drag && i + 2 < argc) continue;
        zwlr_virtual_pointer_v1_button(pointer, now(), BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
        zwlr_virtual_pointer_v1_frame(pointer);
        if (wl_display_roundtrip(display) < 0) return 1;
        usleep(500000);
    }
    zwlr_virtual_pointer_v1_destroy(pointer);
    zwlr_virtual_pointer_manager_v1_destroy(manager);
    wl_registry_destroy(registry);
    wl_display_flush(display);
    wl_display_disconnect(display);
    return 0;
}
