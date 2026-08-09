#!/usr/bin/env python3
"""Package one A4/M4 beta build with the CrossMux/Sticky flash layout."""

import argparse
import configparser
import csv
import hashlib
import json
import shutil
import subprocess
from pathlib import Path


DEVICES = {
    'eego-a4': 'eego_a4',
    'mofei-m4': 'mofei_m4',
}
SEGMENTS = (
    ('bootloader', 'bootloader.bin', 0x0000),
    ('partitions', 'partitions.bin', 0x8000),
    ('boot_app0', 'boot_app0.bin', 0xE000),
    ('firmware', 'firmware.bin', 0x10000),
)
EXPECTED_PARTITIONS = {
    'otadata': (0xE000, 0x2000),
    'app0': (0x10000, 0x640000),
    'app1': (0x650000, 0x640000),
}


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def git_value(root, *args):
    return subprocess.check_output(['git', *args], cwd=root, text=True).strip()


def verify_partition_csv(root):
    found = {}
    with (root / 'partitions.csv').open(newline='') as stream:
        for row in csv.reader(line for line in stream if not line.lstrip().startswith('#')):
            if len(row) < 5:
                continue
            name = row[0].strip()
            if name in EXPECTED_PARTITIONS:
                found[name] = (int(row[3].strip(), 0), int(row[4].strip(), 0))
    if found != EXPECTED_PARTITIONS:
        raise SystemExit(f'partitions.csv is not crossmux-sticky-v1: {found!r}')


def find_boot_app0():
    path = Path.home() / '.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'
    if not path.is_file():
        raise SystemExit(f'boot_app0.bin not found at {path}; build the environment first')
    return path


def copy_asset(source, destination):
    if not source.is_file():
        raise SystemExit(f'missing build asset: {source}')
    shutil.copyfile(source, destination)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('device', choices=DEVICES)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    environment = DEVICES[args.device]
    build = root / '.pio/build' / environment
    output = (args.output or root / 'dist' / args.device).resolve()
    output.mkdir(parents=True, exist_ok=True)

    verify_partition_csv(root)
    copy_asset(build / 'bootloader.bin', output / 'bootloader.bin')
    copy_asset(build / 'partitions.bin', output / 'partitions.bin')
    copy_asset(build / 'firmware.bin', output / 'firmware.bin')
    copy_asset(find_boot_app0(), output / 'boot_app0.bin')

    firmware_size = (output / 'firmware.bin').stat().st_size
    if firmware_size > EXPECTED_PARTITIONS['app0'][1]:
        raise SystemExit(f'firmware.bin is {firmware_size} bytes; app slot is 0x640000 bytes')

    config = configparser.ConfigParser()
    config.read(root / 'platformio.ini')
    short_sha = git_value(root, 'rev-parse', '--short', 'HEAD')
    version = f"{config['crosspoint']['version']}-{args.device}-cn-beta+{short_sha}"
    assets = []
    for role, name, offset in SEGMENTS:
        path = output / name
        assets.append({
            'role': role,
            'name': name,
            'offset': offset,
            'size': path.stat().st_size,
            'sha256': sha256(path),
        })
    manifest = {
        'schemaVersion': 1,
        'device': args.device,
        'environment': environment,
        'chip': 'ESP32-S3',
        'flavor': 'zh-CN',
        'version': version,
        'crossmuxSha': git_value(root, 'rev-parse', 'HEAD'),
        'sdkSha': git_value(root / 'freeink-sdk', 'rev-parse', 'HEAD'),
        'partitionProfile': 'crossmux-sticky-v1',
        'flash': {'size': 0x1000000, 'mode': 'dio', 'frequency': '80m'},
        'assets': assets,
    }
    manifest_path = output / 'manifest.json'
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')

    checksum_paths = [output / name for _, name, _ in SEGMENTS]
    checksum_paths.append(manifest_path)
    (output / 'SHA256SUMS').write_text(
        ''.join(f'{sha256(path)}  {path.name}\n' for path in checksum_paths)
    )
    print(f'Packaged {environment} {version} in {output}')


if __name__ == '__main__':
    main()
