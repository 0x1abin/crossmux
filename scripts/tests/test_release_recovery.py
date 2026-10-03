import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import publish_firmware as publish
import restore_firmware_channel as recovery
from verify_nightly_release import validate_url


class PublicationTest(unittest.TestCase):
    def test_channel_and_version_paths(self):
        base = 'https://github.com/0x1abin/crossmux/releases/download/'
        for tag, channel in [('1.6.0', 'stable'), ('stable-build-old', 'stable'), ('nightly-build-old', 'nightly')]:
            validate_url(base + tag + '/sticky-global-manifest.json', base + channel, channel, '1.6.0')
        for path, channel in [('1.6.1/a.bin', 'stable'), ('1.6.0/a.bin', 'nightly'),
                              ('nightly-build-old/a.bin', 'stable'), ('stable/a.bin', 'stable'),
                              ('1.6.0/a.bin?x=1', 'stable'), ('1.6.0/a.bin#x', 'stable')]:
            with self.assertRaises(ValueError):
                validate_url(base + path, base + channel, channel, '1.6.0')
        with self.assertRaises(ValueError):
            validate_url(base.replace('0x1abin', 'other') + '1.6.0/a.bin', base, 'stable', '1.6.0')

    def test_retry_only_uploads_missing_assets_and_checks_all_conflicts_first(self):
        with tempfile.TemporaryDirectory() as temp:
            present = Path(temp) / 'present.bin'
            missing = Path(temp) / 'missing.bin'
            present.write_bytes(b'existing')
            missing.write_bytes(b'new')
            asset = {'name': present.name, 'size': 8, 'digest': 'sha256:' + hashlib.sha256(b'existing').hexdigest()}
            with mock.patch.object(publish, 'gh') as gh:
                publish.upload_assets('1.6.0', [missing, present], {'assets': [asset]})
                self.assertEqual(gh.call_count, 1)
                self.assertIn(str(missing), gh.call_args.args)
                self.assertNotIn('--clobber', gh.call_args.args)
                gh.reset_mock()
                publish.upload_assets('1.6.0', [present], {'assets': [asset]})
                gh.assert_not_called()
                present.write_bytes(b'conflict')
                with self.assertRaisesRegex(ValueError, 'immutable asset differs'):
                    publish.upload_assets('1.6.0', [missing, present], {'assets': [asset]})
                gh.assert_not_called()

    def test_cos_retry_and_conflict(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            target = root / 'firmware-stable-sticky'
            target.mkdir()
            (target / 'sticky-firmware.bin').write_bytes(b'firmware')
            env = {'COS_SECRET_ID': 'test', 'COS_SECRET_KEY': 'test', 'COS_REGION': 'test', 'COS_BUCKET': 'test'}
            with mock.patch.dict(publish.os.environ, env), mock.patch.object(publish, 'download', return_value=b'firmware') as fetch, mock.patch.object(publish.subprocess, 'run') as run:
                publish.publish_cos(root, 'build', Path('/coscli'))
                run.assert_not_called()
                fetch.return_value = b'wrong'
                with self.assertRaisesRegex(ValueError, 'immutable COS object differs'):
                    publish.publish_cos(root, 'build', Path('/coscli'))
                run.assert_not_called()
                fetch.side_effect = publish.urllib.error.HTTPError('https://assets.crossmux.cn/missing', 404, 'missing', {}, None)
                run.return_value.returncode = 0
                publish.publish_cos(root, 'build', Path('/coscli'))
                self.assertIn('--forbid-overwrite', run.call_args.args[0])

    def test_channel_uploads_index_last_and_uses_public_titles(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            firmware = root / 'xteink-firmware.bin'
            firmware.write_bytes(b'verified')
            index = root / 'index.json'
            index.write_text(json.dumps({'channel': 'stable', 'targets': {'xteink_x4': {'variants': {'global': {
                'manifestUrl': 'https://github.com/0x1abin/crossmux/releases/download/1.6.0/xteink-global-manifest.json',
                'crossmuxSha': 'a' * 40, 'sdkSha': 'b' * 40, 'version': '1.6.0',
            }}}}}))
            with mock.patch.object(publish, 'release', side_effect=[None, {'assets': []}]), mock.patch.object(publish, 'gh') as gh, mock.patch.object(publish, 'channel_alias_data', return_value={'firmware.bin': b'verified'}):
                publish.publish_channel('stable', index)
                uploads = [call.args for call in gh.call_args_list if call.args[:2] == ('release', 'upload')]
                self.assertEqual([Path(call[3]).name for call in uploads], ['firmware.bin', 'release-index.json'])
                self.assertEqual(gh.call_args.args[gh.call_args.args.index('--title') + 1], 'CrossMux Stable')

    def test_release_identity_is_not_tied_to_a_specific_target(self):
        pointer = {'version': '1.7.0', 'crossmuxSha': 'a' * 40, 'sdkSha': 'b' * 40}
        index = {'channel': 'stable', 'targets': {'future_board': {'variants': {'global': pointer}}}}
        self.assertEqual(publish.release_identity(index), ('a' * 40, '1.7.0'))
        index['targets']['another'] = {'variants': {'global': {**pointer, 'version': '1.6.0'}}}
        with self.assertRaisesRegex(ValueError, 'share a version'):
            publish.release_identity(index)
        index['targets']['another']['variants']['global'] = {**pointer, 'crossmuxSha': 'c' * 40}
        with self.assertRaisesRegex(ValueError, 'source revision'):
            publish.release_identity(index)

    def test_alias_failure_never_changes_the_channel(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            index = root / 'index.json'
            index.write_text(json.dumps({'channel': 'nightly', 'targets': {'future_board': {'variants': {'global': {
                'version': '1.7.0-future-rc+aaaaaaa', 'crossmuxSha': 'a' * 40, 'sdkSha': 'b' * 40,
                'manifestUrl': 'https://github.com/0x1abin/crossmux/releases/download/nightly-build-test/future-global-manifest.json',
            }}}}}))
            with mock.patch.object(publish, 'channel_alias_data', side_effect=ValueError('bad checksum')), mock.patch.object(publish, 'gh') as gh:
                with self.assertRaisesRegex(ValueError, 'bad checksum'):
                    publish.publish_channel('nightly', index)
                gh.assert_not_called()

    def test_declared_aliases_verify_content_and_missing_files_fail(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            data = b'verified'
            manifest = {'assets': [{'role': role, 'name': f'xteink-{role}.bin', 'size': len(data),
                                   'sha256': hashlib.sha256(data).hexdigest()}
                                  for role in ('bootloader', 'partitions', 'firmware')]}
            (root / 'xteink-global-manifest.json').write_text(json.dumps(manifest))
            for asset in manifest['assets']:
                (root / asset['name']).write_bytes(data)
            publish.package_aliases(root, 'stable')
            publish.package_aliases(root, 'stable')
            self.assertEqual((root / 'compatibility/firmware-cn.bin').read_bytes(), data)
            (root / 'xteink-firmware.bin').write_bytes(b'corrupt')
            with self.assertRaisesRegex(ValueError, 'verification'):
                publish.package_aliases(root, 'stable')
            (root / 'xteink-firmware.bin').unlink()
            with self.assertRaises(FileNotFoundError):
                publish.package_aliases(root, 'stable')


class RecoveryTest(unittest.TestCase):
    def setUp(self):
        sha = 'a' * 40
        build = f'stable-build-{sha}-158-1'
        pointer = {'version': '1.6.0', 'crossmuxSha': sha, 'sdkSha': 'b' * 40, 'publishedAt': 'original',
                   'manifestUrl': f'https://assets.crossmux.cn/firmware/builds/{build}/xteink_x4/xteink-global-manifest.json'}
        self.source = {'schemaVersion': 1, 'channel': 'stable', 'updatedAt': 'original', 'buildId': build,
                       'releaseNotes': {'global': ['One', 'Two'], 'zh-CN': ['一', '二']},
                       'targets': {'xteink_x4': {'variants': {'global': pointer, 'zh-CN': copy.deepcopy(pointer)}}}}
        self.global_index = recovery.global_index(self.source, build)
        self.current = None
        self.assets = []

    def release(self, tag):
        if tag == 'stable':
            return None if self.current is None else {'id': 1, 'draft': False, 'updated_at': 'original', 'assets': self.assets}
        return {'draft': False}

    def download(self, url):
        if url.endswith('/stable/index.json'):
            return json.dumps(self.source).encode()
        if url.endswith('/stable/release-index.json'):
            return json.dumps(self.current).encode()
        return b'{"manifest":"same"}'

    def verify(self, index, url, sha, root):
        (root / 'firmware.bin').write_bytes(b'verified')

    def test_url_conversion_preserves_original_metadata(self):
        self.assertEqual(recovery.identity(self.global_index), recovery.identity(self.source))
        self.assertEqual(self.global_index['updatedAt'], 'original')
        self.assertIn('/releases/download/stable-build-', self.global_index['targets']['xteink_x4']['variants']['global']['manifestUrl'])
        changed = copy.deepcopy(self.global_index)
        changed['buildId'] = 'different'
        self.assertNotEqual(recovery.identity(changed), recovery.identity(self.source))

    def test_restore_missing_channel_and_leave_healthy_channel_unchanged(self):
        def publish_channel(channel, index):
            self.current = json.loads(index.read_text())
            self.assets = [{'name': 'release-index.json', 'id': 1, 'updated_at': 'now'},
                           {'name': 'firmware.bin', 'id': 2, 'updated_at': 'now'}]
        with mock.patch.object(recovery, 'release', side_effect=self.release), mock.patch.object(recovery, 'download', side_effect=self.download), mock.patch.object(recovery, 'verify_snapshot', side_effect=self.verify), mock.patch.object(recovery.subprocess, 'run'), mock.patch.object(recovery, 'same_asset', return_value=True), mock.patch.object(recovery, 'channel_alias_data', return_value={'firmware.bin': b'verified'}), mock.patch.object(recovery, 'publish_channel', side_effect=publish_channel) as publish_channel:
            recovery.restore('stable')
            self.assertEqual(publish_channel.call_count, 1)
            publish_channel.reset_mock()
            recovery.restore('stable')
            publish_channel.assert_not_called()

    def test_disagreement_or_corrupt_resources_never_publish(self):
        with mock.patch.object(recovery, 'release', side_effect=self.release), mock.patch.object(recovery, 'download', side_effect=self.download), mock.patch.object(recovery, 'verify_snapshot', side_effect=ValueError('bad checksum')), mock.patch.object(recovery.subprocess, 'run'), mock.patch.object(recovery, 'publish_channel') as publish_channel:
            with self.assertRaisesRegex(ValueError, 'bad checksum'):
                recovery.restore('stable')
            publish_channel.assert_not_called()
            self.current = copy.deepcopy(self.global_index)
            self.current['buildId'] = 'newer-build'
            self.assets = [{'name': 'release-index.json'}]
            with self.assertRaisesRegex(ValueError, 'regional channels differ'):
                recovery.restore('stable')
            publish_channel.assert_not_called()

    def test_draft_channel_requires_inspection(self):
        with mock.patch.object(recovery, 'release', return_value={'draft': True}), mock.patch.object(recovery, 'download', side_effect=self.download), mock.patch.object(recovery, 'publish_channel') as publish_channel:
            with self.assertRaisesRegex(ValueError, 'draft'):
                recovery.restore('stable')
            publish_channel.assert_not_called()


if __name__ == '__main__':
    unittest.main()
