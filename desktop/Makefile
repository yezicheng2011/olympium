CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter
WLROOTS_FLAGS := $(shell pkg-config --cflags --libs wlroots) -DWLR_USE_UNSTABLE -Iprotocol
WAYLAND_FLAGS := $(shell pkg-config --cflags --libs wayland-server)
CAIRO_FLAGS := $(shell pkg-config --cflags --libs cairo pango pangocairo)
XKB_FLAGS := $(shell pkg-config --cflags --libs xkbcommon)
DRM_FLAGS := $(shell pkg-config --cflags --libs libdrm pixman-1)
GTK_FLAGS := $(shell pkg-config --cflags --libs gtk+-3.0)

.PHONY: all test clean

all: dev-compositor dev-files dev-settings dev-swatch dev-compat test-blur test-macho splash.png

splash.png: splash.c
	$(CC) $(CFLAGS) -o splash-gen splash.c $(CAIRO_FLAGS)
	./splash-gen splash.png

dev-compositor: compositor.c blur.c blur.h
	$(CC) $(CFLAGS) -o $@ compositor.c blur.c $(WLROOTS_FLAGS) $(WAYLAND_FLAGS) $(CAIRO_FLAGS) $(XKB_FLAGS) $(DRM_FLAGS) -lm

dev-files: files.c
	$(CC) $(CFLAGS) -o $@ files.c $(GTK_FLAGS)

dev-settings: settings.c
	$(CC) $(CFLAGS) -o $@ settings.c $(GTK_FLAGS)

dev-swatch: swatch.c
	$(CC) $(CFLAGS) -o $@ swatch.c $(GTK_FLAGS)

dev-compat: compat.c macho.c macho.h
	$(CC) $(CFLAGS) -o $@ compat.c macho.c

test-blur: test_blur.c blur.c blur.h
	$(CC) $(CFLAGS) -o $@ test_blur.c blur.c

test-macho: test_macho.c macho.c macho.h
	$(CC) $(CFLAGS) -o $@ test_macho.c macho.c

test: test-blur test-macho
	./test-blur
	./test-macho

clean:
	rm -f dev-compositor dev-files dev-settings dev-swatch dev-compat test-blur test-macho splash-gen splash.png
