"""Inject the shared build/release version into its consuming source files."""

import configparser
import os
import subprocess
import sys

try:
    Import('env')  # noqa: F821  # type: ignore[name-defined]
except NameError:
    env = None

_project_dir = env['PROJECT_DIR'] if env is not None else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_project_dir, 'scripts'))
from nightly_targets import version_for


def warn(msg):
    print(f'WARNING [git_branch.py]: {msg}', file=sys.stderr)


def run_git_value(project_dir, args, label):
    try:
        value = subprocess.check_output(
            ['git', *args],
            text=True, stderr=subprocess.PIPE, cwd=project_dir
        ).strip()
        # Strip characters that would break a C string literal
        return ''.join(c for c in value if c not in '"\\')
    except FileNotFoundError:
        warn(f'git not found on PATH; {label} suffix will be "unknown"')
        return 'unknown'
    except subprocess.CalledProcessError as e:
        warn(
            f'git command failed (exit {e.returncode}): '
            f'{e.stderr.strip()}; {label} suffix will be "unknown"'
        )
        return 'unknown'
    except OSError as e:
        warn(
            f'OS error reading git {label}: {e}; '
            f'{label} suffix will be "unknown"'
        )
        return 'unknown'
    except Exception as e:  # pylint: disable=broad-exception-caught
        warn(
            f'Unexpected error reading git {label}: {e}; '
            f'{label} suffix will be "unknown"'
        )
        return 'unknown'


def get_git_branch(project_dir):
    branch = run_git_value(
        project_dir, ['rev-parse', '--abbrev-ref', 'HEAD'], 'branch'
    )
    # Detached HEAD has no branch name.
    if branch == 'HEAD':
        return 'detached'
    return branch


def get_git_short_sha(project_dir):
    return run_git_value(
        project_dir, ['rev-parse', '--short=7', 'HEAD'], 'short SHA'
    )


def get_base_version(project_dir):
    ini_path = os.path.join(project_dir, 'platformio.ini')
    if not os.path.isfile(ini_path):
        warn(f'platformio.ini not found at {ini_path}; base version will be "0.0.0"')
        return '0.0.0'
    config = configparser.ConfigParser()
    config.read(ini_path, encoding='utf-8')
    if not config.has_option('crosspoint', 'version'):
        warn('No [crosspoint] version in platformio.ini; base version will be "0.0.0"')
        return '0.0.0'
    return config.get('crosspoint', 'version')


def inject_version(env):
    mode = env.GetProjectOption('custom_build_mode', None)
    if mode is None:
        return

    project_dir = env['PROJECT_DIR']
    version_string = version_for(
        get_base_version(project_dir), None, mode, 'global', get_git_short_sha(project_dir),
        get_git_branch(project_dir) if mode == 'dev' else 'unknown',
    )

    def add_version(build_env, node):
        # Only these translation units depend on Git metadata. Keep library and
        # reader objects reusable when the branch or commit changes.
        if b'CROSSPOINT_VERSION' not in node.srcnode().get_contents():
            return node
        version_env = build_env.Clone()
        version_env.Append(CPPDEFINES=[('CROSSPOINT_VERSION', f'\\"{version_string}\\"')])
        return version_env.Object(node)

    env.AddBuildMiddleware(add_version)
    print(f'CrossPoint build version: {version_string}')


# SCons supplies env without __file__; direct Python remains a version smoke check.
if env is not None:
    inject_version(env)
elif __name__ == '__main__':
    class _Env(dict):
        def GetProjectOption(self, name, default):
            return self.get(name, default)

        def AddBuildMiddleware(self, _):
            # Validation mode only reports the computed version.
            return None

    inject_version(_Env({'custom_build_mode': 'dev', 'PROJECT_DIR': _project_dir}))
