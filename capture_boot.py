"""Reset the Read Pico over its native USB-Serial/JTAG and capture the boot log.

Reset is esptool's HardReset (esptool/reset.py:152-166) with DTR held low: on this
port DTR drives the IO0 strap, so the USBJTAGSerialReset sequence (which pulls IO0
low) reboots into DOWNLOAD mode instead of the application. Keeping DTR low and
pulsing RTS alone gives a normal boot.

Doing the reset inside this same session is the point: any gap between a separate
reset tool exiting and the port opening loses the first seconds, and the SD font
discovery/load messages land at ~2.4 s.
"""

import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM8"
BAUD = 115200
SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
OUT = sys.argv[3] if len(sys.argv) > 3 else "build_font16_boot.log"

p = serial.Serial(PORT, BAUD, timeout=0.2)


def dtr(v):
    p.dtr = v


def rts(v):
    p.rts = v


# Hard reset, normal boot: IO0 (DTR) stays high, EN (RTS) is pulsed low.
dtr(False)  # IO0 = HIGH -> boot the application, not the ROM download stub
rts(False)
time.sleep(0.1)
rts(True)  # EN = LOW, chip held in reset
time.sleep(0.2)
rts(False)  # EN = HIGH, chip boots

t0 = time.time()
buf = bytearray()
dropped = None
while time.time() - t0 < SECONDS:
    try:
        chunk = p.read(8192)
    except (serial.SerialException, OSError) as exc:
        # The panel can be powered off mid-capture, which makes the CDC port vanish.
        # Keep what we already have instead of losing the whole session to a traceback.
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
    print(f"WARNING: port dropped early -- {dropped}")

KEYS = ("[EPDT]", "Page render:", "Rendered page in")
for i, ln in enumerate(lines):
    if any(k in ln for k in KEYS):
        print(f"{i:4d}: {ln}")

print("--- first 12 lines ---")
for i, ln in enumerate(lines[:12]):
    print(f"{i:4d}: {ln}")
print("--- last 6 lines ---")
for i, ln in enumerate(lines[-6:], start=max(0, len(lines) - 6)):
    print(f"{i:4d}: {ln}")
