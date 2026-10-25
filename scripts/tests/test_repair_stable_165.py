"""Safety checks for the temporary 1.6.5 metadata-only correction."""

import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import repair_stable_165 as repair
from nightly_targets import TARGETS, bundle_name, targets_for


class RepairTest(unittest.TestCase):
    def setUp(self):
        self.notes = {'en': ['One', 'Two'], 'zh': ['一', '二']}
        self.index = {
            'schemaVersion': 1, 'channel': 'stable', 'buildId': repair.BUILD_ID, 'updatedAt': 'before',
            'releaseNotes': {'global': self.notes['en'], 'zh-CN': self.notes['zh']},
            'targets': {key: {'variants': {flavor: {'version': repair.VERSION, 'crossmuxSha': repair.SOURCE_SHA}
                                         for flavor in ('global', 'zh-CN')}} for key in TARGETS},
        }

    def test_filters_only_targets_and_timestamp_without_mutating_source(self):
        original = copy.deepcopy(self.index)
        result = repair.candidate(self.index, self.notes, 'after')
        self.assertEqual(self.index, original)
        self.assertEqual(set(result['targets']), {'xteink_x4', 'sticky', 'xteink_x4_pro', 'metalio_eink4'})
        self.assertEqual(result['updatedAt'], 'after')
        self.assertEqual({k: v for k, v in result.items() if k not in ('targets', 'updatedAt')},
                         {k: v for k, v in original.items() if k not in ('targets', 'updatedAt')})
        for key, target in result['targets'].items():
            self.assertEqual(target, original['targets'][key])
        self.assertEqual(repair.candidate(result, self.notes, 'after'), result)

    def test_rejects_wrong_source_notes_or_incomplete_target_set(self):
        for mutation in ('build', 'version', 'sha', 'notes', 'missing', 'unknown'):
            index = copy.deepcopy(self.index)
            if mutation == 'build':
                index['buildId'] = 'another-build'
            elif mutation in ('version', 'sha'):
                index['targets']['sticky']['variants']['global'][
                    'version' if mutation == 'version' else 'crossmuxSha'] = 'wrong'
            elif mutation == 'notes':
                index['releaseNotes']['global'] = ['Changed']
            elif mutation == 'missing':
                del index['targets']['sticky']
            else:
                index['targets']['unknown'] = index['targets']['sticky']
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                repair.candidate(index, self.notes, 'after')

    def test_guard_rejects_concurrent_index_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'backup').mkdir()
            (root / 'cn').mkdir()
            (root / 'backup/stable-cn.json').write_bytes(b'before')
            (root / 'cn/release-index.json').write_bytes(b'after')
            for current in (b'before', b'after'):
                with mock.patch.object(repair, 'fetch_bytes', return_value=current):
                    repair.guard(root, 'cn')
            with mock.patch.object(repair, 'fetch_bytes', return_value=b'other'), self.assertRaises(ValueError):
                repair.guard(root, 'cn')

    def test_retire_only_explicit_withdrawn_zips_and_refuse_unknown_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'global').mkdir()
            (root / 'backup').mkdir()
            bundles = {key: {'name': bundle_name(key, repair.VERSION), 'size': 42, 'sha256': 'a' * 64}
                       for key in targets_for('stable')}
            index = {'targets': {key: {'bundle': value} for key, value in bundles.items()}}
            path = root / 'global/release-index.json'
            path.write_text(json.dumps(index))
            assets = [{'name': b['name'], 'size': 42, 'digest': 'sha256:' + 'a' * 64} for b in bundles.values()]
            assets += [{'name': 'release-index.json', 'digest': repair.digest(path.read_bytes())}]
            obsolete = {bundle_name(key, repair.VERSION) for key in TARGETS if key not in bundles}
            assets += [{'name': name} for name in obsolete]
            release = {'id': 123, 'body': 'approved', 'assets': assets}
            for tag in ('stable', 'version'):
                (root / f'backup/{tag}-release.json').write_text(json.dumps(release))
            lookup = lambda path: {'tag_name': repair.VERSION} if path == 'releases/latest' else release
            with mock.patch.object(repair, 'api', side_effect=lookup), mock.patch.object(repair, 'gh') as gh:
                repair.retire(root)
                self.assertEqual(gh.call_count, 10)
                self.assertEqual({call.args[3] for call in gh.call_args_list}, obsolete)
                self.assertEqual({call.args[2] for call in gh.call_args_list}, {'stable', repair.VERSION})
            release['assets'].append({'name': 'unrelated.zip'})
            with mock.patch.object(repair, 'api', return_value=release), mock.patch.object(repair, 'gh') as gh:
                with self.assertRaisesRegex(ValueError, 'Unexpected Release asset'):
                    repair.retire(root)
                gh.assert_not_called()

    def test_github_guard_can_retry_an_interrupted_index_upload(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'backup').mkdir()
            (root / 'global').mkdir()
            (root / 'global/release-index.json').write_bytes(b'after')
            for label in ('stable-global', 'version-index'):
                (root / f'backup/{label}.json').write_bytes(b'before')
            release = {'id': 12, 'body': 'approved', 'assets': []}
            for label in ('stable', 'version'):
                (root / f'backup/{label}-release.json').write_text(json.dumps(release))
            with mock.patch.object(repair, 'api', return_value=release):
                repair.guard(root, 'global')
                release['assets'] = [{'name': 'release-index.json', 'digest': repair.digest(b'other')}]
                with self.assertRaisesRegex(ValueError, 'changed after backup'):
                    repair.guard(root, 'global')

    def test_only_content_failures_request_rollback(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'output'
            with mock.patch.dict('os.environ', {'GITHUB_OUTPUT': str(output)}):
                with mock.patch.object(repair, 'verify_release', side_effect=RuntimeError('network')):
                    with self.assertRaises(RuntimeError):
                        repair.verify(Path(directory))
                    self.assertFalse(output.exists())
                with mock.patch.object(repair, 'verify_release', side_effect=ValueError('checksum')):
                    with self.assertRaises(ValueError):
                        repair.verify(Path(directory))
                    self.assertEqual(output.read_text(), 'content_failure=true\n')


if __name__ == '__main__':
    unittest.main()
