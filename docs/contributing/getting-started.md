# Getting Started

This guide helps you build and run CrossMux locally.

## Prerequisites

- PlatformIO Core (`pio`) or VS Code + PlatformIO IDE
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

```sh
pio run
```

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
