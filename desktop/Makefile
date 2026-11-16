CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter
WLROOTS_FLAGS := $(shell pkg-config --cflags --libs wlroots) -DWLR_USE_UNSTABLE -Iprotocol
WAYLAND_FLAGS := $(shell pkg-config --cflags --libs wayland-server)
CAIRO_FLAGS := $(shell pkg-config --cflags --libs cairo pango pangocairo)
# Launchpad shows each program's own icon: PNG, SVG, XPM, or a Mac .icns.
PIXBUF_FLAGS := $(shell pkg-config --cflags --libs gdk-pixbuf-2.0)
XKB_FLAGS := $(shell pkg-config --cflags --libs xkbcommon)
DRM_FLAGS := $(shell pkg-config --cflags --libs libdrm pixman-1)
GTK_FLAGS := $(shell pkg-config --cflags --libs gtk+-3.0)

# dev-clip-helper watches the X clipboard for the compositor. It is only
# built where Xwayland can be: it needs xcb and xcb-xfixes.
CLIP_HELPER := $(shell pkg-config --exists xcb xcb-xfixes && echo dev-clip-helper)

.PHONY: all clip-helper test test-wine test-mac test-packages clean

all: $(CLIP_HELPER) dev-compositor dev-files dev-settings dev-swatch dev-compat olympium-run olympium-testapp dev-install desk-note desk-paste test-blur test-macho splash.png

splash.png: splash.c
	$(CC) $(CFLAGS) -o splash-gen splash.c $(CAIRO_FLAGS)
	./splash-gen splash.png

# macicon.c decodes an icon in a confined child. Launchpad uses it for any
# picture that did not come from a system package.
ICON_SOURCES := macicon.c macbundle.c macho.c sandbox.c
ICON_HEADERS := macicon.h macbundle.h macho.h sandbox.h

dev-compositor: compositor.c blur.c blur.h xwayland-compat.h $(ICON_SOURCES) $(ICON_HEADERS)
	$(CC) $(CFLAGS) -o $@ compositor.c blur.c $(ICON_SOURCES) $(WLROOTS_FLAGS) $(WAYLAND_FLAGS) $(CAIRO_FLAGS) $(PIXBUF_FLAGS) $(XKB_FLAGS) $(DRM_FLAGS) -lm

# Builds the helper where it can be built and does nothing elsewhere.
clip-helper: $(CLIP_HELPER)

# The same compositor built as if wlroots had no Xwayland, which is how the
# i386 image builds it. The test starts it to see that a session still comes up.
dev-compositor-nox: compositor.c blur.c blur.h xwayland-compat.h $(ICON_SOURCES) $(ICON_HEADERS)
	$(CC) $(CFLAGS) -DDEV_DESK_NO_XWAYLAND -o $@ compositor.c blur.c $(ICON_SOURCES) $(WLROOTS_FLAGS) $(WAYLAND_FLAGS) $(CAIRO_FLAGS) $(PIXBUF_FLAGS) $(XKB_FLAGS) $(DRM_FLAGS) -lm

dev-clip-helper: clip-helper.c
	$(CC) $(CFLAGS) -o $@ clip-helper.c $(shell pkg-config --cflags --libs xcb xcb-xfixes)

dev-files: files.c macbundle.c macicon.c macho.c sandbox.c macbundle.h macicon.h macho.h sandbox.h
	$(CC) $(CFLAGS) $(LARGE_FILES) -o $@ files.c macbundle.c macicon.c macho.c sandbox.c $(GTK_FLAGS)

dev-settings: settings.c
	$(CC) $(CFLAGS) -o $@ settings.c $(GTK_FLAGS)

dev-swatch: swatch.c
	$(CC) $(CFLAGS) -o $@ swatch.c $(GTK_FLAGS)

dev-compat: compat.c macho.c macho.h
	$(CC) $(CFLAGS) -o $@ compat.c macho.c

olympium-run: run.c
	$(CC) $(CFLAGS) -o $@ run.c

olympium-testapp: testapp.c
	$(CC) $(CFLAGS) -o $@ testapp.c $(GTK_FLAGS)

# Mac disk images and packages are unpacked by 7-Zip and cpio in a confined
# child. The payload decoder needs liblzma and zlib.
MAC_SOURCES := macpkg.c macbundle.c macicon.c macho.c sandbox.c
MAC_HEADERS := macpkg.h macbundle.h macicon.h macho.h sandbox.h

# A disk image is often larger than 2 GB. On a 32-bit system the file
# calls only see a file that size when they are built for large files.
LARGE_FILES := -D_FILE_OFFSET_BITS=64

dev-install: install.c $(MAC_SOURCES) $(MAC_HEADERS)
	$(CC) $(CFLAGS) $(LARGE_FILES) -o $@ install.c $(MAC_SOURCES) $(GTK_FLAGS) -llzma -lz

desk-note: desk-note.c
	$(CC) $(CFLAGS) -o $@ desk-note.c $(GTK_FLAGS)

desk-paste: desk-paste.c
	$(CC) $(CFLAGS) -o $@ desk-paste.c $(GTK_FLAGS)

test-blur: test_blur.c blur.c blur.h
	$(CC) $(CFLAGS) -o $@ test_blur.c blur.c

test-macho: test_macho.c macho.c macho.h
	$(CC) $(CFLAGS) -o $@ test_macho.c macho.c

test: test-blur test-macho
	./test-blur
	./test-macho

# Needs wine, Xwayland, and mingw. It exits 77 when one is missing.
test-wine:
	./test-wine.sh

# Needs 7-Zip, cpio, and python3. It exits 77 without the first two.
test-mac:
	./test-mac.sh

# Needs dpkg-deb and python3, and rpm, rpm2cpio and rpmbuild for the .rpm part.
test-packages:
	./test-packages.sh

clean:
	rm -f dev-clip-helper dev-compositor-nox dev-compositor dev-files dev-settings dev-swatch dev-compat olympium-run olympium-testapp dev-install desk-note desk-paste test-blur test-macho splash-gen splash.png
