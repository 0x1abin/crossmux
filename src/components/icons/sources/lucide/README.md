# Maintained Lucide SVG sources

These are the unmodified SVG originals needed by `uiChromeIcons.manifest`,
`listIcons.manifest`, `readerToolbarIcons.manifest` and the chrome generator's
settings/status variants. Maintain these files in CrossMux so changing
the SDK's icon collection does not silently change the UI assets.

Source: <https://github.com/lucide-icons/lucide>, revision
`c81680e066f45b640743ca78ae36cdedda3f0318`, copied from FreeInk SDK revision
`6c2f82245ff6c5e3ac5df582c70ca674a84e1f1e`.
`LICENSE` preserves Lucide's ISC terms and the MIT notices for inherited Feather
icons. `SHA256SUMS` records the original SVG bytes.

## Regeneration

Run from the repository root with Python/Pillow, `rsvg-convert` and the pinned
SDK converter available:

```bash
python3 scripts/build_ui_chrome_icons.py
python3 freeink-sdk/libs/assets/Icons/tools/gen_icons.py \
  --manifest src/components/icons/listIcons.manifest \
  --svgdir src/components/icons/sources/lucide \
  --sizes 48 --out src/components/icons/listIcons48.h
python3 freeink-sdk/libs/assets/Icons/tools/gen_icons.py \
  --manifest src/components/icons/readerToolbarIcons.manifest \
  --svgdir src/components/icons/sources/lucide \
  --sizes 48 --out src/components/icons/readerToolbarIcons48.h
clang-format-21 -i src/components/icons/uiChromeIcons.h \
  src/components/icons/listIcons48.h src/components/icons/readerToolbarIcons48.h
python3 scripts/tests/test_ui_chrome_icons.py
```

For other square icon sizes, use the same manifest and SVG directory with
`--sizes 40,64` and a separate output path. The chrome manifest produces the five
Tab icons; list and toolbar manifests preserve their existing logical aliases.
Rendering uses SVG viewBox/stroke geometry directly, with no bitmap resampling.

Current packing is 1 bit per pixel, MSB first, threshold 110, white/1 transparent
and black/0 ink. Tab icons are 56px; list/toolbar and upright settings icons are
48px. The legacy 32px settings variant is rotated 90 degrees counterclockwise;
reader bookmark/Bluetooth markers are 24px.

Battery SVGs are retained as source references but no longer generated into
chrome bitmaps. High-DPI batteries use the original theme's outline, fill and
charging pixels at 2× through `src/components/BatteryIcon.h`; headers and reader
footers share that painter.

To update an original, copy it from a recorded upstream revision, retain its
license, update the revision/checksums and regenerate the affected headers.
Inspect the resulting pixels before accepting a changed upstream outline.
