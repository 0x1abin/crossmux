#!/usr/bin/env python3
"""Check lossless font subsetting, shared indices and optional regeneration."""

import importlib.util
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "lib/EpdFont/scripts"
FONTS = ROOT / "lib/EpdFont/builtinFonts"
CHARACTERS = "0123456789 +-×÷.%=e�"
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


class BuiltinFontShrinkTest(unittest.TestCase):
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
            self.assertEqual(len(glyphs), 4014)
            self.assertEqual(hashlib.sha256(b"".join(packed)).hexdigest(), digest)
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
