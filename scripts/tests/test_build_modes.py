"""Keep the public build entrypoints and injected versions in sync."""

import configparser
import sys
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
            'waveshare_epaper_397', 'metalio_eink4', 'readpico', 'x4c', 'lilygo_t5s3',
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

    def test_only_original_development_targets_wait_for_usb(self):
        self.assertEqual(
            {name.removeprefix('env:') for name in self.config.sections()
             if name.startswith('env:') and 'CROSSPOINT_WAIT_FOR_USB_SERIAL' in self.config[name].get('build_flags', '')},
            {'default', 'sticky', 'x4pro', 'x4c', 'papermono', 'readpico'},
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
