#!/usr/bin/env python3
# Copyright 2026 Ye Zicheng
# SPDX-License-Identifier: Apache-2.0
"""Check that every host icon set is present and the right size."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def png_size(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path} is not a PNG")
    width, height = struct.unpack(">II", data[16:24])
    return width, height


def expect_png(path: Path, size: int) -> None:
    if not path.is_file():
        raise SystemExit(f"missing {path}")
    found = png_size(path)
    if found != (size, size):
        raise SystemExit(f"{path} is {found}, expected {(size, size)}")


def main() -> int:
    expect_png(ROOT / "branding/assets/mark-dark-1024.png", 1024)
    expect_png(ROOT / "branding/assets/mark-light-1024.png", 1024)
    for name in (
        "mark-dark.svg",
        "mark-light.svg",
        "glyph-lime.svg",
        "glyph-dark.svg",
        "glyph-mono.svg",
    ):
        if not (ROOT / "branding/assets" / name).is_file():
            raise SystemExit(f"missing branding/assets/{name}")

    for size in (16, 22, 24, 32, 48, 64, 128, 256, 512):
        expect_png(ROOT / f"share/icons/hicolor/{size}x{size}/apps/olympium.png", size)
    scalable = ROOT / "share/icons/hicolor/scalable/apps"
    for name in ("olympium.svg", "olympium-symbolic.svg"):
        if not (scalable / name).is_file():
            raise SystemExit(f"missing {scalable / name}")
    desktop = (ROOT / "share/applications/app.olympium.linux.desktop").read_text()
    if "Icon=olympium" not in desktop:
        raise SystemExit("Linux desktop file does not name the icon")

    ico = (ROOT / "hosts/windows/Olympium.Windows/Assets/Olympium.ico").read_bytes()
    count = struct.unpack_from("<H", ico, 4)[0]
    found = []
    offset = 6
    for _ in range(count):
        width, height = ico[offset], ico[offset + 1]
        found.append(256 if width == 0 else width)
        offset += 16
    if found != [16, 24, 32, 48, 64, 128, 256]:
        raise SystemExit(f"Windows icon sizes are {found}")
    csproj = (ROOT / "hosts/windows/Olympium.Windows/Olympium.Windows.csproj").read_text()
    if "ApplicationIcon" not in csproj or "Olympium.ico" not in csproj:
        raise SystemExit("Windows project does not embed Olympium.ico")

    mac_sizes = {
        "icon_16x16.png": 16,
        "icon_16x16@2x.png": 32,
        "icon_32x32.png": 32,
        "icon_32x32@2x.png": 64,
        "icon_128x128.png": 128,
        "icon_128x128@2x.png": 256,
        "icon_256x256.png": 256,
        "icon_256x256@2x.png": 512,
        "icon_512x512.png": 512,
        "icon_512x512@2x.png": 1024,
    }
    iconset = ROOT / "hosts/macos/Olympium/Assets.xcassets/AppIcon.appiconset"
    for name, size in mac_sizes.items():
        expect_png(iconset / name, size)
        expect_png(iconset / name.replace(".png", "_light.png"), size)
    icns = (ROOT / "hosts/macos/Olympium/Resources/Olympium.icns").read_bytes()
    if icns[:4] != b"icns":
        raise SystemExit("Olympium.icns is not an icns file")
    kinds = set()
    cursor = 8
    while cursor + 8 <= len(icns):
        kind = icns[cursor : cursor + 4].decode()
        length = struct.unpack(">I", icns[cursor + 4 : cursor + 8])[0]
        kinds.add(kind)
        cursor += length
    for kind in ("icp4", "icp5", "ic11", "icp6", "ic12", "ic07", "ic08", "ic13", "ic09", "ic14", "ic10"):
        if kind not in kinds:
            raise SystemExit(f"icns is missing {kind}")
    project = (ROOT / "hosts/macos/Olympium.xcodeproj/project.pbxproj").read_text()
    if "ASSETCATALOG_COMPILER_APPICON_NAME = AppIcon" not in project:
        raise SystemExit("Mac project does not use the AppIcon catalog")

    for folder, size in {
        "mipmap-mdpi": 48,
        "mipmap-hdpi": 72,
        "mipmap-xhdpi": 96,
        "mipmap-xxhdpi": 144,
        "mipmap-xxxhdpi": 192,
    }.items():
        base = ROOT / "hosts/android/app/src/main/res" / folder
        expect_png(base / "ic_launcher.png", size)
        expect_png(base / "ic_launcher_round.png", size)
    manifest = (ROOT / "hosts/android/app/src/main/AndroidManifest.xml").read_text()
    if "@mipmap/ic_launcher" not in manifest or "roundIcon" not in manifest:
        raise SystemExit("Android manifest is missing a launcher icon")
    adaptive = (ROOT / "hosts/android/app/src/main/res/mipmap-anydpi-v26/ic_launcher.xml").read_text()
    if "ic_launcher_foreground" not in adaptive or "ic_launcher_monochrome" not in adaptive:
        raise SystemExit("Android adaptive icon is incomplete")

    for root in (
        ROOT / "hosts/harmony/AppScope/resources",
        ROOT / "hosts/harmony/entry/src/main/resources",
    ):
        expect_png(root / "base/media/icon.png", 216)
        expect_png(root / "dark/media/icon.png", 216)
        for qualifier, size in {
            "sdpi": 108,
            "mdpi": 144,
            "ldpi": 216,
            "xldpi": 288,
            "xxldpi": 432,
        }.items():
            for name in ("icon.png", "foreground.png", "background.png"):
                expect_png(root / qualifier / "media" / name, size)
                expect_png(root / f"dark-{qualifier}" / "media" / name, size)
        layered = (root / "base/media/layered_image.json").read_text()
        if "foreground" not in layered or "background" not in layered:
            raise SystemExit(f"{root} layered icon is incomplete")
    print("Icon sets are complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
