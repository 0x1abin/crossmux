#!/usr/bin/env python3
"""Generate native-size UI chrome from the repository's maintained Lucide SVGs.

Requires the existing SDK generator dependencies: rsvg-convert and Pillow.
Tabs: 56px, stroke-width=2 in the original 24px viewBox, threshold=110.
"""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / 'freeink-sdk/libs/assets/Icons/tools/gen_icons.py'
SVG_DIR = ROOT / 'src/components/icons/sources/lucide'
OUTPUT = ROOT / 'src/components/icons/uiChromeIcons.h'


def generate(output):
    subprocess.run([sys.executable, str(GENERATOR), '--manifest',
                    str(ROOT / 'src/components/icons/uiChromeIcons.manifest'),
                    '--svgdir', str(SVG_DIR), '--sizes', '56', '--out', str(output)], check=True)
    spec = importlib.util.spec_from_file_location('sdk_icon_generator', GENERATOR)
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    text = output.read_text()
    # Existing drawIcon() callers expect a bitmap rotated 90 degrees CCW.
    # Regenerate that 32px variant too; FUI lists use the upright 48px variant.
    for size in (32, 48):
        image = generator.rasterize(str(SVG_DIR / 'settings-2.svg'), size)
        if size == 32:
            image = image.rotate(90, expand=True)
        data, center = generator.pack(image, size)
        alias = f'settings_2_{size}'
        bits = f'icon_{alias}_bits'
        text += f'\n// lucide: settings-2; {size}px' + ('; legacy CCW layout' if size == 32 else '') + '\n'
        text += f'static const uint8_t {bits}[] = {{'
        text += ', '.join(f'0x{byte:02X}' for byte in data) + '};\n'
        text += f'static const freeink::Icon icon_{alias} = {{{size}, {size}, {center}, {bits}}};\n'
    for alias in ('bookmark', 'bluetooth'):
        data, center = generator.pack(generator.rasterize(str(SVG_DIR / (alias + '.svg')), 24), 24)
        bits = f'icon_reader_{alias}_24_bits'
        text += f'\n// lucide: {alias}; reader footer 24px\nstatic const uint8_t {bits}[] = {{'
        text += ', '.join(f'0x{byte:02X}' for byte in data) + '};\n'
        text += f'static const freeink::Icon icon_reader_{alias}_24 = {{24, 24, {center}, {bits}}};\n'
    output.write_text(text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=OUTPUT)
    generate(parser.parse_args().out)
