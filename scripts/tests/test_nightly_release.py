import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


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
        environments = {entry['environment'] for entry in matrix}
        self.assertEqual(len(environments), 7)

    def test_runtime_models_and_board_tags_are_explicit(self):
        targets = nightly_targets.TARGETS
        self.assertEqual(targets['xteink_x4']['models'], ['xteink_x3', 'xteink_x4'])
        self.assertEqual(targets['xteink_x4_pro']['boardTag'], 'x4pro')
        self.assertEqual(targets['m5stack_paper_mono']['boardTag'], 'papermono')
        self.assertEqual(targets['eego_a4']['environment'], 'eego_a4_nightly')

    def test_versions_are_nightly_release_candidates(self):
        self.assertEqual(
            nightly_targets.version_for('1.5.7', 'sticky', 'global', '12345678'),
            '1.5.7-sticky-rc+1234567',
        )
        self.assertEqual(
            nightly_targets.version_for('1.5.7', 'sticky', 'zh-CN', '12345678'),
            '1.5.7-sticky-rc+1234567',
        )
        self.assertNotIn('beta', nightly_targets.version_for('1.5.7', 'sticky', 'global', '1234567'))

    def test_workflow_packages_one_binary_set(self):
        workflow = (ROOT / '.github/workflows/nightly.yml').read_text()
        self.assertIn("find artifacts -type f -print", workflow)
        self.assertNotIn("find artifacts -path '*/global/*'", workflow)
        self.assertEqual(workflow.count('python3 scripts/package_nightly_target.py "${{ matrix.targetId }}"'), 1)
        self.assertNotIn('for flavor in global cn', workflow)
        self.assertIn('cp "$c3_artifact/xteink-firmware.bin" firmware.bin', workflow)
        self.assertIn('gh release delete-asset nightly firmware-cn.bin', workflow)

    def test_package_contains_one_binary_set_and_two_compatible_manifests(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            build = root / '.pio/build/sticky_nightly'
            build.mkdir(parents=True)
            (build / 'bootloader.bin').write_bytes(b'bootloader')
            (build / 'partitions.bin').write_bytes(b'partitions')
            (build / 'firmware.bin').write_bytes(self.write_image(board='sticky').read_bytes())
            boot_app0 = root / 'boot_app0.bin'
            boot_app0.write_bytes(b'boot_app0')
            (root / 'platformio.ini').write_text('[crosspoint]\nversion = 1.5.7\n')
            output = root / 'dist/sticky'

            def git_value(_root, *args):
                return 'a' * (7 if '--short=7' in args else 40)

            with (
                mock.patch.object(package_nightly_target, 'verify_partition_csv'),
                mock.patch.object(package_nightly_target, 'find_boot_app0', return_value=boot_app0),
                mock.patch.object(package_nightly_target, 'git_value', side_effect=git_value),
            ):
                package_nightly_target.package_target(root, 'sticky', output)

            self.assertEqual(
                {path.name for path in output.iterdir()},
                {
                    'sticky-bootloader.bin',
                    'sticky-partitions.bin',
                    'sticky-boot_app0.bin',
                    'sticky-firmware.bin',
                    'sticky-global-manifest.json',
                    'sticky-cn-manifest.json',
                    'sticky-SHA256SUMS',
                },
            )
            manifests = [
                json.loads((output / nightly_targets.manifest_name('sticky', flavor)).read_text())
                for flavor in nightly_targets.FLAVOR_TOKENS
            ]
            self.assertEqual(manifests[0]['assets'], manifests[1]['assets'])
            self.assertEqual(
                {key: value for key, value in manifests[0].items() if key != 'flavor'},
                {key: value for key, value in manifests[1].items() if key != 'flavor'},
            )

    def test_publish_jobs_keep_credentials_scoped(self):
        workflow = (ROOT / '.github/workflows/nightly.yml').read_text()
        github_job, china_job = workflow.split('  publish_cn:\n')
        github_job = github_job.split('  publish_github:\n')[1]
        self.assertIn('runs-on: ubuntu-latest', github_job)
        self.assertNotIn('COS_SECRET_', github_job)
        self.assertIn('runs-on: [self-hosted, Linux, X64, h2o]', china_job)
        self.assertIn('persist-credentials: false', china_job)
        self.assertNotIn("select_runner.outputs['runs-on']", china_job)
        self.assertNotIn('GH_TOKEN:', china_job)

    def test_coscli_is_verified_before_publishing(self):
        workflow = (ROOT / '.github/workflows/nightly.yml').read_text()
        china_job = workflow.split('  publish_cn:\n')[1]
        self.assertIn('coscli-v1.0.8-linux-amd64', workflow)
        self.assertIn(
            '7165f2ae16c5f7ac495864c963ca574a76e04ec72680d7bc8a8eee3234d8cf91', workflow
        )
        self.assertLess(
            china_job.index('Install COS CLI'), china_job.index('Publish immutable COS objects')
        )
        self.assertLess(
            china_job.index('Publish immutable COS objects'),
            china_job.index('Publish rolling China index last'),
        )
        self.assertNotIn('coscli config add', workflow)
        self.assertNotIn('/usr/local/bin/coscli', china_job)
        self.assertIn('"$RUNNER_TEMP/coscli" cp', china_job)
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
                'environment': nightly_targets.environment_for(target_id, flavor),
                'chip': target['chip'],
                'flavor': flavor,
                'version': f'1.5.7-rc+{revision[:7]}',
                'crossmuxSha': revision,
                'sdkSha': sdk_revision,
                'assets': [{
                    'role': 'firmware',
                    'name': nightly_targets.asset_name(target_id, 'firmware.bin'),
                    'sha256': 'd' * 64,
                }],
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

    def test_china_variants_share_one_target_directory_and_binary(self):
        self.write_pair('sticky')
        index = build_nightly_index.build_index(
            self.root, None, 'cn', 'https://assets.example/firmware/builds/test/', 'now', 'test'
        )
        variants = index['targets']['sticky']['variants']
        self.assertEqual(
            variants['global']['manifestUrl'],
            'https://assets.example/firmware/builds/test/sticky/sticky-global-manifest.json',
        )
        self.assertEqual(
            variants['zh-CN']['manifestUrl'],
            'https://assets.example/firmware/builds/test/sticky/sticky-cn-manifest.json',
        )
        manifests = [
            json.loads((self.root / nightly_targets.manifest_name('sticky', flavor)).read_text())
            for flavor in nightly_targets.FLAVOR_TOKENS
        ]
        self.assertEqual(manifests[0]['assets'], manifests[1]['assets'])

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

    def test_rejects_mismatched_asset_pair(self):
        self.write_pair('sticky')
        chinese = self.root / nightly_targets.manifest_name('sticky', 'zh-CN')
        manifest = json.loads(chinese.read_text())
        manifest['assets'][0]['sha256'] = 'e' * 64
        chinese.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'assets do not match'):
            build_nightly_index.build_index(
                self.root, None, 'global', 'https://example.com/', 'now', 'test'
            )


if __name__ == '__main__':
    unittest.main()
