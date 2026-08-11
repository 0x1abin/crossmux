#!/usr/bin/env python3
"""Safely preflight, back up, flash, and verify an EEGO A4 template package.

No write can start until the package, target identity, operator-confirmed MAC,
mandatory 16 MiB recovery backup, and (for app-only updates) the installed
partition table have all passed their checks.  Use --preflight-only to perform
the package/device checks without changing flash contents.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path


FLASH_BYTES = 16 * 1024 * 1024
PARTITION_OFFSET = 0x8000
PARTITION_BYTES = 0xC00
APP_OFFSET = 0x10000
APP_FILENAME = "eego-a4-template-app.bin"
FULL_FILENAME = "eego-a4-template-full-16mb.bin"
MAC_RE = re.compile(r"\bMAC:\s*([0-9a-fA-F:]{17})\b")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_esptool_python() -> Path:
    candidates = [
        Path(sys.executable),
        Path.home() / ".platformio" / "penv" / "bin" / "python",
    ]
    for candidate in candidates:
        if not candidate.is_file():
            continue
        result = subprocess.run(
            [str(candidate), "-m", "esptool", "version"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        if result.returncode == 0:
            return candidate
    raise RuntimeError("esptool not found; install esptool or PlatformIO first")


def run_esptool(
    python: Path,
    port: str,
    arguments: list[str],
    *,
    baud: int | None = None,
    capture: bool = False,
) -> str:
    command = [
        str(python),
        "-m",
        "esptool",
        "--chip",
        "esp32s3",
        "--port",
        port,
    ]
    if baud is not None:
        command.extend(["--baud", str(baud)])
    command.extend(arguments)
    result = subprocess.run(
        command,
        check=False,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
    )
    output = result.stdout or ""
    if capture and output:
        print(output, end="", flush=True)
    if result.returncode != 0:
        raise RuntimeError(f"esptool failed with exit code {result.returncode}")
    return output


def verify_package(package: Path) -> dict:
    required = (
        "SHA256SUMS",
        "manifest.json",
        "partitions.bin",
        APP_FILENAME,
        FULL_FILENAME,
    )
    missing = [name for name in required if not (package / name).is_file()]
    if missing:
        raise RuntimeError(f"incomplete release package: {', '.join(missing)}")

    entries = []
    for line in (package / "SHA256SUMS").read_text(encoding="ascii").splitlines():
        if not line:
            continue
        expected, separator, relative = line.partition("  ")
        if not separator or not re.fullmatch(r"[0-9a-f]{64}", expected):
            raise RuntimeError(f"invalid SHA256SUMS line: {line!r}")
        path = (package / relative).resolve()
        if package not in path.parents:
            raise RuntimeError(f"checksum path escapes package: {relative}")
        if not path.is_file() or sha256(path) != expected:
            raise RuntimeError(f"checksum mismatch: {relative}")
        entries.append(relative)
    if not entries:
        raise RuntimeError("SHA256SUMS is empty")

    manifest = json.loads((package / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("mcu") != "ESP32-S3":
        raise RuntimeError(f"unexpected manifest MCU: {manifest.get('mcu')!r}")
    if manifest.get("flash_size") != FLASH_BYTES:
        raise RuntimeError("manifest does not describe a 16 MiB flash image")
    if (package / FULL_FILENAME).stat().st_size != FLASH_BYTES:
        raise RuntimeError("full image is not exactly 16 MiB")
    print(
        f"Package checksums: PASS ({len(entries)} files, template {manifest.get('version')})",
        flush=True,
    )
    return manifest


def normalize_mac(value: str) -> str:
    cleaned = value.strip().lower().replace("-", ":")
    if not re.fullmatch(r"[0-9a-f]{2}(?::[0-9a-f]{2}){5}", cleaned):
        raise RuntimeError(f"invalid MAC address: {value!r}")
    return cleaned


def read_device_identity(python: Path, port: str) -> str:
    output = run_esptool(python, port, ["flash-id"], capture=True)
    if "ESP32-S3" not in output:
        raise RuntimeError("connected target did not identify as ESP32-S3")
    match = MAC_RE.search(output)
    if not match:
        raise RuntimeError("could not parse device MAC from esptool output")
    mac = normalize_mac(match.group(1))
    print(f"Device identity: PASS (ESP32-S3, MAC {mac})", flush=True)
    return mac


def read_partition_table(python: Path, port: str, destination: Path) -> None:
    run_esptool(
        python,
        port,
        ["read-flash", hex(PARTITION_OFFSET), hex(PARTITION_BYTES), str(destination)],
    )
    if destination.stat().st_size != PARTITION_BYTES:
        raise RuntimeError("partition-table readback has the wrong length")


def backup_flash(python: Path, port: str, destination: Path) -> str:
    if destination.exists():
        raise RuntimeError(f"refusing to overwrite backup: {destination}")
    if not destination.parent.is_dir():
        raise RuntimeError(f"backup parent directory does not exist: {destination.parent}")
    run_esptool(
        python,
        port,
        ["read-flash", "0x0", hex(FLASH_BYTES), str(destination)],
    )
    if destination.stat().st_size != FLASH_BYTES:
        raise RuntimeError(f"backup is incomplete: {destination.stat().st_size}/{FLASH_BYTES}")
    digest = sha256(destination)
    destination.with_name(destination.name + ".sha256").write_text(
        f"{digest}  {destination.name}\n",
        encoding="ascii",
    )
    print(f"Recovery backup: PASS ({destination}, SHA-256 {digest})", flush=True)
    return digest


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True, help="USB serial device, for example /dev/cu.usbmodem2101")
    parser.add_argument(
        "--package",
        type=Path,
        default=Path(__file__).resolve().parent,
        help="template release directory (default: directory containing this script)",
    )
    parser.add_argument("--baud", type=int, default=921600)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--preflight-only", action="store_true")
    mode.add_argument("--app-update", action="store_true")
    mode.add_argument("--first-install", action="store_true")
    parser.add_argument(
        "--backup",
        type=Path,
        help="new path for the mandatory 16 MiB recovery backup before any write",
    )
    parser.add_argument(
        "--confirm-mac",
        help="exact MAC printed by preflight; required before any write",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    package = args.package.resolve()
    verify_package(package)
    if not args.preflight_only:
        if args.backup is None:
            raise RuntimeError("--backup is mandatory before a write")
        if args.confirm_mac is None:
            raise RuntimeError("--confirm-mac is mandatory before a write")

    esptool_python = find_esptool_python()
    mac = read_device_identity(esptool_python, args.port)
    if not args.preflight_only and normalize_mac(args.confirm_mac) != mac:
        raise RuntimeError(f"MAC confirmation mismatch: connected device is {mac}")

    with tempfile.TemporaryDirectory(prefix="eego-a4-template-preflight-") as temp_dir:
        installed_partitions = Path(temp_dir) / "partitions.bin"
        read_partition_table(esptool_python, args.port, installed_partitions)
        partition_match = installed_partitions.read_bytes() == (package / "partitions.bin").read_bytes()

    print(
        "Installed partition table: "
        + ("MATCH (app-only update allowed)" if partition_match else "DIFFERENT (full install required)"),
        flush=True,
    )

    if args.preflight_only:
        print("PRECHECK RESULT: PASS (no flash data written)", flush=True)
        return 0
    if args.app_update and not partition_match:
        raise RuntimeError("app-only update blocked: installed partition table differs from this package")

    backup_flash(esptool_python, args.port, args.backup.resolve())

    if args.app_update:
        image = package / APP_FILENAME
        offset = APP_OFFSET
        label = "template app update"
    else:
        image = package / FULL_FILENAME
        offset = 0
        label = "template first/full install"

    run_esptool(
        esptool_python,
        args.port,
        ["write-flash", hex(offset), str(image)],
        baud=args.baud,
    )
    run_esptool(
        esptool_python,
        args.port,
        ["verify-flash", hex(offset), str(image)],
        baud=args.baud,
    )
    print(f"FLASH RESULT: PASS ({label}, MAC {mac}, SHA-256 {sha256(image)})", flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
