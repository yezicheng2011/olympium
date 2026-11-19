# Rendering performance

How much CPU the compositor uses, measured, and what was done about it. Everything here is the headless backend with the pixman (software) renderer at 1280x800 on one cloud build machine with 4 cores. The numbers belong to that machine. Real displays, GPUs and applications need their own measurements.

## Method

`desktop/measure-render.sh [SECONDS]` starts the compositor and `test-animate`, an X window repainted every 16 ms, waits for the desktop to settle, and reads the compositor's CPU time from `/proc` and its frame count from its status file for the sample.

## Idle

A desktop with nothing changing renders no frames. `make -C desktop test-render` checks that (at most 4 frames in 4 seconds, zero in the last runs). Another session measured 25.6% of one core before that change and 0.8% after.

## While a window keeps changing

The menu bar, the dock and each title bar are glass: a blur of what the renderer drew behind them, a white veil, a rounded shape. They were rebuilt from scratch every frame, and a callgrind profile put `make_chrome` at 63% of the frame, most of it the blur and the veil. When a window animates somewhere else, the pixels behind the bars are the same as in the last frame.

The finished glass is now kept, keyed by the pixels it was made from and the shape and veil. It is reused only when the pixels match byte for byte, and it depends on no other state, so nothing about the window title, the hover or the clock can go stale. It is bounded at 40 MB, and `OLYMPIUM_BLUR_CACHE=0` turns it off.

| | Frames a second | CPU, one core | CPU a frame |
| --- | --- | --- | --- |
| Cache off | 42 | 30 to 31% | 7.1 to 7.6 ms |
| Cache on | 48 | 21 to 22% | 4.5 ms |

Two runs each, on an otherwise idle machine. About 38% less CPU for each frame.

`OLYMPIUM_BLUR_CACHE=verify` makes the glass again on every use and aborts the compositor if it differs from the kept one. `make -C desktop test-render` runs an animating window under it: about 630 uses, 3 blurs, no difference.

## Where the rest goes

At 4.5 ms a frame the glass is no longer the main cost. What remains is, in the profile, drawing the bars' icons and text with cairo (`draw_chrome_on`, `draw_icon`), uploading each bar as a new texture, and the wlroots scene repainting the whole screen, because `output_frame` adds the whole output as damage every frame. Doing less needs damage tracking for the bars, which is a larger change, and no state-keyed cache was added for it because the drawing reads many fields (menus, hover, network, clock, launchpad) and a missed one would leave a stale picture.

## Image size, measured and left alone

The install image's squashfs is 1915 MB with gzip. With zstd at level 15 it is 1825 MB, 5% less, in 214 seconds. Most of the image is firmware that Ubuntu already ships compressed and Wine's files, so a stronger algorithm finds little. The firmware is the hardware support, so it was not cut either.
