"""Check complete font-ID output and fail closed on missing source headers."""
import importlib.util
import shutil
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('font_ids', ROOT / 'lib/EpdFont/scripts/build-font-ids.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class FontIdsTest(unittest.TestCase):
    def test_generated_ids_match_committed_header(self):
        ids = generator.font_ids(ROOT / 'lib/EpdFont/builtinFonts')
        self.assertEqual(generator.render_header(ids), (ROOT / 'src/fontIds.h').read_text())
        self.assertEqual(len(ids), 13)
        self.assertNotIn(0, ids.values())
        self.assertEqual(len(set(ids.values())), len(ids))

    def test_missing_font_fails_instead_of_emitting_an_id(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(FileNotFoundError):
                generator.font_ids(Path(directory))

    def test_cjk_bitmap_and_shared_index_changes_invalidate_reader_ids(self):
        directory = ROOT / 'lib/EpdFont/builtinFonts'
        baseline = generator.font_ids(directory)
        for changed in ('notosans_cjk_12.h', 'notosans_cjk_common_intervals.h'):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                staging = Path(temporary)
                for name in ('notosans_cjk_12.h', 'notosans_cjk_common_intervals.h'):
                    shutil.copy2(directory / name, staging / name)
                with (staging / changed).open('ab') as output:
                    output.write(b'\n// changed source\n')
                updated = generator.font_ids(directory, staging)
                for name in baseline:
                    if name.startswith(('NOTOSERIF_', 'NOTOSANS_')):
                        self.assertNotEqual(updated[name], baseline[name], name)
                    else:
                        self.assertEqual(updated[name], baseline[name], name)


if __name__ == '__main__':
    unittest.main()
