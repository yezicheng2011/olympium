# Programs from different systems on one desktop

This is what a Linux program and a Windows program can do with each other on this desktop, what the desktop draws around them, and what was run. There is no Android runtime in the image, and macOS programs are kept and not started, so neither takes part.

## Between a Windows program and a Linux program

| What | How | Run |
| --- | --- | --- |
| Copy in one, paste in the other | The compositor's clipboard and Xwayland's selection bridge. Text. | Both directions, in `make -C desktop test-wine`. |
| One shared folder | `$OLYMPIUM_DESK_HOME/exchange` is drive `S:` in the Wine prefix. `dev-install launch` adds the link to a finished prefix. | A file written on Linux was read with `type S:\from-linux.txt`. A file written to `S:\` by `cmd` was read on Linux. |
| Opening a Linux document with a Windows program | A path argument that exists is handed to the Windows program as `Z:\...`. | `report one.txt`, with a space in its name, reached `desk-win.exe` and `GetFileAttributes` found it. |
| A window like any other | A Wine window gets the same title bar, shadow, corners, dock dot, and focus rules as a Wayland window. | The window mapped in every test run. No picture of a Wine window was taken in this pass. |
| Launchpad and dock | Import writes a `.desktop` file and a dock pin for every kind of program. | On the boot of `9c1b41e4…`, before this pass. |

## What is not there

- The clipboard is not stored. When the program that copied closes, the clipboard is empty, as on any Wayland desktop. Wine keeps its own copy while `wineserver` is still running, which is 60 s after the last Windows program. A keeper that stores the text in the compositor was written and removed in this pass: with a Wine program running it set off thousands of selection requests, and the cause was not found.
- Images, files, and rich text on the clipboard were not tested. Drag and drop between a Wine window and a Wayland window was not tried.
- A copy made by a Windows program before its window is in front is ignored.
- Windows programs do not appear in Launchpad by themselves. Wine's own start-menu entries are not read. Only a file imported through Install is listed.
- A Windows program is not offered as the handler of a file type unless it was imported with `--mime EXT`.

## Widgets

Three glass widgets sit on the wallpaper at the right edge, under every window: the time and date, this month with today marked, and memory, disk, and battery. The compositor draws them. They are not programs, they take no clicks, and their layer is rebuilt once every ten seconds.

View, Show or Hide Widgets turns them off and on, and the choice is kept in `$OLYMPIUM_DESK_HOME/widgets`. `olympium.widgets=0` on the kernel command line turns them off for a boot. Reduce Transparency draws them opaque.

There is one fixed set. They cannot be moved, added, or removed one by one, and a program cannot supply its own.

## The look

- A window that is not in front has grey controls and a paler title.
- A back window's shadow and title bar were painted over the window in front of it. They are now cut to the part that is visible.

What still differs from a Mac is in the README: no open or minimize animation, no Mission Control, no search panel, no quick-settings panel, GTK controls inside windows, and the Ubuntu typeface.

## Where this was run

The x86_64 build machine, the compositor with the headless backend, Xwayland, and Wine `9.0~repack-4build3`. The pictures were taken from that headless compositor at 1280 by 800 with the pixman renderer. No image was rebuilt and nothing here was booted.
