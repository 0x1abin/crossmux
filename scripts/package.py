#!/usr/bin/env python3
"""Create a validated EEGO A4 app/full release package."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
from pathlib import Path


VERSION_HEADER = "lib/EegoA4Support/include/EegoA4Hardware.h"
ENVIRONMENT = "eego_a4_diagnostics"
FLASH_SIZE = 0x1000000
SEGMENTS = (
    ("bootloader.bin", 0x0000),
    ("partitions.bin", 0x8000),
    ("boot_app0.bin", 0xE000),
    ("eego-a4-template-app.bin", 0x10000),
)
SOURCE_IMAGES = {
    "crosslink_full_1.0.8": "79c4cdbae8fbb66e8ec586b61ac1209561bee83068627b69fd1f657d970c1483",
    "crosslink_app_1.0.10": "43264c93c8e371db6f2e44574027e50d9444dcff40cc6a645828d2558d70cf24",
    "official_eego_a4_1.2.7": "37e121af158cf63ca4483d79d4e66a0537f153c7b0d1c0a02a7acc74753f059c",
}


def read_version(project_dir: Path) -> str:
    """Read the public firmware version from the board-support contract."""
    header = (project_dir / VERSION_HEADER).read_text(encoding="utf-8")
    match = re.search(r'TEMPLATE_VERSION\s*=\s*"([^"]+)"', header)
    if not match:
        raise ValueError(f"could not read TEMPLATE_VERSION from {VERSION_HEADER}")
    return match.group(1)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_boot_app0() -> Path:
    candidates: list[Path] = []
    if core_dir := os.environ.get("PLATFORMIO_CORE_DIR"):
        candidates.append(
            Path(core_dir) / "packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
        )
    candidates.extend(
        [
            Path.home()
            / ".platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin",
            Path.home()
            / ".platformio/packages/framework-arduinoespressif32-libs/esp32s3/bin/boot_app0.bin",
        ]
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(
        "boot_app0.bin not found. Build with PlatformIO once or set PLATFORMIO_CORE_DIR."
    )


def copy_required(build_dir: Path, release_dir: Path) -> None:
    required = {
        build_dir / "bootloader.bin": release_dir / "bootloader.bin",
        build_dir / "partitions.bin": release_dir / "partitions.bin",
        build_dir / "firmware.bin": release_dir / "eego-a4-template-app.bin",
        find_boot_app0(): release_dir / "boot_app0.bin",
    }
    missing = [str(source) for source in required if not source.is_file()]
    if missing:
        raise FileNotFoundError(
            "Build products are missing:\n  "
            + "\n  ".join(missing)
            + "\nRun: pio run -e eego_a4_diagnostics"
        )
    for source, destination in required.items():
        shutil.copyfile(source, destination)


def make_full_image(release_dir: Path) -> Path:
    output = release_dir / "eego-a4-template-full-16mb.bin"
    image = bytearray(b"\xff") * FLASH_SIZE
    occupied: list[tuple[int, int, str]] = []

    for filename, offset in SEGMENTS:
        source = release_dir / filename
        data = source.read_bytes()
        end = offset + len(data)
        if end > FLASH_SIZE:
            raise ValueError(f"{filename} exceeds 16 MiB flash")
        for other_start, other_end, other_name in occupied:
            if offset < other_end and end > other_start:
                raise ValueError(
                    f"{filename} at 0x{offset:x} overlaps {other_name} "
                    f"at 0x{other_start:x}..0x{other_end:x}"
                )
        image[offset:end] = data
        occupied.append((offset, end, filename))

    output.write_bytes(image)
    return output


def write_metadata(
    project_dir: Path, release_dir: Path, full_image: Path, version: str
) -> None:
    safe_flasher = release_dir / "safe_flash_template.py"
    shutil.copyfile(project_dir / "scripts" / "safe_flash_template.py", safe_flasher)
    license_file = release_dir / "LICENSE"
    notice_file = release_dir / "NOTICE.md"
    third_party_file = release_dir / "THIRD_PARTY_NOTICES.md"
    shutil.copyfile(project_dir / "LICENSE", license_file)
    shutil.copyfile(project_dir / "NOTICE.md", notice_file)
    shutil.copyfile(project_dir / "THIRD_PARTY_NOTICES.md", third_party_file)
    files = [
        release_dir / "bootloader.bin",
        release_dir / "partitions.bin",
        release_dir / "boot_app0.bin",
        release_dir / "eego-a4-template-app.bin",
        full_image,
        safe_flasher,
        license_file,
        notice_file,
        third_party_file,
    ]
    file_records = {
        path.name: {"size": path.stat().st_size, "sha256": sha256(path)} for path in files
    }
    manifest = {
        "schema": 1,
        "name": "EEGO A4 hardware template firmware",
        "version": version,
        "board": "EEGO A4",
        "mcu": "ESP32-S3",
        "flash_mode": "dio",
        "flash_size": FLASH_SIZE,
        "partition_csv_sha256": sha256(project_dir / "partitions.csv"),
        "full_image_segments": [
            {
                "file": filename,
                "offset": offset,
                "offset_hex": f"0x{offset:x}",
                "size": (release_dir / filename).stat().st_size,
            }
            for filename, offset in SEGMENTS
        ],
        "files": file_records,
        "reverse_engineering_sources": SOURCE_IMAGES,
        "safety": {
            "automatic_sleep": False,
            "automatic_flash_write": False,
            "sd_format": False,
            "wifi_credentials_persisted": False,
            "frontlight_requires_i2c_ack": True,
            "deep_sleep_timer_seconds": [3, 60],
        },
        "safe_flash": {
            "tool": safe_flasher.name,
            "write_guards": [
                "package SHA256SUMS",
                "ESP32-S3 identity",
                "operator-confirmed MAC",
                "mandatory 16 MiB backup",
                "matching partition table for app-only update",
                "post-write verify-flash",
            ],
        },
        "external_font_resources": {
            "required_path": "/fonts/MiSansA4",
            "format": "CPFONT v4, 2-bit, regular",
            "point_sizes": [8, 10, 12, 14, 16, 18],
            "embedded_in_flash_image": False,
        },
        "hardware_contract": {
            "display": {
                "controller": "UC8279C",
                "framebuffer": [768, 552],
                "controller_scan": [768, 600],
                "portrait_logical": [552, 768],
                "safe_content": {
                    "outer_radius_px": 60,
                    "inset_px": 12,
                    "x": 12,
                    "y": 12,
                    "width": 528,
                    "height": 744,
                    "inner_radius_px": 48,
                    "foreground_required": True,
                    "full_panel_exceptions": [
                        "background",
                        "display_edge_or_waveform_test",
                        "touch_edge_test",
                    ],
                },
                "ui_content_rect": {
                    "inset_px": 28,
                    "x": 28,
                    "y": 28,
                    "width": 496,
                    "height": 712,
                    "radius_px": 0,
                    "default_canvas_clip": True,
                    "recommended_title_origin": [32, 32],
                },
            },
            "touch": {
                "controller": "GSLX680",
                "i2c_address": "0x40",
                "calibration": "official EEGO A4 1.2.7 full-range transform",
            },
            "charge_status": {
                "gpio": 11,
                "polarity_rule": (
                    "LM3630A present: active-high; "
                    "LM3630A absent: active-low"
                ),
                "validated_unit": "LM3630A absent / active-low",
                "phase_semantics": "requires battery, charging, full captures",
            },
        },
    }
    (release_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    release_readme = f"""# EEGO A4 template firmware {version}

- `eego-a4-template-full-16mb.bin`: first install at offset `0x0`.
- `eego-a4-template-app.bin`: app-only update at `0x10000`, only after the
  symmetric 6.4 MiB OTA partition table has already been installed.
- Verify every file against `SHA256SUMS` before flashing.
- Review `LICENSE`, `NOTICE.md`, and `THIRD_PARTY_NOTICES.md` before
  redistribution.
- Run `python safe_flash_template.py --port <PORT> --preflight-only` before
  flashing. A write requires a new 16 MiB backup path and the exact MAC printed
  by preflight; app-only mode also requires a byte-identical partition table.
- The Chinese/English font test reads CPFONT v4 resources from SD
  `/fonts/MiSansA4` (8/10/12/14/16/18 pt); font files are not embedded in this
  flash image.
- The firmware never sleeps automatically. The optional `sleep` console command
  is clamped to 3..60 seconds and always enables timer wake.
- The calibrated portrait rounded-safe shape is
  `x=12, y=12, 528x744, R48` inside the physical `R60` aperture. Ordinary
  foreground UI uses the inscribed zero-radius rectangle
  `x=28, y=28, 496x712`, with a recommended title origin of `(32,32)`.
  Full-panel drawing is reserved for backgrounds and explicit display/touch
  edge tests.
- Full documentation is in the source project's `README.md` and `docs/`.

This package is generated, not flashed, by `scripts/package.py`.

Safe app-only update:

```sh
python safe_flash_template.py --port /dev/cu.usbmodemXXXX --app-update \\
  --backup eego-a4-before-template.bin --confirm-mac 00:00:00:00:00:00
```

Safe first/full install:

```sh
python safe_flash_template.py --port /dev/cu.usbmodemXXXX --first-install \\
  --backup eego-a4-before-template.bin --confirm-mac 00:00:00:00:00:00
```
"""
    (release_dir / "README.md").write_text(release_readme, encoding="utf-8")

    checksum_lines = [
        f"{file_records[path.name]['sha256']}  {path.name}" for path in sorted(files)
    ]
    checksum_lines.extend(
        [
            f"{sha256(release_dir / 'manifest.json')}  manifest.json",
            f"{sha256(release_dir / 'README.md')}  README.md",
        ]
    )
    (release_dir / "SHA256SUMS").write_text("\n".join(checksum_lines) + "\n", encoding="utf-8")


def verify_release(release_dir: Path) -> None:
    expected_files = {
        "README.md",
        "SHA256SUMS",
        "LICENSE",
        "NOTICE.md",
        "THIRD_PARTY_NOTICES.md",
        "boot_app0.bin",
        "bootloader.bin",
        "eego-a4-template-app.bin",
        "eego-a4-template-full-16mb.bin",
        "manifest.json",
        "partitions.bin",
        "safe_flash_template.py",
    }
    actual_entries = {path.name for path in release_dir.iterdir()}
    if actual_entries != expected_files:
        missing = sorted(expected_files - actual_entries)
        unexpected = sorted(actual_entries - expected_files)
        details = []
        if missing:
            details.append("missing: " + ", ".join(missing))
        if unexpected:
            details.append("unexpected/stale: " + ", ".join(unexpected))
        raise ValueError(
            "release directory is not deterministic (" + "; ".join(details) + ")"
        )

    manifest = json.loads((release_dir / "manifest.json").read_text(encoding="utf-8"))
    full = release_dir / "eego-a4-template-full-16mb.bin"
    if full.stat().st_size != FLASH_SIZE:
        raise ValueError("full image is not exactly 16 MiB")
    if sha256(full) != manifest["files"][full.name]["sha256"]:
        raise ValueError("full image hash does not match manifest")
    full_data = full.read_bytes()
    for segment in manifest["full_image_segments"]:
        source = (release_dir / segment["file"]).read_bytes()
        offset = segment["offset"]
        if full_data[offset : offset + len(source)] != source:
            raise ValueError(f"merged segment mismatch: {segment['file']}")

    checksum_records: dict[str, str] = {}
    for line in (release_dir / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
        parts = line.split(maxsplit=1)
        if len(parts) != 2:
            raise ValueError(f"malformed SHA256SUMS line: {line!r}")
        digest, filename = parts
        filename = filename.strip()
        if filename in checksum_records:
            raise ValueError(f"duplicate SHA256SUMS entry: {filename}")
        checksum_records[filename] = digest
    checksum_targets = expected_files - {"SHA256SUMS"}
    if set(checksum_records) != checksum_targets:
        raise ValueError("SHA256SUMS does not cover the exact release file set")
    for filename, expected_digest in checksum_records.items():
        if sha256(release_dir / filename) != expected_digest:
            raise ValueError(f"SHA256SUMS mismatch: {filename}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=Path,
        help="release directory (default: release/<TEMPLATE_VERSION>)",
    )
    args = parser.parse_args()

    project_dir = Path(__file__).resolve().parents[1]
    version = read_version(project_dir)
    build_dir = project_dir / ".pio/build" / ENVIRONMENT
    release_dir = (args.output or project_dir / "release" / version).resolve()
    release_dir.mkdir(parents=True, exist_ok=True)

    copy_required(build_dir, release_dir)
    full_image = make_full_image(release_dir)
    write_metadata(project_dir, release_dir, full_image, version)
    verify_release(release_dir)

    print(f"Packaged {full_image}")
    print(f"Full image SHA-256: {sha256(full_image)}")
    print(f"Manifest: {release_dir / 'manifest.json'}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
