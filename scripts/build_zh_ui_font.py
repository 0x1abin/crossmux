#!/usr/bin/env python3
"""Generate Chinese UI font headers (Noto Sans SC subset).

Scans translation YAMLs to discover the exact set of CJK codepoints
referenced by UI strings, merges them into intervals, and invokes
fontconvert.py to emit five .h files under lib/EpdFont/builtinFonts/:

    notosans_sc_8_regular.h
    notosans_sc_10_regular.h
    notosans_sc_10_bold.h
    notosans_sc_12_regular.h
    notosans_sc_12_bold.h

fontconvert.py's built-in interval list (Latin, Cyrillic, punctuation, ...)
is preserved so the SC family also covers ASCII / Latin glyphs --- which
keeps mixed-script labels like "WiFi" or "KOReader" rendering correctly
when the UI switches to Chinese.

Usage:
    python3 scripts/build_zh_ui_font.py
"""
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TRANSLATIONS_DIR = REPO_ROOT / "lib" / "I18n" / "translations"
FONT_SRC_DIR = REPO_ROOT / "lib" / "EpdFont" / "builtinFonts" / "source" / "NotoSansSC"
FONT_OUT_DIR = REPO_ROOT / "lib" / "EpdFont" / "builtinFonts"
FONTCONVERT = REPO_ROOT / "lib" / "EpdFont" / "scripts" / "fontconvert.py"

# (size, style, source filename)
JOBS = [
    (8,  "regular", "NotoSansSC-Regular.otf"),
    (10, "regular", "NotoSansSC-Regular.otf"),
    (10, "bold",    "NotoSansSC-Bold.otf"),
    (12, "regular", "NotoSansSC-Regular.otf"),
    (12, "bold",    "NotoSansSC-Bold.otf"),
]

# Scan English (for any non-ASCII Latin in source strings) and the Chinese
# translation (for the actual hanzi). fontconvert.py already covers ASCII
# by default, so anything <= 0x7F adds nothing.
YAML_SOURCES = ["english.yaml", "simplified_chinese.yaml"]
MIN_CP = 0x80


def collect_codepoints():
    cps = set()
    for name in YAML_SOURCES:
        text = (TRANSLATIONS_DIR / name).read_text(encoding="utf-8")
        for ch in text:
            cp = ord(ch)
            if cp >= MIN_CP:
                cps.add(cp)
    return cps


def merge_intervals(cps):
    if not cps:
        return []
    s = sorted(cps)
    out = [[s[0], s[0]]]
    for cp in s[1:]:
        if cp == out[-1][1] + 1:
            out[-1][1] = cp
        else:
            out.append([cp, cp])
    return out


def run_one(size, style, src_filename, intervals):
    font_name = f"notosans_sc_{size}_{style}"
    src = FONT_SRC_DIR / src_filename
    out = FONT_OUT_DIR / f"{font_name}.h"
    if not src.exists():
        sys.exit(f"missing source font: {src}")

    args = [
        sys.executable, str(FONTCONVERT),
        font_name, str(size), str(src),
    ]
    for lo, hi in intervals:
        args += ["--additional-intervals", f"0x{lo:X},0x{hi:X}"]

    print(f"\n=== {font_name} ===", flush=True)
    with out.open("wb") as f:
        proc = subprocess.run(args, stdout=f)
    if proc.returncode != 0:
        sys.exit(f"fontconvert failed for {font_name} (exit {proc.returncode})")
    print(f"  -> {out.name}: {out.stat().st_size:,} bytes", flush=True)


def main():
    if not FONT_SRC_DIR.exists():
        sys.exit(f"font sources missing: {FONT_SRC_DIR}\n"
                 f"download NotoSansSC-Regular.otf and NotoSansSC-Bold.otf into that dir.")

    cps = collect_codepoints()
    intervals = merge_intervals(cps)
    glyph_count = sum(hi - lo + 1 for lo, hi in intervals)
    print(f"Scanned {len(YAML_SOURCES)} yaml file(s): {len(cps)} unique codepoints "
          f"(>= U+{MIN_CP:04X}) merged into {len(intervals)} interval(s), "
          f"~{glyph_count} additional glyphs requested.")

    for size, style, src in JOBS:
        run_one(size, style, src, intervals)

    total = sum((FONT_OUT_DIR / f"notosans_sc_{s}_{w}.h").stat().st_size
                for s, w, _ in JOBS)
    print(f"\nDone. 5 headers, total {total:,} bytes "
          f"(~{total / 1024:.1f} KB) in {FONT_OUT_DIR.relative_to(REPO_ROOT)}/")


if __name__ == "__main__":
    main()
