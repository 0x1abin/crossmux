# Getting Started

This guide helps you build and run CrossMux locally.

## Prerequisites

- [pioarduino Core](https://github.com/pioarduino/platformio-core) (`pio`) or VS Code + pioarduino IDE, using the pinned core below
- Python 3 (the CI toolchain version is recorded in `.github/workflows/ci.yml`)
- CMake and Ninja for host tests
- `clang-format` 21+ in your `PATH` (CI uses clang-format 21)
- USB-C cable
- A matching device for hardware testing; the default build targets Xteink X3/X4

If `./bin/clang-format-fix` fails with either of these errors, install clang-format 21:

- `clang-format: No such file or directory`
- `.clang-format: error: unknown key 'AlignFunctionDeclarations'`

Examples:

```sh
# Debian/Ubuntu (try this first)
sudo apt-get update && sudo apt-get install -y clang-format-21

# If the package is unavailable, add LLVM apt repo and retry
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 21
sudo apt-get update
sudo apt-get install -y clang-format-21

# macOS (Homebrew)
brew install clang-format
```

Then verify:

```sh
clang-format-21 --version
```

The reported major version must be 21 or newer.

## Clone and initialize

```sh
git clone --recursive https://github.com/0x1abin/crossmux.git
cd crossmux
```

If you already cloned without submodules:

```sh
git submodule update --init --recursive
```

Enable the repository-managed Git hooks (required once per clone):

```sh
git config core.hooksPath .githooks
chmod +x .githooks/pre-commit
```

## Build

With an existing compilation setup, run the selected profile directly:

```sh
pio run -e default
```

`pio run` also selects the C3 X3/X4 profile. Select an existing board environment
in `platformio.ini` for other devices; see [device profiles](./touch-and-ui.md#building-and-testing-on-other-devices).
Keep the installed tools, packages, and build caches. The setup commands below
apply only to a new environment or an identified missing or incompatible
dependency; they are not steps to repeat before each build.

## First-time toolchain setup

Use the pioarduino Core revision installed by
[CI](../../.github/workflows/ci.yml), together with the platform pinned in
`platformio.ini`. On Linux or macOS, install the core in the ignored local
virtual environment:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install pioarduino==6.2.0
python scripts/patch_pioarduino_cache.py --prepare-platform
pio --version
```

On Windows, use the Python version configured in CI and create the environment with `py -3 -m venv .venv` and activate
`.venv/Scripts/activate` in Git Bash, or `.venv\Scripts\Activate.ps1` in PowerShell.
Use this core in the IDE too; installing the IDE alone does not verify its core
version. CI also lists the Python dependencies needed for a fresh platform setup.

Profiles with `custom_sdkconfig`, including `default` and `sticky`, build a
nested core environment. Pin that core too before the first build, as CI does:

```sh
python -m pip install uv
pio pkg install -e default
uv pip install --python ~/.platformio/penv/bin/python 'pioarduino==6.2.0'
```

Replace `default` with the selected profile. The nested Python path above uses
the default Linux/macOS PlatformIO directory; on Windows use
`~/.platformio/penv/Scripts/python.exe`, or the configured core directory.
Keep both core pins aligned with CI when updating the toolchain. After setup,
use the [normal build command](#build).

## Flash

```sh
pio run --target upload
```

## First checks before opening a PR

```sh
./bin/ci-check
```

The repository pins the pioarduino platform in `platformio.ini`. `pio run` builds
the X3/X4 development firmware; `pio run -e gh_release` builds its unified-language
stable profile. Use the matching environment for other devices; see
[build environments and simulator setup](../engineering/build-system.md).

Documentation-only changes need local link, command, and whitespace checks,
not firmware builds.

## What to read next

- [Architecture Overview](./architecture.md)
- [Development Workflow](./development-workflow.md)
- [Testing and Debugging](./testing-debugging.md)

Development version flags affect sources using `CROSSPOINT_VERSION`; changing a
branch or SHA alone does not require a full clean build. Keep personal ports and
flags in ignored `platformio.local.ini`. Its `build_flags` replaces the entire
selected environment list, so preserve its device/capability/logging/version
flags in addition to `${base.build_flags}`.
