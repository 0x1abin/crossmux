import hashlib
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
import nightly_retention
import nightly_targets
import package_nightly_target
import verify_nightly_release


class NightlyTargetTest(unittest.TestCase):
    def test_fetch_retries_incomplete_reads(self):
        response = mock.MagicMock()
        response.__enter__.return_value.read.side_effect = [
            verify_nightly_release.http.client.IncompleteRead(b'partial', 1),
            b'complete',
        ]
        with mock.patch.object(
            verify_nightly_release.urllib.request, 'urlopen', return_value=response
        ) as urlopen, mock.patch.object(verify_nightly_release.time, 'sleep'):
            self.assertEqual(
                verify_nightly_release.fetch_bytes('https://assets.crossmux.cn/asset.bin'),
                b'complete',
            )
        self.assertEqual(urlopen.call_count, 2)

    def test_matrix_has_c3_and_six_s3_targets(self):
        matrix = package_nightly_target.matrix('nightly')['include']
        self.assertEqual(len(matrix), 7)
        self.assertEqual(
            {entry['targetId'] for entry in matrix},
            set(nightly_targets.TARGETS),
        )
        environments = {entry['environment'] for entry in matrix}
        self.assertEqual(len(environments), 7)
        self.assertEqual(
            package_nightly_target.matrix('stable')['include'],
            [{'targetId': 'xteink_x4', 'deviceSlug': 'xteink', 'environment': 'gh_release'}],
        )

    def test_runtime_models_and_board_tags_are_explicit(self):
        targets = nightly_targets.TARGETS
        self.assertEqual(targets['xteink_x4']['models'], ['xteink_x3', 'xteink_x4'])
        self.assertEqual(targets['xteink_x4_pro']['boardTag'], 'x4pro')
        self.assertEqual(targets['m5stack_paper_mono']['boardTag'], 'papermono')
        self.assertEqual(targets['eego_a4']['environments']['nightly'], 'eego_a4_nightly')

    def test_versions_are_nightly_release_candidates(self):
        self.assertEqual(
            nightly_targets.version_for('1.5.7', 'sticky', 'nightly', 'global', '12345678'),
            '1.5.7-sticky-rc+1234567',
        )
        self.assertEqual(
            nightly_targets.version_for('1.5.7', 'sticky', 'nightly', 'zh-CN', '12345678'),
            '1.5.7-sticky-rc+1234567',
        )
        self.assertNotIn(
            'beta',
            nightly_targets.version_for('1.5.7', 'sticky', 'nightly', 'global', '1234567'),
        )
        self.assertEqual(
            nightly_targets.version_for('1.5.8', 'xteink_x4', 'stable', 'global', '1234567'),
            '1.5.8',
        )

    def test_workflow_packages_one_binary_set(self):
        workflow = (ROOT / '.github/workflows/nightly.yml').read_text()
        hardware_workflow = (ROOT / '.github/workflows/hardware-ci.yml').read_text()
        self.assertIn("find artifacts -type f -print", workflow)
        self.assertNotIn("find artifacts -path '*/global/*'", workflow)
        self.assertEqual(
            workflow.count('python3 scripts/package_nightly_target.py "${{ matrix.targetId }}"'), 1
        )
        self.assertIn('--channel "${{ needs.prepare.outputs.channel }}"', workflow)
        self.assertNotIn('for flavor in global cn', workflow)
        self.assertIn('cp artifacts/xteink-firmware.bin firmware.bin', workflow)
        self.assertIn('cp artifacts/xteink-firmware.bin firmware-cn.bin', workflow)
        self.assertIn('gh release delete-asset "$CHANNEL" firmware-cn.bin', workflow)
        self.assertNotIn('--flavor', hardware_workflow)
        self.assertIn('(cd "dist/nightly/$target" && sha256sum --check *-SHA256SUMS)', hardware_workflow)

    def test_workflow_fails_closed_and_verifies_both_regions(self):
        workflow = (ROOT / '.github/workflows/nightly.yml').read_text()
        self.assertIn("group: firmware-${{ github.event_name == 'push'", workflow)
        self.assertIn("if: needs.prepare.outputs.channel == 'stable'", workflow)
        self.assertEqual(workflow.count("if: needs.prepare.outputs.channel == 'nightly'"), 6)
        self.assertNotIn("if: always() && needs.prepare.result == 'success'", workflow)
        self.assertNotIn('--dir previous/global || true', workflow)
        self.assertNotIn('-o previous/cn.json || true', workflow)
        self.assertNotIn('--previous previous/', workflow)
        self.assertNotIn('name: nightly-previous-', workflow)
        rolling = workflow.split('- name: Publish rolling global index last', 1)[1].split(
            '  publish_cn:', 1
        )[0]
        self.assertGreater(rolling.index('legacy_assets='), rolling.index('xteink-firmware.bin'))
        verify = workflow.split('  verify_publish:', 1)[1]
        self.assertIn('needs: [prepare, publish_github, publish_cn]', verify)
        self.assertEqual(verify.count('python3 scripts/verify_nightly_release.py'), 2)
        self.assertIn(
            '  cleanup_github:\n    if:', workflow
        )
        self.assertIn('  cleanup_cn:\n    if:', workflow)
        self.assertEqual(workflow.count('python3 scripts/nightly_retention.py'), 2)
        self.assertIn('gh release delete "$build_tag"', workflow)
        self.assertIn('cos://${COS_BUCKET}/firmware/builds/${build_id}/', workflow)
        self.assertIn('--cleanup-tag --yes', workflow)
        self.assertIn('--recursive --force', workflow)
        cleanup = workflow.split('- name: Delete obsolete COS Nightly builds', 1)[1]
        versioning = cleanup.split('versioning=', 1)[1].split('list_args=', 1)[0]
        self.assertIn('"${cos_args[@]}"', versioning)
        self.assertIn('cos:GetBucketVersioning', versioning)
        self.assertIn('2>&1', versioning)

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
                package_nightly_target.package_target(root, 'sticky', 'nightly', output)

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

    def test_stable_package_has_release_version_and_legacy_migration_roles(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            build = root / '.pio/build/gh_release'
            build.mkdir(parents=True)
            (build / 'bootloader.bin').write_bytes(b'bootloader')
            (build / 'partitions.bin').write_bytes(b'partitions')
            (build / 'firmware.bin').write_bytes(
                self.write_image(chip_id=0x0005, board='x4').read_bytes()
            )
            (root / 'platformio.ini').write_text('[crosspoint]\nversion = 1.5.8\n')
            output = root / 'dist/xteink_x4'

            def git_value(_root, *args):
                return 'a' * (7 if '--short=7' in args else 40)

            with (
                mock.patch.object(package_nightly_target, 'verify_partition_csv'),
                mock.patch.object(package_nightly_target, 'git_value', side_effect=git_value),
            ):
                package_nightly_target.package_target(root, 'xteink_x4', 'stable', output)

            manifest = json.loads((output / 'xteink-global-manifest.json').read_text())
            self.assertEqual(manifest['channel'], 'stable')
            self.assertEqual(manifest['version'], '1.5.8')
            self.assertEqual(manifest['environment'], 'gh_release')
            self.assertEqual(
                [asset['role'] for asset in manifest['assets']],
                ['bootloader', 'partitions', 'firmware'],
            )

    def test_publish_jobs_keep_credentials_scoped(self):
        workflow = (ROOT / '.github/workflows/nightly.yml').read_text()
        github_job, china_job = workflow.split('  publish_cn:\n')
        github_job = github_job.split('  publish_github:\n')[1]
        china_job = china_job.split('  verify_publish:\n')[0]
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

    def test_cleanup_reuses_the_published_attempt_and_lists_current_cos_objects_only(self):
        workflow = (ROOT / '.github/workflows/nightly.yml').read_text()
        self.assertIn('BUILD_TAG: ${{ needs.prepare.outputs.build_tag }}', workflow)
        self.assertIn('BUILD_ID: ${{ needs.prepare.outputs.build_id }}', workflow)
        cleanup = workflow.split('  cleanup_cn:', 1)[1]
        self.assertNotIn('list_args+=(--recursive --all-versions)', cleanup)
        self.assertIn('rm_args+=(--all-versions)', cleanup)
        self.assertIn('-name error.report -exec cat {} +', workflow)
        self.assertNotIn('--disable-log', cleanup)
        listing = cleanup.split('if ! "$RUNNER_TEMP/coscli" ls', 1)[1].split(
            'python3 scripts/nightly_retention.py', 1
        )[0]
        self.assertIn('"${cos_args[@]}"', listing)
        self.assertIn('cos:GetBucket', listing)
        self.assertIn('2>&1', listing)

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

    def write_pair(
        self, target_id, revision='a' * 40, sdk_revision='b' * 40, channel='nightly'
    ):
        target = nightly_targets.TARGETS[target_id]
        for flavor in nightly_targets.FLAVOR_TOKENS:
            manifest = {
                'schemaVersion': 1,
                'channel': channel,
                'targetId': target_id,
                'models': target['models'],
                'deviceSlug': target['deviceSlug'],
                'boardTag': target['boardTag'],
                'supportedChannels': target['supportedChannels'],
                'environment': nightly_targets.environment_for(target_id, channel, flavor),
                'chip': target['chip'],
                'flavor': flavor,
                'version': '1.5.8' if channel == 'stable' else f'1.5.7-rc+{revision[:7]}',
                'crossmuxSha': revision,
                'sdkSha': sdk_revision,
                'assets': [{
                    'role': 'firmware',
                    'name': nightly_targets.asset_name(target_id, 'firmware.bin'),
                    'sha256': 'd' * 64,
                }],
            }
            (self.root / nightly_targets.manifest_name(target_id, flavor)).write_text(json.dumps(manifest))

    def write_all_pairs(self, revision='a' * 40, sdk_revision='b' * 40, channel='nightly'):
        for target_id in nightly_targets.targets_for(channel):
            self.write_pair(target_id, revision, sdk_revision, channel)

    def test_builds_complete_index(self):
        self.write_all_pairs()
        index = build_nightly_index.build_index(
            self.root,
            'global',
            'https://example.com/nightly/',
            '2026-08-26T00:00:00Z',
            'nightly-test',
            'nightly',
        )
        self.assertEqual(set(index['targets']), set(nightly_targets.TARGETS))
        self.assertEqual(
            index['targets']['sticky']['variants']['zh-CN']['manifestUrl'],
            'https://example.com/nightly/sticky-cn-manifest.json',
        )

    def test_china_variants_share_one_target_directory_and_binary(self):
        self.write_all_pairs()
        index = build_nightly_index.build_index(
            self.root, 'cn', 'https://assets.example/firmware/builds/test/', 'now', 'test', 'nightly'
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

    def test_stable_index_requires_notes_and_contains_only_x3_x4(self):
        self.write_all_pairs(channel='stable')
        with self.assertRaisesRegex(ValueError, 'Stable release notes are required'):
            build_nightly_index.build_index(
                self.root, 'global', 'https://example.com/', 'now', 'test', 'stable'
            )
        notes = {'en': ['One', 'Two'], 'zh': ['一', '二']}
        index = build_nightly_index.build_index(
            self.root, 'global', 'https://example.com/', 'now', 'test', 'stable', notes
        )
        self.assertEqual(set(index['targets']), {'xteink_x4'})
        self.assertEqual(index['releaseNotes'], {'global': notes['en'], 'zh-CN': notes['zh']})

    def test_rejects_incomplete_target_set(self):
        self.write_all_pairs()
        (self.root / nightly_targets.manifest_name('sticky', 'zh-CN')).unlink()
        with self.assertRaisesRegex(ValueError, 'expected one sticky/zh-CN manifest'):
            build_nightly_index.build_index(
                self.root, 'cn', 'https://assets.example/firmware/builds/test/', 'now', 'test', 'nightly'
            )

    def test_rejects_mismatched_sdk_pair(self):
        self.write_all_pairs()
        chinese = self.root / nightly_targets.manifest_name('sticky', 'zh-CN')
        manifest = json.loads(chinese.read_text())
        manifest['sdkSha'] = 'c' * 40
        chinese.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'SDK revisions do not match'):
            build_nightly_index.build_index(
                self.root, 'global', 'https://example.com/', 'now', 'test', 'nightly'
            )

    def test_rejects_mismatched_asset_pair(self):
        self.write_all_pairs()
        chinese = self.root / nightly_targets.manifest_name('sticky', 'zh-CN')
        manifest = json.loads(chinese.read_text())
        manifest['assets'][0]['sha256'] = 'e' * 64
        chinese.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'assets do not match'):
            build_nightly_index.build_index(
                self.root, 'global', 'https://example.com/', 'now', 'test', 'nightly'
            )

    def test_rejects_mixed_target_revisions(self):
        self.write_all_pairs()
        self.write_pair('sticky', revision='c' * 40)
        with self.assertRaisesRegex(ValueError, 'target CrossMux revisions do not match'):
            build_nightly_index.build_index(
                self.root, 'global', 'https://example.com/', 'now', 'test', 'nightly'
            )


class NightlyRetentionTest(unittest.TestCase):
    def previous_index(self, storage, first_build, second_build):
        targets = {}
        for index, target_id in enumerate(nightly_targets.TARGETS):
            build = second_build if index == 0 else first_build
            if storage == 'github':
                base = f'https://github.com/0x1abin/crossmux/releases/download/{build}/'
            else:
                base = f'https://assets.crossmux.cn/firmware/builds/{build}/{target_id}/'
            targets[target_id] = {
                'targetId': target_id,
                'variants': {
                    flavor: {
                        'manifestUrl': base + nightly_targets.manifest_name(target_id, flavor)
                    }
                    for flavor in nightly_targets.FLAVOR_TOKENS
                },
            }
        return {'schemaVersion': 1, 'channel': 'nightly', 'targets': targets}

    def test_github_keeps_current_and_every_build_in_previous_index(self):
        current = f'nightly-build-{"a" * 40}-10-1'
        previous = f'nightly-build-{"b" * 40}-9-1'
        previous_fallback = f'nightly-build-{"c" * 40}-8-1'
        obsolete = f'nightly-build-{"d" * 40}-7-1'
        candidates = '\n'.join((current, previous, previous_fallback, obsolete, 'v1.5.7'))
        self.assertEqual(
            nightly_retention.obsolete_builds(
                'github',
                current,
                self.previous_index('github', previous, previous_fallback),
                candidates,
            ),
            [obsolete],
        )

    def test_cos_extracts_build_directories_from_listing(self):
        current = f'nightly-build-{"a" * 40}-10-1'
        previous = f'{"b" * 40}-9-1'
        previous_fallback = f'nightly-build-{"c" * 40}-8-1'
        obsolete = f'nightly-build-{"d" * 40}-7-1'
        candidates = '\n'.join(
            f'firmware/builds/{build}/ | DIR'
            for build in (current, previous, previous_fallback, obsolete)
        )
        self.assertEqual(
            nightly_retention.obsolete_builds(
                'cos',
                current,
                self.previous_index('cos', previous, previous_fallback),
                candidates,
            ),
            [obsolete],
        )

    def test_rejects_unexpected_previous_url_and_incomplete_listing(self):
        current = f'nightly-build-{"a" * 40}-10-1'
        previous = f'nightly-build-{"b" * 40}-9-1'
        index = self.previous_index('github', previous, previous)
        index['targets']['sticky']['variants']['global']['manifestUrl'] = (
            'https://example.com/firmware.bin'
        )
        with self.assertRaisesRegex(ValueError, 'unexpected previous sticky/global'):
            nightly_retention.obsolete_builds('github', current, index, current)
        with self.assertRaisesRegex(ValueError, 'missing from the candidate list'):
            nightly_retention.obsolete_builds(
                'github', current, self.previous_index('github', previous, previous), previous
            )


class PublishedNightlyTest(unittest.TestCase):
    def setUp(self):
        self.index_url = (
            'https://github.com/0x1abin/crossmux/releases/download/nightly/release-index.json'
        )
        self.release_url = (
            'https://github.com/0x1abin/crossmux/releases/download/nightly-build-test/'
        )
        self.current_sha = 'a' * 40
        self.old_sha = 'c' * 40
        self.sdk_sha = 'b' * 40
        self.store = {}
        self.fetches = {}
        targets = {}
        for target_id, target in nightly_targets.TARGETS.items():
            revision = self.current_sha
            assets = []
            for role, name, offset in verify_nightly_release.expected_assets(target_id, 'nightly'):
                data = f'{target_id}/{role}'.encode()
                self.store[self.release_url + name] = data
                assets.append({
                    'role': role,
                    'name': name,
                    'offset': offset,
                    'size': len(data),
                    'sha256': hashlib.sha256(data).hexdigest(),
                })
            variants = {}
            for flavor in nightly_targets.FLAVOR_TOKENS:
                version = f'1.5.7-rc+{revision[:7]}'
                manifest = {
                    'schemaVersion': 1,
                    'channel': 'nightly',
                    'targetId': target_id,
                    'models': target['models'],
                    'deviceSlug': target['deviceSlug'],
                    'boardTag': target['boardTag'],
                    'supportedChannels': target['supportedChannels'],
                    'environment': nightly_targets.environment_for(target_id, 'nightly', flavor),
                    'flavor': flavor,
                    'version': version,
                    'crossmuxSha': revision,
                    'sdkSha': self.sdk_sha,
                    'assets': assets,
                }
                url = self.release_url + nightly_targets.manifest_name(target_id, flavor)
                self.store[url] = json.dumps(manifest).encode()
                variants[flavor] = {
                    'version': version,
                    'crossmuxSha': revision,
                    'sdkSha': self.sdk_sha,
                    'publishedAt': 'now',
                    'manifestUrl': url,
                }
            targets[target_id] = {
                'targetId': target_id,
                'models': target['models'],
                'deviceSlug': target['deviceSlug'],
                'boardTag': target['boardTag'],
                'supportedChannels': target['supportedChannels'],
                'variants': variants,
            }
        self.index = {
            'schemaVersion': 1,
            'channel': 'nightly',
            'updatedAt': 'now',
            'buildId': 'test',
            'targets': targets,
        }
        self.write_index()

    def write_index(self):
        self.store[self.index_url] = json.dumps(self.index).encode()

    def fetch(self, url):
        self.fetches[url] = self.fetches.get(url, 0) + 1
        return self.store[url]

    def test_verifies_complete_current_release_with_one_asset_fetch(self):
        result = verify_nightly_release.verify_release(
            self.index_url, self.current_sha, 'nightly', self.fetch
        )
        self.assertEqual(result['targets'], len(nightly_targets.TARGETS))
        self.assertEqual(result['currentTargets'], len(nightly_targets.TARGETS))
        for url in self.store:
            if url.endswith('.bin'):
                self.assertEqual(self.fetches[url], 1)

    def test_rejects_target_from_previous_revision(self):
        target_id = 'xteink_x4'
        for flavor in nightly_targets.FLAVOR_TOKENS:
            url = self.release_url + nightly_targets.manifest_name(target_id, flavor)
            manifest = json.loads(self.store[url])
            manifest['crossmuxSha'] = self.old_sha
            self.store[url] = json.dumps(manifest).encode()
            self.index['targets'][target_id]['variants'][flavor]['crossmuxSha'] = self.old_sha
        self.write_index()
        with self.assertRaisesRegex(ValueError, 'does not point to the current revision'):
            verify_nightly_release.verify_release(self.index_url, self.current_sha, 'nightly', self.fetch)

    def test_rejects_missing_target(self):
        self.index['targets'].pop('sticky')
        self.write_index()
        with self.assertRaisesRegex(ValueError, 'canonical target set'):
            verify_nightly_release.verify_release(self.index_url, self.current_sha, 'nightly', self.fetch)

    def test_rejects_manifest_difference(self):
        url = self.release_url + nightly_targets.manifest_name('sticky', 'zh-CN')
        manifest = json.loads(self.store[url])
        manifest['unexpected'] = True
        self.store[url] = json.dumps(manifest).encode()
        with self.assertRaisesRegex(ValueError, 'differ beyond flavor'):
            verify_nightly_release.verify_release(self.index_url, self.current_sha, 'nightly', self.fetch)

    def test_rejects_corrupt_asset(self):
        url = self.release_url + nightly_targets.asset_name('sticky', 'firmware.bin')
        self.store[url] += b'corrupt'
        with self.assertRaisesRegex(ValueError, 'size or SHA-256'):
            verify_nightly_release.verify_release(self.index_url, self.current_sha, 'nightly', self.fetch)

if __name__ == '__main__':
    unittest.main()
