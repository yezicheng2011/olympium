# Olympium mark

Linkspan uses the official Olympium mark. The letterform and the dark tile (`#0a0b0d` with `#c5f04a`) are the mark from the Olympium site. Light tiles use the same path in `#0a0b0d` on `#f4f5f0`. Nothing here is copied from another product.

## Masters

| File | Use |
| --- | --- |
| `assets/mark-dark.svg` | Official tile. Dark appearance, and the default app icon. |
| `assets/mark-light.svg` | Same letterform for light appearance. |
| `assets/glyph-lime.svg` | Letterform only, for a dark tile foreground. |
| `assets/glyph-dark.svg` | Letterform only, for a light tile foreground. |
| `assets/glyph-mono.svg` | Letterform only, for Android themed icons. |
| `assets/mark-dark-1024.png` | Raster of the official tile. |
| `assets/mark-light-1024.png` | Raster of the light tile. |

## Regenerating the host sets

`rsvg-convert` and Python Pillow are required.

```bash
python3 scripts/render-icons.py
python3 scripts/check-icons.py
```

The script writes:

- Linux hicolor PNGs, a scalable SVG, and a symbolic SVG under `share/icons`. The desktop file is `share/applications/app.linkspan.linux.desktop` with `Icon=linkspan`.
- A Windows `.ico` at 16, 24, 32, 48, 64, 128, and 256. The Windows project embeds it as the executable icon.
- A macOS asset catalog with 16 through 1024, including the `@2x` sizes, plus light and dark appearances, and `Linkspan.icns`.
- Android mipmaps from mdpi through xxxhdpi, including round icons. The adaptive icon adds the lime foreground, the dark background, and a monochrome glyph.
- OpenHarmony layered icons, start-window icons, and density variants for both the light and dark color modes.

The Windows executable uses one icon, the official tile, because Windows does not switch an executable icon with the system theme. macOS, Linux symbolic icons, Android monochrome icons, and OpenHarmony `dark` resources carry the appearance variants.
