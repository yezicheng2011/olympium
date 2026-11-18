#!/usr/bin/env python3
"""Render the verified guest calculator area in a Wayland test window."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import socket
import subprocess
import tempfile

import gi
gi.require_version('Gtk', '3.0')
gi.require_version('Gdk', '3.0')
from gi.repository import Gdk, GdkPixbuf, GLib, Gtk


def qmp_command(stream, name, arguments=None):
    stream.write((json.dumps(dict(execute=name, arguments=arguments or {})) + '\n').encode())
    stream.flush()
    for line in stream:
        response = json.loads(line)
        if 'error' in response:
            raise RuntimeError(str(response['error']))
        if 'return' in response:
            return response['return']
    raise RuntimeError('QEMU monitor disconnected')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qmp', required=True)
    parser.add_argument('--hdc', required=True)
    parser.add_argument('--target', required=True)
    parser.add_argument('--bounds', required=True, nargs=4, type=int)
    parser.add_argument('--reference', type=Path, required=True)
    args = parser.parse_args()
    x, y, right, bottom = args.bounds
    if not (0 <= x < right <= 360 and 0 <= y < bottom <= 720):
        parser.error('invalid guest bounds')
    width, height = right - x, bottom - y
    display_width, display_height = width * 4 // 5, height * 4 // 5
    window = Gtk.Window(title='Calculator')
    window.set_decorated(False)
    window.set_resizable(False)
    surface = Gtk.EventBox()
    picture = Gtk.Image()
    surface.set_size_request(display_width, display_height)
    surface.add(picture)
    window.add(surface)
    error = []
    pending_click = None
    pending_frame = None
    last_pixels = None
    close_requested = False

    with (socket.socket(socket.AF_UNIX) as connection,
          tempfile.TemporaryDirectory(prefix='olympium-oh-frame-') as directory,
          ThreadPoolExecutor(max_workers=1) as executor):
        connection.settimeout(5)
        connection.connect(args.qmp)
        with connection.makefile('rwb') as monitor:
            if 'QMP' not in json.loads(monitor.readline()):
                raise RuntimeError('Missing QEMU monitor greeting')
            qmp_command(monitor, 'qmp_capabilities')
            frame = Path(directory) / 'frame.ppm'

            def read_frame():
                qmp_command(monitor, 'screendump', dict(filename=str(frame)))
                pixels = GdkPixbuf.Pixbuf.new_from_file(str(frame))
                if pixels.get_width() != 360 or pixels.get_height() != 720:
                    raise RuntimeError('Guest display size changed')
                cropped = pixels.new_subpixbuf(x, y, width, height)
                return cropped.scale_simple(display_width, display_height, GdkPixbuf.InterpType.BILINEAR)

            def send_click(gx, gy):
                result = subprocess.run(
                    [args.hdc, '-t', args.target, 'shell', 'uitest', 'uiInput', 'click',
                     str(gx), str(gy)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    text=True, timeout=12, check=True)
                if 'No Error' not in result.stdout:
                    raise RuntimeError('Guest rejected pointer input: ' + result.stdout.strip())

            def closed(*_):
                Gtk.main_quit()

            def request_close(*_):
                nonlocal close_requested
                close_requested = True
                return False

            def receive_click(_widget, event):
                nonlocal pending_click
                if (event.button == Gdk.BUTTON_PRIMARY and 0 <= event.x < display_width and
                        0 <= event.y < display_height and
                        (pending_click is None or pending_click.done())):
                    if pending_click is not None:
                        try:
                            pending_click.result()
                        except Exception as failure:
                            error.append(failure)
                            closed()
                            return True
                    gx = x + int(event.x * width / display_width)
                    gy = y + int(event.y * height / display_height)
                    print(f'Wayland pointer → guest ({gx}, {gy})', flush=True)
                    pending_click = executor.submit(send_click, gx, gy)
                return True

            def refresh():
                nonlocal pending_frame, pending_click, last_pixels
                try:
                    if pending_click is not None and pending_click.done():
                        pending_click.result()
                        pending_click = None
                    if pending_frame is not None and pending_frame.done():
                        pixbuf = pending_frame.result()
                        pending_frame = None
                        current = pixbuf.get_pixels()
                        if current != last_pixels:
                            if last_pixels is None:
                                pixbuf.savev(str(args.reference), 'png', [], [])
                            picture.set_from_pixbuf(pixbuf)
                            last_pixels = current
                    if pending_frame is None and window.get_mapped():
                        pending_frame = executor.submit(read_frame)
                    return True
                except Exception as failure:
                    error.append(failure)
                    closed()
                    return False

            surface.add_events(Gdk.EventMask.BUTTON_PRESS_MASK | Gdk.EventMask.BUTTON_RELEASE_MASK)
            surface.connect('button-press-event', receive_click)
            window.connect('delete-event', request_close)
            window.connect('destroy', closed)
            window.show_all()
            timer = GLib.timeout_add(160, refresh)
            Gtk.main()
            if not error:
                GLib.source_remove(timer)
            if pending_frame is not None:
                pending_frame.result()
            if pending_click is not None:
                pending_click.result()
    if error:
        raise error[0]
    if close_requested:
        return
    raise RuntimeError('Window exited without a close request')


if __name__ == '__main__':
    main()
