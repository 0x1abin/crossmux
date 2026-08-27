import json
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPTS = Path(__file__).resolve().parents[1]
ROOT = SCRIPTS.parent
sys.path.insert(0, str(SCRIPTS))

import build_nightly_index
import nightly_targets
import package_nightly_target


class NightlyTargetTest(unittest.TestCase):
    def test_matrix_has_c3_and_six_s3_targets(self):
        matrix = package_nightly_target.matrix()['include']
        self.assertEqual(len(matrix), 7)
        self.assertEqual(
            {entry['targetId'] for entry in matrix},
            set(nightly_targets.TARGETS),
        )
        environments = {
            environment
            for target in nightly_targets.TARGETS.values()
            for environment in target['environments'].values()
        }
        self.assertEqual(len(environments), 14)

    def test_runtime_models_and_board_tags_are_explicit(self):
        targets = nightly_targets.TARGETS
        self.assertEqual(targets['xteink_x4']['models'], ['xteink_x3', 'xteink_x4'])
        self.assertEqual(targets['xteink_x4_pro']['boardTag'], 'x4pro')
        self.assertEqual(targets['m5stack_paper_mono']['boardTag'], 'papermono')
        self.assertEqual(targets['eego_a4']['environments']['zh-CN'], 'eego_a4_cn_nightly')

    def test_versions_are_nightly_release_candidates(self):
        self.assertEqual(
            nightly_targets.version_for('1.5.7', 'sticky', 'global', '12345678'),
            '1.5.7-sticky-rc+1234567',
        )
        self.assertEqual(
            nightly_targets.version_for('1.5.7', 'sticky', 'zh-CN', '12345678'),
            '1.5.7-sticky-cn-rc+1234567',
        )
        self.assertNotIn('beta', nightly_targets.version_for('1.5.7', 'sticky', 'global', '1234567'))

    def test_github_release_includes_both_variants(self):
        workflow = (ROOT / '.github/workflows/nightly.yml').read_text()
        self.assertIn("find artifacts -type f -print", workflow)
        self.assertNotIn("find artifacts -path '*/global/*'", workflow)

    def test_coscli_is_verified_before_publishing(self):
        workflow = (ROOT / '.github/workflows/nightly.yml').read_text()
        self.assertIn('coscli-v1.0.8-linux-amd64', workflow)
        self.assertIn(
            '7165f2ae16c5f7ac495864c963ca574a76e04ec72680d7bc8a8eee3234d8cf91', workflow
        )
        self.assertLess(
            workflow.index('Install COS CLI'), workflow.index('Publish immutable GitHub build')
        )
        self.assertNotIn('coscli config add', workflow)
        self.assertIn('cos://${COS_BUCKET}/firmware/builds/', workflow)
        self.assertIn('cos_args+=(--token "$COS_SESSION_TOKEN")', workflow)
        self.assertIn('--fail-output-path "$RUNNER_TEMP/coscli-errors"', workflow)
        self.assertIn('-name error.report -exec cat {} +', workflow)

    def write_image(self, chip_id=0x0009, board='eego_a4'):
        image = bytearray(24)
        image[0] = 0xE9
        image[12:14] = chip_id.to_bytes(2, 'little')
        image.extend(f'CROSSPOINT-BOARD-V1:{board};'.encode())
        temp = tempfile.NamedTemporaryFile(delete=False)
        temp.write(image)
        temp.close()
        self.addCleanup(Path(temp.name).unlink)
        return Path(temp.name)

    def test_rejects_wrong_chip_or_board(self):
        package_nightly_target.verify_firmware(self.write_image(), 0x0009, 'eego_a4')
        with self.assertRaises(SystemExit):
            package_nightly_target.verify_firmware(self.write_image(chip_id=5), 0x0009, 'eego_a4')
        with self.assertRaises(SystemExit):
            package_nightly_target.verify_firmware(self.write_image(board='murphy_m4'), 0x0009, 'eego_a4')


class NightlyIndexTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def write_pair(self, target_id, revision='a' * 40, sdk_revision='b' * 40):
        target = nightly_targets.TARGETS[target_id]
        for flavor in nightly_targets.FLAVOR_TOKENS:
            manifest = {
                'schemaVersion': 1,
                'channel': 'nightly',
                'targetId': target_id,
                'models': target['models'],
                'deviceSlug': target['deviceSlug'],
                'boardTag': target['boardTag'],
                'supportedChannels': target['supportedChannels'],
                'environment': target['environments'][flavor],
                'chip': target['chip'],
                'flavor': flavor,
                'version': f'1.5.7-rc+{revision[:7]}',
                'crossmuxSha': revision,
                'sdkSha': sdk_revision,
                'assets': [{'role': 'firmware', 'name': 'firmware.bin'}],
            }
            (self.root / nightly_targets.manifest_name(target_id, flavor)).write_text(json.dumps(manifest))

    def test_updates_complete_pair_and_retains_missing_target(self):
        self.write_pair('sticky')
        previous = {
            'targets': {
                'eego_a4': {'targetId': 'eego_a4', 'variants': {'global': {'version': 'old'}}},
                'retired': {'targetId': 'retired'},
            }
        }
        index = build_nightly_index.build_index(
            self.root,
            previous,
            'global',
            'https://example.com/nightly/',
            '2026-08-26T00:00:00Z',
            'nightly-test',
        )
        self.assertIn('sticky', index['targets'])
        self.assertIn('eego_a4', index['targets'])
        self.assertNotIn('retired', index['targets'])
        self.assertEqual(
            index['targets']['sticky']['variants']['zh-CN']['manifestUrl'],
            'https://example.com/nightly/sticky-cn-manifest.json',
        )

    def test_does_not_advance_incomplete_pair(self):
        self.write_pair('sticky')
        (self.root / nightly_targets.manifest_name('sticky', 'zh-CN')).unlink()
        index = build_nightly_index.build_index(
            self.root, None, 'cn', 'https://assets.example/firmware/builds/test/', 'now', 'test'
        )
        self.assertNotIn('sticky', index['targets'])

    def test_rejects_mismatched_sdk_pair(self):
        self.write_pair('sticky')
        chinese = self.root / nightly_targets.manifest_name('sticky', 'zh-CN')
        manifest = json.loads(chinese.read_text())
        manifest['sdkSha'] = 'c' * 40
        chinese.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'SDK revisions do not match'):
            build_nightly_index.build_index(
                self.root, None, 'global', 'https://example.com/', 'now', 'test'
            )


if __name__ == '__main__':
    unittest.main()
