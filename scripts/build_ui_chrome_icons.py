#!/usr/bin/env python3
"""Generate native-size UI chrome from the repository's maintained Lucide SVGs.

Requires the existing SDK generator dependencies: rsvg-convert and Pillow.
Tabs: 56px, stroke-width=2 in the original 24px viewBox, threshold=110.
Battery: rasterize at 32px, crop rows [6,26) without resampling (32x20).
The charging path is fitted inside the battery cavity before rasterization.
"""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

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
    text += '\n// Battery: 32px SVG raster, rows [6,26); no bitmap resizing.\n'
    for alias in ('bookmark', 'bluetooth'):
        data, center = generator.pack(generator.rasterize(str(SVG_DIR / (alias + '.svg')), 24), 24)
        bits = f'icon_reader_{alias}_24_bits'
        text += f'\n// lucide: {alias}; reader footer 24px\nstatic const uint8_t {bits}[] = {{'
        text += ', '.join(f'0x{byte:02X}' for byte in data) + '};\n'
        text += f'static const freeink::Icon icon_reader_{alias}_24 = {{24, 24, {center}, {bits}}};\n'
    with tempfile.TemporaryDirectory(prefix='ui-chrome-icons-') as directory:
        for alias, source in (('battery', 'battery'), ('charging', 'battery-charging')):
            svg = SVG_DIR / (source + '.svg')
            if alias == 'charging':
                root = ET.parse(svg).getroot()
                bolt = root.find('{http://www.w3.org/2000/svg}path')
                assert bolt is not None and bolt.attrib['d'] == 'm11 7-3 5h4l-3 5'
                for child in list(root):
                    if child is not bolt:
                        root.remove(child)
                bolt.set('transform', 'translate(2 2) scale(0.8)')
                svg = Path(directory) / 'charging.svg'
                ET.ElementTree(root).write(svg, encoding='unicode')
            data, _ = generator.pack(generator.rasterize(str(svg), 32), 32)
            data = data[6 * 4:26 * 4]
            assert len(data) == 80
            name = 'battery_32x20' if alias == 'battery' else 'battery_charging_32x20'
            bits = f'icon_{name}_bits'
            text += f'// lucide: {source}\nstatic const uint8_t {bits}[] = {{'
            text += ', '.join(f'0x{byte:02X}' for byte in data) + '};\n'
            text += f'static const freeink::Icon icon_{name} = {{32, 20, 10, {bits}}};\n'
    output.write_text(text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=OUTPUT)
    generate(parser.parse_args().out)
