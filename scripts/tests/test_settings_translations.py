#!/usr/bin/env python3
"""Check explicit Chinese settings translations and six-size built-in glyph coverage."""

import importlib.util
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("gen_i18n", ROOT / "scripts/gen_i18n.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class SettingsTranslationsTest(unittest.TestCase):
    def test_settings_and_glyph_coverage(self):
        english = generator.parse_yaml_file(str(ROOT / "lib/I18n/translations/english.yaml"))
        chinese = generator.parse_yaml_file(str(ROOT / "lib/I18n/translations/chinese.yaml"))
        sources = [ROOT / "src/SettingsList.h", ROOT / "src/HomeButtonSettings.h"]
        sources += list((ROOT / "src/activities/settings").glob("*.cpp"))
        sources += list((ROOT / "src/activities/settings").glob("*.h"))
        keys = set().union(*(set(re.findall(r"\bSTR_[A-Z0-9_]+\b", path.read_text())) for path in sources))
        self.assertFalse(keys - chinese.keys(), sorted(keys - chinese.keys()))
        for key in english:
            if key.startswith(("STR_THEME_", "STR_SPACING_")):
                self.assertEqual(chinese[key], english[key], key)
        self.assertEqual(chinese["STR_INX_TAB_POSITION"], "Inx 标签栏位置")

        cjk = lambda text: set(re.findall(r"[一-鿿]", text))
        required = cjk((ROOT / "lib/I18n/translations/chinese.yaml").read_text())
        required |= cjk((ROOT / "lib/EpdFont/scripts/cn_almanac_chars.txt").read_text())
        common = cjk((ROOT / "lib/EpdFont/scripts/chars_3500_common.txt").read_text()) | required
        for name, expected in (("cn_common_chars.txt", common), ("cn_i18n_chars.txt", required)):
            self.assertEqual(cjk((ROOT / "lib/EpdFont/scripts" / name).read_text()), expected, name)
        for size in (8, 10, 12, 14, 16, 18):
            name = "notosans_cjk_common_intervals.h" if size <= 12 else f"notosans_cjk_{size}.h"
            header = (ROOT / "lib/EpdFont/builtinFonts" / name).read_text()
            intervals = re.search(r"Intervals\[\] = \{(.*?)\n\};", header, re.S).group(1)
            codepoints = set()
            for first, last in re.findall(r"\{\s*(0x[0-9A-F]+),\s*(0x[0-9A-F]+),", intervals):
                codepoints.update(range(int(first, 16), int(last, 16) + 1))
            missing = {ord(char) for char in (common if size <= 12 else required)} - codepoints
            self.assertFalse(missing, (size, "".join(map(chr, sorted(missing)))))


if __name__ == "__main__":
    unittest.main()
