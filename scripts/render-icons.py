#!/usr/bin/env python3
# Copyright 2026 Ye Zicheng
# SPDX-License-Identifier: Apache-2.0
"""Rasterize the Olympium mark into every host icon set.

The vector masters live in branding/assets. This script does not invent a
different letterform. Dark tiles use the official lime on #0a0b0d. Light
tiles use the same path in #0a0b0d on #f4f5f0.
"""

from __future__ import annotations

import shutil
import struct
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "branding" / "assets"
DARK = ASSETS / "mark-dark.svg"
LIGHT = ASSETS / "mark-light.svg"
GLYPH_LIME = ASSETS / "glyph-lime.svg"
GLYPH_DARK = ASSETS / "glyph-dark.svg"
GLYPH = "M10 8.5h6.2c4.15 0 7.05 2.55 7.05 6.55S20.35 21.6 16.2 21.6H13.1V23.5H10V8.5zm3.1 3.05v7.05h3.05c2.35 0 3.85-1.4 3.85-3.52 0-2.13-1.5-3.53-3.85-3.53H13.1z"
DARK_HEX = (0x0A, 0x0B, 0x0D, 255)
LIGHT_HEX = (0xF4, 0xF5, 0xF0, 255)


def raster(svg: Path, size: int, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    subprocess.check_call(
        [
            "rsvg-convert",
            "--width",
            str(size),
            "--height",
            str(size),
            "--output",
            str(dest),
            str(svg),
        ]
    )


def solid(size: int, color: tuple[int, int, int, int], dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    Image.new("RGBA", (size, size), color).save(dest)


def circle(src: Path, dest: Path) -> None:
    image = Image.open(src).convert("RGBA")
    mask = Image.new("L", image.size, 0)
    ImageDraw.Draw(mask).ellipse((0, 0, image.size[0] - 1, image.size[1] - 1), fill=255)
    image.putalpha(mask)
    dest.parent.mkdir(parents=True, exist_ok=True)
    image.save(dest)


def write_ico(sources: list[Path], dest: Path) -> None:
    blobs = [path.read_bytes() for path in sources]
    count = len(blobs)
    header = struct.pack("<HHH", 0, 1, count)
    entries = b""
    offset = 6 + 16 * count
    for path, blob in zip(sources, blobs):
        image = Image.open(path)
        width = 0 if image.width >= 256 else image.width
        height = 0 if image.height >= 256 else image.height
        entries += struct.pack("<BBBBHHII", width, height, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    dest.write_bytes(header + entries + b"".join(blobs))


def write_icns(pngs: dict[bytes, Path], dest: Path) -> None:
    chunks = []
    for kind, path in pngs.items():
        data = path.read_bytes()
        chunks.append(kind + struct.pack(">I", len(data) + 8) + data)
    body = b"".join(chunks)
    dest.write_bytes(b"icns" + struct.pack(">I", len(body) + 8) + body)


def copy_tree_png(name: str, src: Path, folders: list[Path]) -> None:
    for folder in folders:
        folder.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, folder / name)


def main() -> int:
    if shutil.which("rsvg-convert") is None:
        print("rsvg-convert is required", file=sys.stderr)
        return 1

    raster(DARK, 1024, ASSETS / "mark-dark-1024.png")
    raster(LIGHT, 1024, ASSETS / "mark-light-1024.png")

    linux_sizes = [16, 22, 24, 32, 48, 64, 128, 256, 512]
    for size in linux_sizes:
        raster(DARK, size, ROOT / f"share/icons/hicolor/{size}x{size}/apps/linkspan.png")
    scalable = ROOT / "share/icons/hicolor/scalable/apps"
    scalable.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(DARK, scalable / "linkspan.svg")
    shutil.copyfile(GLYPH_DARK, scalable / "linkspan-symbolic.svg")

    win_sizes = [16, 24, 32, 48, 64, 128, 256]
    win_dir = ROOT / "hosts/windows/Linkspan.Windows/Assets"
    win_pngs = []
    for size in win_sizes:
        dest = ASSETS / "windows" / f"linkspan-{size}.png"
        raster(DARK, size, dest)
        win_pngs.append(dest)
    write_ico(win_pngs, win_dir / "Linkspan.ico")
    shutil.copyfile(win_dir / "Linkspan.ico", ASSETS / "Linkspan.ico")

    mac_specs = [
        ("icon_16x16.png", 16),
        ("icon_16x16@2x.png", 32),
        ("icon_32x32.png", 32),
        ("icon_32x32@2x.png", 64),
        ("icon_128x128.png", 128),
        ("icon_128x128@2x.png", 256),
        ("icon_256x256.png", 256),
        ("icon_256x256@2x.png", 512),
        ("icon_512x512.png", 512),
        ("icon_512x512@2x.png", 1024),
    ]
    iconset = ROOT / "hosts/macos/Linkspan/Assets.xcassets/AppIcon.appiconset"
    iconset.mkdir(parents=True, exist_ok=True)
    (ROOT / "hosts/macos/Linkspan/Assets.xcassets").mkdir(parents=True, exist_ok=True)
    icns_map: dict[bytes, Path] = {}
    icns_types = {
        16: [b"icp4"],
        32: [b"icp5", b"ic11"],
        64: [b"icp6", b"ic12"],
        128: [b"ic07"],
        256: [b"ic08", b"ic13"],
        512: [b"ic09", b"ic14"],
        1024: [b"ic10"],
    }
    cached: dict[int, Path] = {}
    for filename, size in mac_specs:
        if size not in cached:
            cached[size] = ASSETS / "macos" / f"mark-{size}.png"
            raster(DARK, size, cached[size])
        light_name = filename.replace(".png", "_light.png")
        raster(LIGHT, size, iconset / light_name)
        shutil.copyfile(cached[size], iconset / filename)
    for size, kinds in icns_types.items():
        for kind in kinds:
            icns_map[kind] = cached[size]
    icns_path = ROOT / "hosts/macos/Linkspan/Resources/Linkspan.icns"
    write_icns(icns_map, icns_path)
    shutil.copyfile(icns_path, ASSETS / "Linkspan.icns")

    densities = {
        "mipmap-mdpi": 48,
        "mipmap-hdpi": 72,
        "mipmap-xhdpi": 96,
        "mipmap-xxhdpi": 144,
        "mipmap-xxxhdpi": 192,
    }
    android_res = ROOT / "hosts/android/app/src/main/res"
    for folder, size in densities.items():
        full = ASSETS / "android" / f"linkspan-{size}.png"
        raster(DARK, size, full)
        dest_dir = android_res / folder
        dest_dir.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(full, dest_dir / "ic_launcher.png")
        circle(full, dest_dir / "ic_launcher_round.png")

    harmony_scales = {
        "sdpi": 108,
        "mdpi": 144,
        "ldpi": 216,
        "xldpi": 288,
        "xxldpi": 432,
    }
    harmony_roots = [
        ROOT / "hosts/harmony/AppScope/resources",
        ROOT / "hosts/harmony/entry/src/main/resources",
    ]
    for size_name, size in {"base": 216, **harmony_scales}.items():
        dark_icon = ASSETS / "harmony" / f"dark-icon-{size}.png"
        light_icon = ASSETS / "harmony" / f"light-icon-{size}.png"
        dark_fg = ASSETS / "harmony" / f"dark-fg-{size}.png"
        light_fg = ASSETS / "harmony" / f"light-fg-{size}.png"
        dark_bg = ASSETS / "harmony" / f"dark-bg-{size}.png"
        light_bg = ASSETS / "harmony" / f"light-bg-{size}.png"
        raster(DARK, size, dark_icon)
        raster(LIGHT, size, light_icon)
        raster(GLYPH_LIME, size, dark_fg)
        raster(GLYPH_DARK, size, light_fg)
        solid(size, DARK_HEX, dark_bg)
        solid(size, LIGHT_HEX, light_bg)
        if size_name == "base":
            light_qual, dark_qual = "base", "dark"
        else:
            light_qual, dark_qual = size_name, f"dark-{size_name}"
        for root in harmony_roots:
            light_dir = root / light_qual / "media"
            dark_dir = root / dark_qual / "media"
            copy_tree_png("icon.png", light_icon, [light_dir])
            copy_tree_png("icon.png", dark_icon, [dark_dir])
            copy_tree_png("foreground.png", light_fg, [light_dir])
            copy_tree_png("foreground.png", dark_fg, [dark_dir])
            copy_tree_png("background.png", light_bg, [light_dir])
            copy_tree_png("background.png", dark_bg, [dark_dir])
    print("Rendered Olympium icon sets.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
