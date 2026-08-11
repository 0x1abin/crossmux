#!/usr/bin/env python3
"""Capture and render the framebuffer of the running EEGO A4 diagnostics app."""

from __future__ import annotations

import argparse
import hashlib
import time
from pathlib import Path


WIDTH = 768
HEIGHT = 552
EXPECTED_BYTES = WIDTH * HEIGHT // 8


def read_line(device: serial.Serial, deadline: float) -> bytes:
    data = bytearray()
    while time.monotonic() < deadline:
        value = device.read(1)
        if not value:
            continue
        if value == b"\n":
            return bytes(data).rstrip(b"\r")
        data.extend(value)
        if len(data) > 4096:
            raise RuntimeError("serial line exceeded 4096 bytes")
    raise TimeoutError("timed out waiting for a serial line")


def read_exact(device: serial.Serial, size: int, deadline: float) -> bytes:
    data = bytearray()
    while len(data) < size and time.monotonic() < deadline:
        chunk = device.read(size - len(data))
        if chunk:
            data.extend(chunk)
    if len(data) != size:
        raise RuntimeError(f"short framebuffer transfer: {len(data)}/{size}")
    return bytes(data)


def send_command(device: serial.Serial, command: str) -> None:
    device.write(f"CMD:{command}\n".encode("utf-8"))
    device.flush()


def wait_for_header(device: serial.Serial, timeout: float) -> int:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = read_line(device, deadline)
        if line.startswith(b"SCREENSHOT_ERROR:"):
            raise RuntimeError(line.decode("ascii", errors="replace"))
        if line.startswith(b"SCREENSHOT_START:"):
            return int(line.split(b":", 1)[1])
    raise TimeoutError("missing SCREENSHOT_START")


def open_serial(port: str) -> serial.Serial:
    device = serial.Serial()
    device.port = port
    device.baudrate = 115200
    device.timeout = 0.05
    device.write_timeout = 5
    device.dtr = False
    device.rts = False
    device.open()
    return device


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Read the diagnostic firmware's exact binary framebuffer protocol "
            "and write raw, PBM, and rotated portrait PNG files."
        )
    )
    parser.add_argument("--port", required=True, help="serial port, e.g. /dev/ttyACM0")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("captures"),
        help="output directory (default: captures)",
    )
    parser.add_argument(
        "--name", default="eego-a4-screen", help="output basename without extension"
    )
    parser.add_argument(
        "--page-command",
        action="append",
        default=[],
        help="command to send before capture; repeat for multiple commands",
    )
    parser.add_argument(
        "--boot-wait",
        type=float,
        default=3.0,
        help="seconds to wait after opening USB CDC (default: 3)",
    )
    parser.add_argument(
        "--page-wait",
        type=float,
        default=3.0,
        help="seconds after each page command (default: 3)",
    )
    parser.add_argument(
        "--timeout", type=float, default=30.0, help="capture timeout in seconds"
    )
    args = parser.parse_args()

    global serial, Image
    try:
        import serial
        from PIL import Image
    except ImportError as error:
        raise SystemExit(
            "missing host dependency; run: python -m pip install -r requirements.txt"
        ) from error

    args.output_dir.mkdir(parents=True, exist_ok=True)
    raw_path = args.output_dir / f"{args.name}.raw"
    pbm_path = args.output_dir / f"{args.name}.pbm"
    png_path = args.output_dir / f"{args.name}.png"

    with open_serial(args.port) as device:
        time.sleep(max(0.0, args.boot_wait))
        device.reset_input_buffer()
        for command in args.page_command:
            send_command(device, command)
            time.sleep(max(0.0, args.page_wait))
            device.reset_input_buffer()

        send_command(device, "screenshot")
        declared_size = wait_for_header(device, args.timeout)
        if declared_size != EXPECTED_BYTES:
            raise RuntimeError(
                f"unexpected framebuffer size: {declared_size}/{EXPECTED_BYTES}"
            )
        raw = read_exact(device, declared_size, time.monotonic() + args.timeout)
        footer = read_line(device, time.monotonic() + 10)
        if footer != b"SCREENSHOT_END":
            raise RuntimeError(f"bad screenshot footer: {footer!r}")

    raw_path.write_bytes(raw)
    pbm_path.write_bytes(f"P4\n{WIDTH} {HEIGHT}\n".encode("ascii") + raw)
    Image.frombytes("1", (WIDTH, HEIGHT), raw).rotate(-90, expand=True).save(
        png_path
    )

    digest = hashlib.sha256(raw).hexdigest()
    print(f"captured {len(raw)} bytes")
    print(f"raw SHA-256: {digest}")
    print(f"raw: {raw_path}")
    print(f"pbm: {pbm_path}")
    print(f"png: {png_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
