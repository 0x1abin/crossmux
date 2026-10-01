"""Attach to the Read Pico's USB-Serial/JTAG port and read WITHOUT resetting it.

capture_boot.py deliberately resets so it can catch the boot sequence; this one
must not, because the point is to watch the UI draw the page the user is already
on. Toggling DTR/RTS here would reboot the device and lose that state.

Usage: capture_live.py [port] [seconds] [outfile]
"""

import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM8"
SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 60.0
OUT = sys.argv[3] if len(sys.argv) > 3 else "build_live.log"

p = serial.Serial(PORT, 115200, timeout=0.2)
# Leave the control lines exactly as the OS opened them: on this port DTR drives the
# IO0 strap, and any change can reset the chip.
t0 = time.time()
buf = bytearray()
dropped = None
while time.time() - t0 < SECONDS:
    try:
        chunk = p.read(8192)
    except (serial.SerialException, OSError) as exc:
        dropped = exc
        break
    if chunk:
        buf.extend(chunk)
try:
    p.close()
except Exception:
    pass

with open(OUT, "wb") as fh:
    fh.write(bytes(buf))

text = bytes(buf).decode("utf-8", "replace")
lines = text.splitlines()
print(f"captured {len(buf)} bytes, {len(lines)} lines")
if dropped is not None:
    print(f"WARNING: port dropped -- {dropped}")

KEYS = ("SDFS", "SDMGR", "SDCF", "SDREG", "FCM", "prewarm", "GFX", "MEM", "WIFI", "AIR",
        "EPDT", "font", "Font")
print("--- matching lines ---")
for i, ln in enumerate(lines):
    if any(k in ln for k in KEYS):
        print(f"{i:4d}: {ln}")
print("--- last 25 lines (all) ---")
for i, ln in enumerate(lines[-25:], start=max(0, len(lines) - 25)):
    print(f"{i:4d}: {ln}")
