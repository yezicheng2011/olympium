/*
 * Copyright 2026 Daniel Ye
 * Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Turns an application's .icns into a picture the desktop can show, without
 * the desktop ever decoding it.
 *
 * An icon file comes with the application and is whatever its author made
 * it. Decoding a PNG or an .icns is done by image libraries that have had
 * memory-safety bugs, and the programs that show icons here run as the
 * session's user: the installer, Files, and, for Launchpad, the compositor.
 * So the decoding is done by a child process that can open no file and no
 * connection. All it gives back is 256 by 256 pixels, a block of bytes of a
 * known length that is treated as colours and nothing else. The picture file
 * the desktop keeps is then written from those pixels by this process.
 */
#ifndef DEV_MACICON_H
#define DEV_MACICON_H

enum { MAC_ICON_SIZE = 256 };

/*
 * Decodes icns_path in a confined child and writes a PNG to png_out.
 * Returns 0 when the picture was written.
 */
int mac_icon_render(const char *icns_path, const char *png_out);

#endif
