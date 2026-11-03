import configparser
import hashlib
import io
import struct
import subprocess
import json
import re
import sys
import tempfile
import unittest
import zipfile
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


def bundle_bytes(target_id, contents):
    checksum_name = f"{nightly_targets.TARGETS[target_id]['deviceSlug']}-SHA256SUMS"
    payloads = dict(contents)
    if checksum_name not in payloads:
        payloads[checksum_name] = ''.join(
            f'{hashlib.sha256(data).hexdigest()}  {name}\n' for name, data in payloads.items()
        ).encode()
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in payloads.items():
            archive.writestr(name, data)
    return stream.getvalue()


class NightlyTargetTest(unittest.TestCase):
    def test_boot_app0_uses_the_active_platformio_core(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            home = root / 'home'
            custom_core = root / 'isolated-core'
            relative = Path('packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin')
            default_asset = home / '.platformio' / relative
            custom_asset = custom_core / relative
            for asset in (default_asset, custom_asset):
                asset.parent.mkdir(parents=True)
                asset.write_bytes(b'boot_app0')
            with mock.patch.object(Path, 'home', return_value=home):
                for override, expected in (('', default_asset), (str(custom_core), custom_asset)):
                    with self.subTest(core_dir=override), mock.patch.dict(
                        'os.environ', {'PLATFORMIO_CORE_DIR': override}
                    ):
                        self.assertEqual(package_nightly_target.find_boot_app0(), expected)
                with mock.patch.dict('os.environ', {}, clear=True):
                    self.assertEqual(package_nightly_target.find_boot_app0(), default_asset)
                custom_asset.unlink()
                with mock.patch.dict('os.environ', {'PLATFORMIO_CORE_DIR': str(custom_core)}):
                    with self.assertRaises(SystemExit) as error:
                        package_nightly_target.find_boot_app0()
                    self.assertIn(str(custom_asset), str(error.exception))

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

    def test_exact_channel_release_targets(self):
        stable = {'xteink_x4', 'sticky', 'xteink_x4_pro', 'metalio_eink4'}
        for channel in nightly_targets.CHANNELS:
            expected = stable if channel == 'stable' else set(nightly_targets.TARGETS)
            matrix = package_nightly_target.matrix(channel)['include']
            with self.subTest(channel=channel):
                self.assertEqual({entry['targetId'] for entry in matrix}, expected)
                self.assertEqual(len(matrix), len(expected))
                self.assertEqual(len({entry['environment'] for entry in matrix}), len(expected))

    def test_nightly_only_targets_cannot_be_packaged_as_stable(self):
        for target_id in set(nightly_targets.TARGETS) - {'xteink_x4', 'sticky', 'xteink_x4_pro', 'metalio_eink4'}:
            with self.subTest(target=target_id), tempfile.TemporaryDirectory() as temp:
                output = Path(temp) / 'output'
                with self.assertRaisesRegex(SystemExit, 'does not support the stable channel'):
                    package_nightly_target.package_target(Path(temp), target_id, 'stable', output)
                self.assertFalse(output.exists())

    def test_runtime_models_and_board_tags_are_explicit(self):
        targets = nightly_targets.TARGETS
        self.assertEqual(targets['xteink_x4']['models'], ['xteink_x3', 'xteink_x4'])
        self.assertEqual(targets['xteink_x4_pro']['boardTag'], 'x4pro')
        self.assertEqual(targets['m5stack_paper_mono']['boardTag'], 'papermono')
        self.assertEqual(targets['eego_a4']['environments']['nightly'], 'eego_a4_nightly')
        self.assertEqual(targets['metalio_eink4']['models'], ['metalio_eink4'])
        self.assertEqual(targets['metalio_eink4']['boardTag'], 'metalio_eink4')
        self.assertEqual(targets['metalio_eink4']['deviceSlug'], 'metalio-eink4')
        self.assertTrue(targets['metalio_eink4']['fullInstall'])
        self.assertEqual(nightly_targets.environment_for('metalio_eink4', 'nightly', 'global'),
                         nightly_targets.environment_for('metalio_eink4', 'nightly', 'zh-CN'))

    def test_readpico_release_contract(self):
        target = nightly_targets.TARGETS['readpico']
        self.assertEqual(target['models'], ['readpico', 'read_pico'])
        self.assertEqual(target['chip'], 'ESP32-S3')
        self.assertEqual(target['boardTag'], 'readpico')
        self.assertEqual(target['supportedChannels'], ['nightly'])
        for flavor in nightly_targets.FLAVOR_TOKENS:
            self.assertEqual(nightly_targets.environment_for('readpico', 'nightly', flavor), 'readpico_nightly')
            self.assertEqual(nightly_targets.environment_for('readpico', 'stable', flavor), 'readpico-gh_release')
            self.assertEqual(nightly_targets.version_for('1.6.0', 'readpico', 'nightly', flavor, '12345678'),
                             '1.6.0-rc+1234567')
        config = configparser.ConfigParser()
        config.read(ROOT / 'platformio.ini')
        environment = config['env:readpico_nightly']
        self.assertIn('readpico_hardware', environment['extends'])
        self.assertNotIn('CROSSPOINT_WAIT_FOR_USB_SERIAL', environment.get('build_flags', ''))

    def test_versions_are_nightly_release_candidates(self):
        self.assertEqual(
            nightly_targets.version_for('1.5.7', 'sticky', 'nightly', 'global', '12345678'),
            '1.5.7-rc+1234567',
        )
        self.assertEqual(
            nightly_targets.version_for('1.5.7', 'sticky', 'nightly', 'zh-CN', '12345678'),
            '1.5.7-rc+1234567',
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
        self.assertIn('pattern: firmware-stable-*', workflow)
        self.assertIn('merge-multiple: true', workflow)
        self.assertIn("find artifacts -type f -name '*.zip' -print", workflow)
        self.assertIn('assets=(artifacts/*.zip release-index.json)', workflow)
        self.assertNotIn('cp artifacts/xteink-', workflow)
        self.assertNotIn('firmware-cn.bin', workflow)
        self.assertIn('from nightly_targets import matrix', hardware_workflow)
        self.assertIn('from nightly_targets import targets_for', hardware_workflow)
        self.assertIn('-e x4c ', hardware_workflow)
        self.assertNotIn('x4c_nightly', hardware_workflow)
        self.assertNotIn('--flavor', hardware_workflow)
        self.assertIn('(cd "dist/nightly/$target" && sha256sum --check *-SHA256SUMS)', hardware_workflow)

    def test_workflow_fails_closed_and_verifies_both_regions(self):
        workflow = (ROOT / '.github/workflows/nightly.yml').read_text()
        self.assertIn("group: firmware-${{ github.event_name == 'push'", workflow)
        self.assertIn("!inputs.dry_run && needs.prepare.outputs.channel == 'stable'", workflow)
        self.assertEqual(workflow.count("if: needs.prepare.outputs.channel == 'nightly'"), 4)
        self.assertNotIn("if: always() && needs.prepare.result == 'success'", workflow)
        self.assertNotIn('--dir previous/global || true', workflow)
        self.assertNotIn('-o previous/cn.json || true', workflow)
        self.assertNotIn('--previous previous/', workflow)
        self.assertNotIn('name: nightly-previous-', workflow)
        rolling = workflow.split('- name: Publish rolling global index last', 1)[1].split(
            '  publish_cn:', 1
        )[0]
        self.assertLess(rolling.index('"${bundles[@]}"'), rolling.index('upload "$CHANNEL" release-index.json'))
        self.assertNotIn('delete-asset', rolling)
        verify = workflow.split('  verify_publish:', 1)[1]
        self.assertIn('needs: [prepare, publish_github, publish_cn]', verify)
        self.assertEqual(verify.count('python3 scripts/verify_nightly_release.py'), 2)
        self.assertGreater(verify.index('Retire obsolete rolling assets'), verify.rindex('python3 scripts/verify_nightly_release.py'))
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

    def test_workflow_dry_run_skips_every_publish_and_cleanup_job(self):
        workflow = (ROOT / '.github/workflows/nightly.yml').read_text()
        self.assertRegex(workflow, r'dry_run:\n(?:.*\n){2}        type: boolean\n        default: false')
        self.assertIn("inputs.dry_run && format('dry-run-{0}', github.ref) || 'publish'", workflow)
        self.assertIn('"${GITHUB_SHA::7}" "$GITHUB_REF_NAME" > release-body.md', workflow)
        jobs = dict(re.findall(r'^  ([a-z_]+):\n(.*?)(?=^  [a-z_]+:|\Z)', workflow, re.M | re.S))
        publishing = {
            'publish_github': None, 'publish_cn': None, 'verify_publish': None,
            'publish_stable_release': 'stable', 'cleanup_github': 'nightly', 'cleanup_cn': 'nightly',
        }
        for name, channel in publishing.items():
            expected = '!inputs.dry_run'
            if channel:
                expected += f" && needs.prepare.outputs.channel == '{channel}'"
            self.assertTrue(jobs[name].startswith('    if: ${{ ' + expected + ' }}\n'), name)
            condition = expected.replace('!inputs.dry_run', 'not dry_run').replace(
                'needs.prepare.outputs.channel', 'actual_channel'
            ).replace('&&', 'and')
            for dry_run, actual_channel in ((True, 'nightly'), (False, 'nightly'), (False, 'stable')):
                with self.subTest(job=name, dry_run=dry_run, channel=actual_channel):
                    self.assertEqual(
                        eval(condition, {}, {'dry_run': dry_run, 'actual_channel': actual_channel}),
                        not dry_run and (channel is None or actual_channel == channel),
                    )
        for name, body in jobs.items():
            if re.search(r'gh release (?:create|upload|edit|delete)|coscli" (?:cp|rm)', body):
                self.assertIn(name, publishing, f'unguarded external write in {name}')
        validation = jobs['validate']
        self.assertIn('needs: [prepare, notes, build]', validation)
        self.assertIn('for region in global cn; do', validation)
        self.assertIn('--index-file "$index" --artifacts artifacts', validation)
        self.assertIn('name: global-release-index', validation)
        self.assertIn('name: cn-release-index', validation)
        self.assertNotIn('    if:', validation)
        self.assertNotIn('GH_TOKEN:', validation)
        self.assertNotIn('COS_SECRET_', validation)
        for name in ('publish_github', 'publish_cn'):
            self.assertIn('needs: [prepare, notes, validate]', jobs[name])

    def test_cos_target_directories_keep_hyphenated_platform_ids(self):
        workflow = (ROOT / '.github/workflows/nightly.yml').read_text()
        publish = workflow.split('- name: Publish immutable COS objects', 1)[1].split(
            '- name: Publish rolling China index last', 1
        )[0]
        self.assertIn('CHANNEL: ${{ needs.prepare.outputs.channel }}', publish)
        assignment = next(line.strip() for line in publish.splitlines() if line.strip().startswith('target='))
        for channel in nightly_targets.CHANNELS:
            for target in ('xteink_x4', 'future-reader', 'future-epaper-397'):
                with self.subTest(target=target, channel=channel):
                    result = subprocess.check_output(
                        ['bash', '-c', assignment + '\n' + 'printf "%s" "$target"'],
                        env={'CHANNEL': channel, 'artifact': f'artifacts/firmware-{channel}-{target}'},
                        text=True,
                    )
                    self.assertEqual(result, target)

    def test_all_target_channel_packages_have_one_binary_set_and_compatible_manifests(self):
        for target_id, target in nightly_targets.TARGETS.items():
            for channel in target['supportedChannels']:
                with self.subTest(target=target_id, channel=channel), tempfile.TemporaryDirectory() as temp:
                    root = Path(temp)
                    version = '1.6.0' if channel == 'stable' else '1.6.0-rc+aaaaaaa'
                    slug = target['deviceSlug']
                    build = root / '.pio/build' / nightly_targets.environment_for(target_id, channel, 'global')
                    build.mkdir(parents=True)
                    (build / 'bootloader.bin').write_bytes(b'bootloader')
                    (build / 'partitions.bin').write_bytes(self.partition_image())
                    (build / 'firmware.bin').write_bytes(self.write_image(
                        chip_id=target['chipId'], board=target['boardTag'], version=version
                    ).read_bytes())
                    boot_app0 = root / 'boot_app0.bin'
                    boot_app0.write_bytes(b'boot_app0')
                    (root / 'platformio.ini').write_text('[crosspoint]\nversion = 1.6.0\n')
                    output = root / 'dist' / target_id

                    def git_value(_root, *args):
                        return 'a' * (7 if '--short=7' in args else 40)

                    with (
                        mock.patch.object(package_nightly_target, 'verify_partition_csv'),
                        mock.patch.object(package_nightly_target, 'find_boot_app0', return_value=boot_app0),
                        mock.patch.object(package_nightly_target, 'git_value', side_effect=git_value),
                        mock.patch('builtins.print'),
                    ):
                        package_nightly_target.package_target(root, target_id, channel, output)

                    self.assertEqual(
                        {path.name for path in output.iterdir()},
                        {
                            f'{slug}-bootloader.bin', f'{slug}-partitions.bin',
                            f'{slug}-boot_app0.bin', f'{slug}-firmware.bin',
                            f'{slug}-global-manifest.json', f'{slug}-cn-manifest.json',
                            f'{slug}-SHA256SUMS', nightly_targets.bundle_name(target_id, version),
                        },
                    )
                    manifests = [
                        json.loads((output / nightly_targets.manifest_name(target_id, flavor)).read_text())
                        for flavor in nightly_targets.FLAVOR_TOKENS
                    ]
                    self.assertEqual(
                        {key: value for key, value in manifests[0].items() if key != 'flavor'},
                        {key: value for key, value in manifests[1].items() if key != 'flavor'},
                    )
                    self.assertEqual([asset['role'] for asset in manifests[0]['assets']],
                                     ['bootloader', 'partitions', 'boot_app0', 'firmware'])
                    self.assertEqual(manifests[0]['channel'], channel)
                    self.assertEqual(manifests[0]['version'], version)
                    self.assertEqual(manifests[0]['supportedChannels'], target['supportedChannels'])
                    bundle = output / nightly_targets.bundle_name(target_id, version)
                    with zipfile.ZipFile(bundle) as archive:
                        self.assertEqual(set(archive.namelist()), {path.name for path in output.iterdir()} - {bundle.name})
                        for name in archive.namelist():
                            self.assertEqual(archive.read(name), (output / name).read_bytes())

    def test_stable_c3_package_has_the_same_full_install_roles(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            build = root / '.pio/build/gh_release'
            build.mkdir(parents=True)
            (build / 'bootloader.bin').write_bytes(b'bootloader')
            (build / 'partitions.bin').write_bytes(self.partition_image())
            (build / 'firmware.bin').write_bytes(
                self.write_image(chip_id=0x0005, board='x4', version='1.5.8').read_bytes()
            )
            boot_app0 = root / 'boot_app0.bin'
            boot_app0.write_bytes(b'boot_app0')
            (root / 'platformio.ini').write_text('[crosspoint]\nversion = 1.5.8\n')
            output = root / 'dist/xteink_x4'

            def git_value(_root, *args):
                return 'a' * (7 if '--short=7' in args else 40)

            with (
                mock.patch.object(package_nightly_target, 'verify_partition_csv'),
                mock.patch.object(package_nightly_target, 'find_boot_app0', return_value=boot_app0),
                mock.patch.object(package_nightly_target, 'git_value', side_effect=git_value),
            ):
                package_nightly_target.package_target(root, 'xteink_x4', 'stable', output)

            manifest = json.loads((output / 'xteink-global-manifest.json').read_text())
            self.assertEqual(manifest['channel'], 'stable')
            self.assertEqual(manifest['version'], '1.5.8')
            self.assertEqual(manifest['environment'], 'gh_release')
            self.assertEqual(
                [asset['role'] for asset in manifest['assets']],
                ['bootloader', 'partitions', 'boot_app0', 'firmware'],
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

    def write_image(self, chip_id=0x0009, board='eego_a4', version='1.6.0'):
        image = bytearray(80)
        image[32:36] = (0xabcd5432).to_bytes(4, 'little')
        image[48:80] = version.encode().ljust(32, b'\x00')
        image[0] = 0xE9
        image[12:14] = chip_id.to_bytes(2, 'little')
        image.extend(f'CROSSPOINT-BOARD-V1:{board};'.encode())
        temp = tempfile.NamedTemporaryFile(delete=False)
        temp.write(image)
        temp.close()
        self.addCleanup(Path(temp.name).unlink)
        return Path(temp.name)

    @staticmethod
    def partition_image():
        return b''.join(
            struct.pack('<HBBII16sI', 0x50aa, kind, subtype, offset, size, name.encode(), 0)
            for name, kind, subtype, offset, size in (
                ('otadata', 1, 0, 0xE000, 0x2000),
                ('app0', 0, 0x10, 0x10000, 0x640000),
                ('app1', 0, 0x11, 0x650000, 0x640000),
            )
        )

    def test_partition_binary_rejects_mismatched_and_duplicate_entries(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'partitions.bin'
            valid = self.partition_image()
            path.write_bytes(valid)
            package_nightly_target.verify_partition_binary(path)
            wrong_offset = bytearray(valid)
            wrong_offset[36:40] = (0x20000).to_bytes(4, 'little')
            wrong_type = bytearray(valid)
            wrong_type[34] = 1
            for data in (wrong_offset, wrong_type, valid + valid[:32], valid[:32], b'bad'):
                path.write_bytes(data)
                with self.subTest(data=bytes(data[:8])), self.assertRaises(SystemExit):
                    package_nightly_target.verify_partition_binary(path)

    def test_firmware_must_contain_the_exact_build_version(self):
        image = self.write_image(version='1.6.0-rc+aaaaaaa')
        package_nightly_target.verify_firmware(image, 0x0009, 'eego_a4', expected_version='1.6.0-rc+aaaaaaa')
        for version in ('1.6.0', '1.6.0-rc+bbbbbbb'):
            with self.subTest(version=version), self.assertRaises(SystemExit):
                package_nightly_target.verify_firmware(image, 0x0009, 'eego_a4', expected_version=version)

    def test_metalio_image_rejects_other_boards(self):
        package_nightly_target.verify_firmware(self.write_image(board='metalio_eink4'), 0x0009, 'metalio_eink4')
        with self.assertRaises(SystemExit):
            package_nightly_target.verify_firmware(self.write_image(board='waveshare_epaper_397'),
                                                  0x0009, 'metalio_eink4')

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
                **nightly_targets.public_target(target_id),
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
        contents = {
            nightly_targets.manifest_name(target_id, flavor):
                (self.root / nightly_targets.manifest_name(target_id, flavor)).read_bytes()
            for flavor in nightly_targets.FLAVOR_TOKENS
        }
        bundle = self.root / nightly_targets.bundle_name(target_id, manifest['version'])
        bundle.write_bytes(bundle_bytes(target_id, contents))

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
        target = index['targets']['sticky']
        self.assertEqual(target['devices'], nightly_targets.TARGETS['sticky']['devices'])
        bundle = target['bundle']
        self.assertEqual(bundle['url'], f'https://example.com/nightly/{bundle["name"]}')
        data = (self.root / bundle['name']).read_bytes()
        self.assertEqual(bundle['size'], len(data))
        self.assertEqual(bundle['sha256'], hashlib.sha256(data).hexdigest())

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
        bundle = index['targets']['sticky']['bundle']
        self.assertEqual(bundle['url'], f'https://assets.example/firmware/builds/test/sticky/{bundle["name"]}')

    def test_rejects_missing_bundle(self):
        self.write_all_pairs()
        next(self.root.glob('*-sticky.zip')).unlink()
        with self.assertRaisesRegex(ValueError, 'expected one sticky bundle'):
            build_nightly_index.build_index(self.root, 'global', 'https://example.com/', 'now', 'test', 'nightly')

    def test_rejects_manifest_with_wrong_capabilities(self):
        self.write_all_pairs()
        path = self.root / nightly_targets.manifest_name('sticky', 'global')
        manifest = json.loads(path.read_text())
        manifest['installStrategy'] = 'unknown'
        path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'invalid sticky/global manifest'):
            build_nightly_index.build_index(self.root, 'global', 'https://example.com/', 'now', 'test', 'nightly')

    def test_stable_index_requires_notes_and_contains_eligible_targets(self):
        self.write_all_pairs(channel='stable')
        with self.assertRaisesRegex(ValueError, 'Stable release notes are required'):
            build_nightly_index.build_index(
                self.root, 'global', 'https://example.com/', 'now', 'test', 'stable'
            )
        notes = {'en': ['One', 'Two'], 'zh': ['一', '二']}
        index = build_nightly_index.build_index(
            self.root, 'global', 'https://example.com/', 'now', 'test', 'stable', notes
        )
        self.assertEqual(set(index['targets']), {'xteink_x4', 'sticky', 'xteink_x4_pro', 'metalio_eink4'})
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

    def test_keeps_historical_builds_when_targets_change(self):
        current = f'nightly-build-{"a" * 40}-10-1'
        previous = f'nightly-build-{"b" * 40}-9-1'
        previous_fallback = f'nightly-build-{"c" * 40}-8-1'
        obsolete = f'nightly-build-{"d" * 40}-7-1'
        for storage in ('github', 'cos'):
            candidates = '\n'.join((current, previous, previous_fallback, obsolete, 'stable', 'nightly'))
            for change in ('added', 'retired'):
                with self.subTest(storage=storage, change=change):
                    index = self.previous_index(storage, previous, previous_fallback)
                    if change == 'added':
                        del index['targets']['readpico']
                    else:
                        target = index['targets'].pop('xteink_x4')
                        target['targetId'] = 'retired_device'
                        index['targets']['retired_device'] = target
                    self.assertEqual(
                        nightly_retention.obsolete_builds(storage, current, index, candidates),
                        [obsolete],
                    )

    def test_rejects_malformed_historical_indexes(self):
        current = f'nightly-build-{"a" * 40}-10-1'
        previous = f'nightly-build-{"b" * 40}-9-1'
        cases = (
            (('schemaVersion',), 2),
            (('channel',), 'stable'),
            (('targets',), None),
            (('targets',), []),
            (('targets',), {}),
            (('targets', 'sticky', 'targetId'), 'wrong_target'),
            (('targets', 'sticky', 'variants'), {'global': {}}),
            (('targets', 'sticky', 'variants', 'global'), {}),
            (('targets', 'sticky', 'variants', 'global', 'manifestUrl'), 'https://example.com/firmware.bin'),
        )
        for storage in ('github', 'cos'):
            for path, value in cases:
                with self.subTest(storage=storage, path=path, value=value):
                    index = self.previous_index(storage, previous, previous)
                    entry = index
                    for key in path[:-1]:
                        entry = entry[key]
                    entry[path[-1]] = value
                    with self.assertRaises(ValueError):
                        nightly_retention.obsolete_builds(storage, current, index, current)

    def test_rejects_empty_target_identity(self):
        current = f'nightly-build-{"a" * 40}-10-1'
        previous = f'nightly-build-{"b" * 40}-9-1'
        for storage in ('github', 'cos'):
            index = self.previous_index(storage, previous, previous)
            index['targets'][''] = index['targets'].pop('sticky')
            index['targets']['']['targetId'] = ''
            with self.subTest(storage=storage), self.assertRaisesRegex(ValueError, 'variant set'):
                nightly_retention.obsolete_builds(storage, current, index, current)

    def test_rejects_unexpected_previous_url_and_incomplete_listing(self):
        current = f'nightly-build-{"a" * 40}-10-1'
        previous = f'nightly-build-{"b" * 40}-9-1'
        for storage in ('github', 'cos'):
            with self.subTest(storage=storage):
                index = self.previous_index(storage, previous, previous)
                index['targets']['sticky']['variants']['global']['manifestUrl'] = (
                    'https://example.com/firmware.bin'
                )
                with self.assertRaisesRegex(ValueError, 'unexpected previous sticky/global'):
                    nightly_retention.obsolete_builds(storage, current, index, current)
                with self.assertRaisesRegex(ValueError, 'missing from the candidate list'):
                    nightly_retention.obsolete_builds(
                        storage, current, self.previous_index(storage, previous, previous), previous
                    )


class PublishedNightlyTest(unittest.TestCase):
    def setUp(self):
        self.prepare_release('nightly')

    def prepare_release(self, channel):
        self.index_url = f'https://github.com/0x1abin/crossmux/releases/download/{channel}/release-index.json'
        self.release_url = f'https://github.com/0x1abin/crossmux/releases/download/{channel}-build-test/'
        self.current_sha = 'a' * 40
        self.old_sha = 'c' * 40
        self.sdk_sha = 'b' * 40
        self.store = {}
        self.fetches = {}
        targets = {}
        for target_id, target in nightly_targets.targets_for(channel).items():
            version = '1.5.8' if channel == 'stable' else f'1.5.7-rc+{self.current_sha[:7]}'
            assets = []
            contents = {}
            for role, name, offset in verify_nightly_release.expected_assets(target_id, channel):
                data = f'{target_id}/{role}'.encode()
                contents[name] = data
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
                manifest = {
                    'schemaVersion': 1,
                    'channel': channel,
                    **nightly_targets.public_target(target_id),
                    'environment': nightly_targets.environment_for(target_id, channel, flavor),
                    'flavor': flavor,
                    'version': version,
                    'crossmuxSha': self.current_sha,
                    'sdkSha': self.sdk_sha,
                    'assets': assets,
                }
                name = nightly_targets.manifest_name(target_id, flavor)
                contents[name] = json.dumps(manifest).encode()
                url = self.release_url + name
                self.store[url] = contents[name]
                variants[flavor] = {
                    'version': version,
                    'crossmuxSha': self.current_sha,
                    'sdkSha': self.sdk_sha,
                    'publishedAt': 'now',
                    'manifestUrl': url,
                }
            name = nightly_targets.bundle_name(target_id, version)
            data = bundle_bytes(target_id, contents)
            self.store[self.release_url + name] = data
            targets[target_id] = {
                **nightly_targets.public_target(target_id),
                'variants': variants,
                'bundle': {
                    'name': name,
                    'url': self.release_url + name,
                    'size': len(data),
                    'sha256': hashlib.sha256(data).hexdigest(),
                },
            }
        self.index = {
            'schemaVersion': 1,
            'channel': channel,
            'updatedAt': 'now',
            'buildId': 'test',
            'targets': targets,
        }
        if channel == 'stable':
            self.index['releaseNotes'] = {'global': ['First note', 'Second note'], 'zh-CN': ['第一项', '第二项']}
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
        self.assertEqual(result['assets'], 36)
        self.assertEqual(result['bundles'], 9)
        for url in self.store:
            if url.endswith(('.bin', '.zip')):
                self.assertEqual(self.fetches[url], 1)

    def test_verifies_four_stable_targets_and_sixteen_assets(self):
        self.prepare_release('stable')
        result = verify_nightly_release.verify_release(self.index_url, self.current_sha, 'stable', self.fetch)
        self.assertEqual(result['targets'], 4)
        self.assertEqual(result['assets'], 16)
        self.assertEqual(result['bundles'], 4)

    def local_release(self, root, region='global'):
        artifacts = root / 'artifacts'
        artifacts.mkdir()
        index_file = root / 'index.json'
        index_file.write_bytes(self.store[self.index_url])
        for url, data in self.store.items():
            if url != self.index_url:
                (artifacts / url.rsplit('/', 1)[-1]).write_bytes(data)
        if region == 'cn':
            index = json.loads(index_file.read_bytes())
            for entry in index['targets'].values():
                target_dir = artifacts / entry['targetId']
                target_dir.mkdir()
                for path in list(artifacts.iterdir()):
                    if path.is_file() and (
                        path.name.startswith(entry['deviceSlug'] + '-') or path.name == entry['bundle']['name']
                    ):
                        path.rename(target_dir / path.name)
                for pointer in entry['variants'].values():
                    pointer['manifestUrl'] = (
                        f"https://assets.crossmux.cn/firmware/builds/test/{entry['targetId']}/"
                        + pointer['manifestUrl'].rsplit('/', 1)[-1]
                    )
                entry['bundle']['url'] = (
                    f"https://assets.crossmux.cn/firmware/builds/test/{entry['targetId']}/"
                    + entry['bundle']['name']
                )
            index_file.write_text(json.dumps(index))
        return index_file, artifacts

    def test_local_verification_covers_both_channels_and_regions_without_network(self):
        for channel in nightly_targets.CHANNELS:
            self.prepare_release(channel)
            for region in ('global', 'cn'):
                with self.subTest(channel=channel, region=region), tempfile.TemporaryDirectory() as directory:
                    index_file, artifacts = self.local_release(Path(directory), region)
                    index_url = self.index_url if region == 'global' else (
                        f'https://assets.crossmux.cn/firmware/releases/{channel}/index.json'
                    )
                    argv = ['verify_nightly_release.py', '--channel', channel, '--index-url', index_url,
                            '--index-file', str(index_file), '--artifacts', str(artifacts),
                            '--expected-sha', self.current_sha]
                    with mock.patch.object(sys, 'argv', argv), mock.patch.object(
                        verify_nightly_release.urllib.request, 'urlopen', side_effect=AssertionError('network')
                    ), mock.patch('builtins.print') as output:
                        verify_nightly_release.main()
                    self.assertIn('16 assets plus 4 ZIP bundles' if channel == 'stable' else '36 assets plus 9 ZIP bundles',
                                  output.call_args.args[0])

    def test_local_missing_ambiguous_or_corrupt_artifacts_fail_without_remote_fallback(self):
        for failure in ('missing', 'ambiguous', 'corrupt'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as directory:
                index_file, artifacts = self.local_release(Path(directory))
                path = artifacts / nightly_targets.asset_name('sticky', 'firmware.bin')
                if failure == 'missing':
                    path.unlink()
                elif failure == 'ambiguous':
                    duplicate = artifacts / 'duplicate'
                    duplicate.mkdir()
                    (duplicate / path.name).write_bytes(path.read_bytes())
                else:
                    path.write_bytes(b'corrupt')
                with mock.patch.object(
                    verify_nightly_release.urllib.request, 'urlopen', side_effect=AssertionError('network')
                ), self.assertRaisesRegex(ValueError, 'local artifact|size or SHA-256'):
                    verify_nightly_release.verify_release(
                        self.index_url, self.current_sha, 'nightly',
                        verify_nightly_release.local_fetch(self.index_url, index_file, artifacts),
                    )

    def test_local_verification_still_rejects_untrusted_urls(self):
        self.index['targets']['sticky']['bundle']['url'] = 'https://example.com/firmware.zip'
        self.write_index()
        with tempfile.TemporaryDirectory() as directory:
            index_file, artifacts = self.local_release(Path(directory))
            with self.assertRaisesRegex(ValueError, 'outside the expected release path'):
                verify_nightly_release.verify_release(
                    self.index_url, self.current_sha, 'nightly',
                    verify_nightly_release.local_fetch(self.index_url, index_file, artifacts),
                )

    def test_local_cli_requires_both_index_and_artifacts(self):
        for option in ('--index-file', '--artifacts'):
            argv = ['verify_nightly_release.py', '--channel', 'nightly', '--index-url', self.index_url,
                    '--expected-sha', self.current_sha, option, 'local']
            with self.subTest(option=option), mock.patch.object(sys, 'argv', argv), mock.patch(
                'sys.stderr', new_callable=io.StringIO
            ) as stderr, self.assertRaises(SystemExit):
                verify_nightly_release.main()
            self.assertIn('--index-file and --artifacts must be used together', stderr.getvalue())

    def replace_bundle_contents(self, change):
        entry = self.index['targets']['sticky']['bundle']
        with zipfile.ZipFile(io.BytesIO(self.store[entry['url']])) as archive:
            contents = {name: archive.read(name) for name in archive.namelist()}
        change(contents)
        data = bundle_bytes('sticky', contents)
        self.store[entry['url']] = data
        entry.update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
        self.write_index()

    def test_rejects_corrupt_bundle(self):
        entry = self.index['targets']['sticky']['bundle']
        self.store[entry['url']] += b'corrupt'
        with self.assertRaisesRegex(ValueError, 'bundle failed size or SHA-256'):
            verify_nightly_release.verify_release(self.index_url, self.current_sha, 'nightly', self.fetch)

    def test_rejects_bundle_with_extra_file(self):
        self.replace_bundle_contents(lambda contents: contents.update({'../extra.bin': b'bad'}))
        with self.assertRaisesRegex(ValueError, 'unexpected contents'):
            verify_nightly_release.verify_release(self.index_url, self.current_sha, 'nightly', self.fetch)

    def test_rejects_bundle_with_different_binary(self):
        self.replace_bundle_contents(lambda contents: contents.update({'sticky-firmware.bin': b'bad'}))
        with self.assertRaisesRegex(ValueError, 'bundle contains an invalid firmware'):
            verify_nightly_release.verify_release(self.index_url, self.current_sha, 'nightly', self.fetch)

    def test_rejects_bundle_with_different_manifest(self):
        self.replace_bundle_contents(lambda contents: contents.update({'sticky-global-manifest.json': b'{}'}))
        with self.assertRaisesRegex(ValueError, 'bundle manifest differs'):
            verify_nightly_release.verify_release(self.index_url, self.current_sha, 'nightly', self.fetch)

    def test_rejects_bundle_with_bad_checksums(self):
        self.replace_bundle_contents(lambda contents: contents.update({'sticky-SHA256SUMS': b'bad'}))
        with self.assertRaisesRegex(ValueError, 'invalid checksums'):
            verify_nightly_release.verify_release(self.index_url, self.current_sha, 'nightly', self.fetch)

    def test_rejects_bundle_outside_release_path(self):
        self.index['targets']['sticky']['bundle']['url'] = 'https://example.com/firmware.zip'
        self.write_index()
        with self.assertRaisesRegex(ValueError, 'outside the expected release path'):
            verify_nightly_release.verify_release(self.index_url, self.current_sha, 'nightly', self.fetch)

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
