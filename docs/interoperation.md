# Programs from different systems on one desktop

This is what a Linux program and a Windows program can do with each other on this desktop, what the desktop draws around them, and what was run. There is no Android runtime in the image, so Android takes no part. A Mac application can be installed and listed, and is started only through Darling, which is not in the image; `docs/mac-apps.md` covers that, and no Mac program took part in anything here.

## Between a Windows program and a Linux program

| What | How | Run |
| --- | --- | --- |
| Copy in one, paste in the other | The compositor's clipboard and Xwayland's selection bridge. Text. | Both directions, in `make -C desktop test-wine`. |
| One shared folder | `$OLYMPIUM_DESK_HOME/exchange` is drive `S:` in the Wine prefix. `dev-install launch` adds the link to a finished prefix. | A file written on Linux was read with `type S:\from-linux.txt`. A file written to `S:\` by `cmd` was read on Linux. |
| Opening a Linux document with a Windows program | A path argument that exists is handed to the Windows program as `Z:\...`. | `report one.txt`, with a space in its name, reached `desk-win.exe` and `GetFileAttributes` found it. |
| A window like any other | A Wine window gets the same title bar, shadow, corners, dock dot, and focus rules as a Wayland window. | The window mapped in every test run. No picture of a Wine window was taken in this pass. |
| Launchpad and dock | Import writes a `.desktop` file for every kind of program, and a dock pin for Linux and Windows ones. A shortcut that a Windows installer puts in the Start Menu becomes an entry too: Wine writes it under the user's applications, and Launchpad reads that folder and the folders under it. | Import: on the boot of `9c1b41e4…`. The Start Menu shortcut: `make -C desktop test-wine` made one under Wine, and Launchpad listed it and started it. |

## The clipboard

Text copied in one program pastes in another, between a Windows program and a Linux program in both directions. Three things were added to make that hold.

- The desktop keeps a copy. On Wayland and on X the clipboard is not stored: the program that copied hands the bytes over when someone pastes, so closing that program empties it. The compositor now reads text a quarter of a second after it is copied and, when the owner goes away, offers the same text itself. Up to 4 MB, text only, in memory only. `OLYMPIUM_CLIP_KEEP=0` turns it off.
- `dev-clip-helper` watches the X clipboard. The first version of the keeper offered old text in the moment after a Windows program had taken the clipboard and before the Xwayland bridge had told the seat. That took the clipboard back from the program, and the bridge then asked itself for the data: 579 requests in one test run. The helper tells the compositor who owns the X clipboard, and text is only offered while nobody does. The same test run now makes 14 requests.
- The helper nudges a silent owner. The bridge asks a new owner what it holds in the instant the owner changes. Wine is often still inside the call that took ownership and leaves that question unanswered until something else arrives. About one copy in ten from a Windows program never reached the desktop: 18 of 20 pastes arrived. The helper asks again 120 ms and 500 ms later, which wakes it. 30 of 30 pastes arrived, with the keeper on and with it off.

In `make -C desktop test-wine`: a Windows program copied, was closed, and Wine was stopped, and a Wayland program then pasted its text. A Wayland program copied, was closed, and the next program pasted its text.

The helper reads no clipboard contents. It asks only for the list of formats and throws the answer away.

## What is not there

- Images, files, and rich text on the clipboard were not tested. Drag and drop between a Wine window and a Wayland window was not tried.
- A copy made by a Windows program before its window is in front is ignored.
- A Windows program that puts no shortcut in the Start Menu appears in Launchpad only when it was imported through Install. One with a shortcut and no icon of its own is drawn with the stand-in tile.
- A Windows program is not offered as the handler of a file type unless it was imported with `--mime EXT`.

## Widgets

Three glass widgets sit on the wallpaper at the right edge, under every window: the time and date, this month with today marked, and memory, disk, and battery. The compositor draws them. They are not programs, they take no clicks, and their layer is rebuilt once every ten seconds.

View, Show or Hide Widgets turns them off and on, and the choice is kept in `$OLYMPIUM_DESK_HOME/widgets`. `olympium.widgets=0` on the kernel command line turns them off for a boot. Reduce Transparency draws them opaque.

There is one fixed set. They cannot be moved, added, or removed one by one, and a program cannot supply its own.

## The look

- The magnifier in the menu bar opens a search panel, as do Control and Super with Space. It searches the list Launchpad shows, so a Linux program, a Windows shortcut, and a Mac application are found the same way. In `make -C desktop test-mac` it found a Mac application by a Chinese name, put a name that starts with the typed letters first, and started the selected one on Return.
- A window that is not in front has grey controls and a paler title.
- A back window's shadow and title bar were painted over the window in front of it. They are now cut to the part that is visible.

What still differs from a Mac is in the README: no open or minimize animation, no Mission Control, no quick-settings panel, GTK controls inside windows, and the Ubuntu typeface. The search panel does not look in files.

## Where this was run

The x86_64 build machine, the compositor with the headless backend, Xwayland, and Wine `9.0~repack-4build3`. The pictures were taken from that headless compositor at 1280 by 800 with the pixman renderer. No image was rebuilt and nothing here was booted.
