"""Keep the public build entrypoints and injected versions in sync."""

import configparser
import sys
import types
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))

import git_branch
from nightly_targets import TARGETS, version_for


class BuildModesTest(unittest.TestCase):
    def setUp(self):
        self.config = configparser.ConfigParser()
        self.config.read(ROOT / 'platformio.ini')

    def test_exact_build_entrypoints_and_shared_hardware(self):
        development = {
            'default', 'sticky', 'x4pro', 'papermono', 'eego_a4', 'murphy_m4',
            'waveshare_epaper_397', 'metalio_eink4', 'readpico', 'x4c', 'lilygo_t5s3', 'picco',
        }
        simulators = {
            'simulator', 'simulator_x3', 'simulator_sticky', 'simulator_eego_a4',
            'simulator_murphy_m4', 'simulator_readpico',
        }
        released = {environment for target in TARGETS.values() for environment in target['environments'].values()}
        self.assertEqual(len(released), 18)
        self.assertEqual(
            {name.removeprefix('env:') for name in self.config.sections() if name.startswith('env:')},
            development | simulators | released,
        )
        for name in development | released:
            with self.subTest(environment=name):
                environment = self.config[f'env:{name}']
                mode = 'nightly' if name.endswith('_nightly') or name == 'gh_release_rc' else (
                    'stable' if name.endswith('gh_release') else 'dev'
                )
                hardware, mode_section = environment['extends'].split(', ')
                self.assertEqual(mode_section, f'mode_{mode}')
                self.assertEqual(self.config[mode_section]['custom_build_mode'], mode)
                self.assertIn('${' + hardware + '.build_flags}', environment['build_flags'])
                self.assertIn('${' + mode_section + '.build_flags}', environment['build_flags'])
                self.assertNotIn('CROSSPOINT_VERSION', environment['build_flags'])
        for target in TARGETS.values():
            stable = self.config['env:' + target['environments']['stable']]['extends'].split(', ')[0]
            nightly = self.config['env:' + target['environments']['nightly']]['extends'].split(', ')[0]
            self.assertEqual(stable, nightly)

    def test_native_c_dialect_keeps_cpp_and_hardware_flags(self):
        self.assertIn('-std=gnu11', self.config['env:simulator']['build_flags'])
        self.assertIn('-std=gnu++2a', self.config['env:simulator']['build_flags'])
        for section in self.config.sections():
            if section.startswith('env:simulator_'):
                self.assertEqual(self.config[section]['extends'], 'env:simulator')
            elif section != 'env:simulator':
                self.assertNotIn('-std=gnu11', self.config[section].get('build_flags', ''))

    def test_picco_preserves_stock_partition_and_app_only_upload(self):
        hardware = self.config['picco_hardware']
        self.assertEqual(hardware['board_build.partitions'], 'partitions_picco.csv')
        self.assertIn('post:scripts/app_only_upload.py', hardware['extra_scripts'])
        self.assertFalse(any(target['boardTag'] == 'picco' for target in TARGETS.values()))

    def test_picco_post_hook_removes_captured_images_and_rejects_other_flash_actions(self):
        class BuildEnv(dict):
            def Replace(self, **values):
                self.update(values)

            def subst(self, value):
                return value.replace('$BUILD_DIR', '/private/picco')

            def Exit(self, code):
                raise SystemExit(code)

        command = '$UPLOADER $UPLOADERFLAGS $ESP32_APP_OFFSET $SOURCE'
        script = ROOT / 'scripts/app_only_upload.py'
        images = [('0x0', '$BUILD_DIR/bootloader.bin'), ('0x8000', '$BUILD_DIR/partitions.bin'),
                  ('0x15000', '/framework/boot_app0.bin')]
        base_flags = ['--chip', 'esp32s3', '--port', '$UPLOAD_PORT', 'write-flash', '-z']

        def run_hook(targets, upload_command=command):
            environment = BuildEnv(ESP32_APP_OFFSET='0xe0000', UPLOADCMD=upload_command,
                                   UPLOADERFLAGS=base_flags.copy(), FLASH_EXTRA_IMAGES=images.copy())
            # The platform BUILD_SCRIPT appends these before running post hooks.
            for address, image in images:
                environment['UPLOADERFLAGS'] += [address, environment.subst(image)]
            module = types.ModuleType('SCons.Script')
            module.COMMAND_LINE_TARGETS = targets
            with mock.patch.dict(sys.modules, {'SCons': types.ModuleType('SCons'), 'SCons.Script': module}), \
                    mock.patch('builtins.print'):
                exec(compile(script.read_text(), str(script), 'exec'),
                     {'Import': lambda _name: None, 'env': environment})
            return environment

        for targets in ([], ['upload'], ['nobuild', 'upload']):
            with self.subTest(targets=targets):
                environment = run_hook(targets)
                self.assertEqual(environment['FLASH_EXTRA_IMAGES'], [])
                self.assertEqual(environment['UPLOADERFLAGS'], base_flags)
                self.assertEqual(environment['ESP32_APP_OFFSET'], '0xe0000')
                self.assertEqual(environment['UPLOADCMD'], command)
        for target in ('erase', 'erase_upload', 'uploadfs', 'uploadfsota'):
            with self.subTest(target=target), self.assertRaises(SystemExit) as error:
                run_hook([target])
            self.assertEqual(error.exception.code, 1)
        for other in ('espota -f $SOURCE', 'dfu-util -D $SOURCE',
                      'openocd program_esp $SOURCE', 'custom-flasher $SOURCE'):
            with self.subTest(command=other), self.assertRaises(SystemExit) as error:
                run_hook(['upload'], other)
            self.assertEqual(error.exception.code, 1)

    def test_only_original_development_targets_wait_for_usb(self):
        self.assertEqual(
            {name.removeprefix('env:') for name in self.config.sections()
             if name.startswith('env:') and 'CROSSPOINT_WAIT_FOR_USB_SERIAL' in self.config[name].get('build_flags', '')},
            {'default', 'sticky', 'x4pro', 'x4c', 'papermono', 'readpico', 'picco'},
        )

    def test_release_modes_require_git_provenance(self):
        for mode in ('stable', 'nightly'):
            for revision in ('unknown', '', '123456', '123456z'):
                with self.subTest(mode=mode, revision=revision), self.assertRaises(ValueError):
                    version_for('1.6.5', None, mode, 'global', revision)
        self.assertEqual(version_for('1.6.5', None, 'dev', 'global', 'unknown'),
                         '1.6.5-dev-unknown-unknown')

    def test_scons_executes_without_file_global(self):
        environment = mock.MagicMock()
        environment.__getitem__.side_effect = {'PROJECT_DIR': str(ROOT)}.__getitem__
        environment.GetProjectOption.return_value = 'stable'
        script = ROOT / 'scripts/git_branch.py'
        globals_dict = {'Import': lambda _name: None, 'env': environment}
        with mock.patch('builtins.print'):
            exec(compile(script.read_text(), str(script), 'exec'), globals_dict)
        environment.AddBuildMiddleware.assert_called_once()

    def test_versions_match_the_packager_and_only_rebuild_consumers(self):
        class BuildEnv(dict):
            def GetProjectOption(self, name, default):
                return self.get(name, default)

            def AddBuildMiddleware(self, callback):
                self.middleware = callback

            def Clone(self):
                return self

            def Append(self, **values):
                self.update(values)

            def Object(self, node):
                return ('object', node)

        for mode in ('dev', 'stable', 'nightly'):
            with self.subTest(mode=mode), mock.patch.object(git_branch, 'get_base_version', return_value='1.6.5'), \
                    mock.patch.object(git_branch, 'get_git_short_sha', return_value='12345678'), \
                    mock.patch.object(git_branch, 'get_git_branch', return_value='feature'), mock.patch('builtins.print'):
                env = BuildEnv(custom_build_mode=mode, PROJECT_DIR=str(ROOT))
                git_branch.inject_version(env)
                consumer = mock.Mock()
                consumer.srcnode().get_contents.return_value = b'const char* version = CROSSPOINT_VERSION;'
                self.assertEqual(env.middleware(env, consumer), ('object', consumer))
                expected = version_for('1.6.5', None, mode, 'global', '12345678', 'feature')
                self.assertEqual(env['CPPDEFINES'], [('CROSSPOINT_VERSION', f'\\"{expected}\\"')])
                other = mock.Mock()
                other.srcnode().get_contents.return_value = b'void reader() {}'
                self.assertIs(env.middleware(env, other), other)
        simulator = BuildEnv(PROJECT_DIR=str(ROOT))
        git_branch.inject_version(simulator)
        self.assertFalse(hasattr(simulator, 'middleware'))


if __name__ == '__main__':
    unittest.main()
