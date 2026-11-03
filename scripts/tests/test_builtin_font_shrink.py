#!/usr/bin/env python3
"""Check lossless font subsetting, shared indices and optional regeneration."""

import importlib.util
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "lib/EpdFont/scripts"
FONTS = ROOT / "lib/EpdFont/builtinFonts"
CHARACTERS = "0123456789 +-×÷.%=e�"
CJK_ADDED_CODEPOINTS = {0x0401, 0x0451, 0x2116} | set(range(0x0410, 0x0450))
# Only the 14/16/18pt i18n subsets gain these already-common Chinese glyphs.
CJK_TRANSLATION_CODEPOINTS = set(map(ord, "买仍仓代便借序忙核满组购"))
REGENERATE = "--regenerate" in sys.argv
if REGENERATE:
    sys.argv.remove("--regenerate")

spec = importlib.util.spec_from_file_location("share_intervals", SCRIPTS / "share-cn-font-intervals.py")
share = importlib.util.module_from_spec(spec)
spec.loader.exec_module(share)
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location("compress_cn", SCRIPTS / "compress-cn-builtin-fonts.py")
compress_cn = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compress_cn)


def array(text, suffix):
    return re.search(rf"\w+{suffix}\[\d*\] = \{{(.*?)\n\}};", text, re.S).group(1)


def rows(text, suffix):
    return [tuple(int(value.strip(), 0) for value in row.split(','))
            for row in re.findall(r"\{\s*([^{}]+?)\s*\}", array(text, suffix))]


def glyphs_with_bitmaps(text):
    glyphs = rows(text, "Glyphs")
    bitmaps = bytes(int(value, 16) for value in re.findall(r"0x([\dA-F]{2})", array(text, "Bitmaps")))
    decoded = {}
    for offset, length, raw_length, count, first in rows(text, "Groups"):
        raw = zlib.decompress(bitmaps[offset:offset + length], -15)
        assert len(raw) == raw_length
        position = 0
        for index in range(first, first + count):
            width, height = glyphs[index][:2]
            length = ((width + 3) // 4) * height
            decoded[index] = raw[position:position + length]
            position += length
        assert position == len(raw)
    return {cp: (glyphs[offset + cp - first][:6], decoded[offset + cp - first])
            for first, last, offset in rows(text, "Intervals") for cp in range(first, last + 1)}


def cjk_glyphs(size):
    name = f"notosans_cjk_{size}"
    text = (FONTS / f"{name}.h").read_text()
    glyphs, packed = compress_cn.packed_glyphs(text, name)
    intervals = (FONTS / share.SHARED_FILE).read_text() if size <= 12 else text
    keys = ("width", "height", "advanceX", "left", "top", "dataLength")
    return {cp: (tuple(glyphs[offset + cp - first][key] for key in keys), packed[offset + cp - first])
            for first, last, offset in rows(intervals, "Intervals") for cp in range(first, last + 1)}


class BuiltinFontShrinkTest(unittest.TestCase):
    def test_cjk_source_supported_cyrillic_and_numero_coverage(self):
        required = CJK_ADDED_CODEPOINTS
        for size in (8, 10, 12, 14, 16, 18):
            with self.subTest(size=size):
                glyphs = cjk_glyphs(size)
                self.assertTrue(required <= glyphs.keys(),
                                f"{size}pt missing {sorted(required - glyphs.keys())}")
                for cp in required:
                    metrics, pixels = glyphs[cp]
                    self.assertGreater(min(metrics[:3]), 0, f"{size}pt U+{cp:04X}")
                    self.assertTrue(any(pixels), f"{size}pt U+{cp:04X} has no visible pixels")

    def test_new_translation_glyphs_are_present_and_nonempty(self):
        for size in (8, 10, 12, 14, 16, 18):
            glyphs = cjk_glyphs(size)
            self.assertTrue(CJK_TRANSLATION_CODEPOINTS <= glyphs.keys(), size)
            for codepoint in CJK_TRANSLATION_CODEPOINTS:
                metrics, pixels = glyphs[codepoint]
                self.assertGreater(metrics[0], 0, (size, codepoint))
                self.assertTrue(any(pixels), (size, codepoint))

    def test_cjk_existing_codepoints_metrics_and_pixels_are_unchanged(self):
        # Reviewed 2108d739 fonts, before adding the source-supported Cyrillic and № glyphs.
        expected = {
            8: (4014, "e36373fc0855b9210909469da3ae79f0e663370e09f5c9a8939877bf18afd426"),
            10: (4014, "3347cf3692595c7d51fdc417c7f4a6b9397f1858581e24686047ff543a7e4c47"),
            12: (4014, "fb493de7d327c639d0d0f91089edc15b9840077f2c438e978ea3fa84f53af102"),
            14: (1304, "de63203dbdf416e0c165c7d006643985aac0c8c3b901e993abed5ce0e013aed0"),
            16: (1304, "433bfae14125d63685d55eaee9742cff79816a40cf4e835c3879bfd312045d73"),
            18: (1304, "20ff1771b3add035274ee22dea827e55de66245acf247dedd88b94074a58b6fe"),
        }
        for size, (count, digest) in expected.items():
            glyphs = cjk_glyphs(size)
            added = CJK_ADDED_CODEPOINTS | (CJK_TRANSLATION_CODEPOINTS if size >= 14 else set())
            legacy = {cp: glyph for cp, glyph in glyphs.items() if cp not in added}
            self.assertEqual(len(legacy), count)
            self.assertEqual(len(glyphs), count + len(added))
            fingerprint = hashlib.sha256()
            for cp, (metrics, pixels) in sorted(legacy.items()):
                fingerprint.update(struct.pack("<I6i", cp, *metrics) + pixels)
            self.assertEqual(fingerprint.hexdigest(), digest, f"{size}pt legacy glyphs changed")

    def test_common_cjk_pixels_groups_and_idempotence(self):
        # Packed-pixel fingerprints from the reviewed, uncompressed 441a854b tree.
        expected = {
            8: "b3aee0eed81fb5b65f121848446631c7c103bfd5e781880fdfb100eb37153c94",
            10: "eddcab932438c0f79fafe814abfe67922440f78b87f0e14e8b3af7d7958e74c8",
            12: "42e04886df7658eb2e9805a0fd30dad74ae51a819269babc252411db771f0198",
        }
        for size, digest in expected.items():
            name = f"notosans_cjk_{size}"
            text = (FONTS / f"{name}.h").read_text()
            glyphs, packed = compress_cn.packed_glyphs(text, name)
            self.assertEqual(len(glyphs), 4014 + len(CJK_ADDED_CODEPOINTS))
            legacy = [pixels for cp, (_, pixels) in sorted(cjk_glyphs(size).items())
                      if cp not in CJK_ADDED_CODEPOINTS]
            self.assertEqual(hashlib.sha256(b"".join(legacy)).hexdigest(), digest)
            groups = compress_cn.parse_groups(compress_cn.array_body(text, name + "Groups"))
            self.assertLessEqual(max(g["uncompressedSize"] for g in groups), 4096)
            self.assertEqual(compress_cn.compressed_header(text, name), text)

    def test_macro_selects_exclusive_font_dependencies(self):
        common = {"chinese_chess_16.h", "notosans_cjk_12.h", "ubuntu_12_regular.h", "ubuntu_12_bold.h"}
        normal = {"notosans_8_regular.h", "notosans_18_regular.h", "notosans_18_bold.h",
                  "notosans_cjk_8.h", "notosans_cjk_10.h", "ubuntu_10_regular.h", "ubuntu_10_bold.h"}
        high = {"notosans_12_regular.h", "notosans_14_regular.h", "notosans_14_bold.h",
                "notosans_16_regular.h", "notosans_16_bold.h", "notosans_cjk_14.h", "notosans_cjk_16.h"}
        for high_dpi in (False, True):
            command = ["c++", "-MM", "-x", "c++", "-", "-I" + str(FONTS.parent)]
            if high_dpi:
                command.append("-DCROSSMUX_UI_PROFILE_HIGH_DPI")
            result = subprocess.run(command, input='#include <builtinFonts/all.h>\n', text=True,
                                    capture_output=True, check=True)
            assets = {Path(p).name for p in result.stdout.replace("\\\n", "").split()
                      if Path(p).name in common | normal | high}
            self.assertEqual(assets, common | (high if high_dpi else normal))

    def test_invalid_font_does_not_replace_any_output(self):
        with tempfile.TemporaryDirectory() as d:
            directory = Path(d)
            for size in (8, 10, 12):
                (directory / f"notosans_cjk_{size}.h").write_text(
                    (FONTS / f"notosans_cjk_{size}.h").read_text() if size != 12 else "invalid")
            before = {p.name: p.read_bytes() for p in directory.iterdir()}
            result = subprocess.run([sys.executable, str(SCRIPTS / "compress-cn-builtin-fonts.py"), str(directory)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(before, {p.name: p.read_bytes() for p in directory.iterdir()})

    def test_calculator_bitmaps_and_coverage(self):
        for style in ("regular", "bold"):
            original = glyphs_with_bitmaps((FONTS / f"notosans_18_{style}.h").read_text())
            subset = glyphs_with_bitmaps((FONTS / f"calculator_18_{style}.h").read_text())
            self.assertEqual(set(subset), set(map(ord, CHARACTERS)))
            for cp, glyph in subset.items():
                self.assertEqual(glyph, original[cp], f"{style}: U+{cp:04X}")

    def test_shared_outputs_are_current(self):
        for path, output in share.shared_outputs(FONTS):
            self.assertEqual(path.read_text(), output)

    def test_sharing_changes_only_interval_storage(self):
        shared = (FONTS / share.SHARED_FILE).read_text()
        body = array(shared, "Intervals")
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            for size in (8, 10, 12):
                name = f"notosans_cjk_{size}"
                text = (FONTS / f"{name}.h").read_text()
                text = text.replace(f'#include "{share.SHARED_FILE}"\n', '')
                text = text.replace(f'{share.SHARED_NAME},', f'{name}Intervals,')
                declaration = f'static const EpdUnicodeInterval {name}Intervals[] = {{{body}\n}};\n'
                # Restore the generator's original location, immediately before the font descriptor.
                text = text.replace(f'\n\nstatic const EpdFontData {name}',
                                    f'\n{declaration}\nstatic const EpdFontData {name}')
                (directory / f"{name}.h").write_text(text)
            for path, output in share.shared_outputs(directory):
                self.assertEqual(output, (FONTS / path.name).read_text())

    def test_mismatched_indices_do_not_write_anything(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            for size in (8, 10, 12):
                offset = 1 if size == 12 else 0
                (directory / f"notosans_cjk_{size}.h").write_text(
                    f"static const EpdUnicodeInterval notosans_cjk_{size}Intervals[] = {{\n"
                    f"    {{ 0x20, 0x21, 0x{offset:X} }},\n}};\n")
            before = {path: path.read_bytes() for path in directory.iterdir()}
            result = subprocess.run([sys.executable, str(SCRIPTS / "share-cn-font-intervals.py"), str(directory)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("interval tables differ", result.stderr)
            self.assertEqual(before, {path: path.read_bytes() for path in directory.iterdir()})

    @unittest.skipUnless(REGENERATE, "pass --regenerate with the font-generation Python environment")
    def test_calculator_generation_is_reproducible(self):
        for style in ("Regular", "Bold"):
            name = f"calculator_18_{style.lower()}"
            result = subprocess.run(
                [sys.executable, "fontconvert.py", name, "18", f"../builtinFonts/source/NotoSans/NotoSans-{style}.ttf",
                 "--2bit", "--compress", "--pnum", "--zopfli", "--characters", CHARACTERS],
                cwd=SCRIPTS, check=True, capture_output=True, text=True)
            self.assertEqual(result.stdout, (FONTS / f"{name}.h").read_text())


if __name__ == "__main__":
    unittest.main()
