#!/usr/bin/env bash
# CrossPoint Reader simulator — one-shot driver. Installs missing host dependencies,
# configures + builds simulator/, then launches the binary with simulator/sd_root/
# mounted as the SD card. Re-run any time; every step is idempotent.

set -euo pipefail

# Always run from the repo root so relative paths in this script are stable, no matter
# where the caller invoked it from.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

usage() {
  cat <<EOF
Usage: ./simulator.sh [-- <args passed to crosspoint_simulator>]

Installs host dependencies (idempotent), builds simulator/, then runs the binary
with --sd-root ./simulator/sd_root. Positional args are forwarded to the binary
unchanged — useful for --scale or to override --sd-root.

Environment switches:
  SC_SKIP_INSTALL=1   Skip the dependency-install step (assume deps present).
  SC_CLEAN=1          Remove simulator/build/ before configuring (full rebuild).
  SC_JOBS=N           Parallel build job count (default: nproc / sysctl -n hw.ncpu).

Examples:
  ./simulator.sh                          # install (if needed) → build → run
  ./simulator.sh --scale 2                # 2× window magnification
  ./simulator.sh --sd-root /tmp/sd        # override SD card root
  SC_SKIP_INSTALL=1 ./simulator.sh        # skip apt/brew (faster repeat runs)
  SC_CLEAN=1 ./simulator.sh               # force rebuild from scratch
EOF
}

if [[ ${1:-} == "--help" || ${1:-} == "-h" ]]; then
  usage
  exit 0
fi

SKIP_INSTALL="${SC_SKIP_INSTALL:-0}"
CLEAN="${SC_CLEAN:-0}"
if [[ -n "${SC_JOBS:-}" ]]; then
  JOBS="$SC_JOBS"
elif command -v nproc >/dev/null 2>&1; then
  JOBS="$(nproc)"
elif command -v sysctl >/dev/null 2>&1; then
  JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
else
  JOBS=4
fi

log() { printf '\033[1;36m[simulator.sh]\033[0m %s\n' "$*"; }
err() { printf '\033[1;31m[simulator.sh]\033[0m %s\n' "$*" >&2; }

# -----------------------------------------------------------------------------
# Step 1: detect platform
# -----------------------------------------------------------------------------
OS="$(uname -s)"

# -----------------------------------------------------------------------------
# Step 2: install dependencies (idempotent — only touches missing ones)
# -----------------------------------------------------------------------------
need_sudo() {
  if [[ $EUID -eq 0 ]]; then
    echo ""
  elif command -v sudo >/dev/null 2>&1; then
    echo "sudo"
  else
    err "Need root for package install but sudo is not available."
    err "Re-run as root, or install manually and re-run with SC_SKIP_INSTALL=1."
    exit 1
  fi
}

install_macos() {
  if ! command -v brew >/dev/null 2>&1; then
    err "Homebrew not found. Install it from https://brew.sh and re-run."
    exit 1
  fi
  local missing=()
  command -v cmake   >/dev/null 2>&1 || missing+=(cmake)
  command -v python3 >/dev/null 2>&1 || missing+=(python)
  command -v git     >/dev/null 2>&1 || missing+=(git)
  command -v pkg-config >/dev/null 2>&1 || missing+=(pkg-config)
  pkg-config --exists sdl2 2>/dev/null || brew list sdl2 >/dev/null 2>&1 || missing+=(sdl2)
  if [[ ${#missing[@]} -gt 0 ]]; then
    log "brew install ${missing[*]}"
    brew install "${missing[@]}"
  else
    log "macOS dependencies already present"
  fi
}

install_linux_apt() {
  local sudo_cmd; sudo_cmd="$(need_sudo)"
  local missing=()
  command -v cmake   >/dev/null 2>&1 || missing+=(cmake)
  command -v python3 >/dev/null 2>&1 || missing+=(python3)
  command -v g++     >/dev/null 2>&1 || missing+=(build-essential)
  command -v git     >/dev/null 2>&1 || missing+=(git)
  command -v pkg-config >/dev/null 2>&1 || missing+=(pkg-config)
  pkg-config --exists sdl2 2>/dev/null || missing+=(libsdl2-dev)
  pkg-config --exists libcurl 2>/dev/null || missing+=(libcurl4-openssl-dev)
  if [[ ${#missing[@]} -gt 0 ]]; then
    log "$sudo_cmd apt-get install -y ${missing[*]}"
    $sudo_cmd apt-get update -y
    $sudo_cmd apt-get install -y "${missing[@]}"
  else
    log "Linux (apt) dependencies already present"
  fi
}

install_linux_dnf() {
  local sudo_cmd; sudo_cmd="$(need_sudo)"
  local missing=()
  command -v cmake   >/dev/null 2>&1 || missing+=(cmake)
  command -v python3 >/dev/null 2>&1 || missing+=(python3)
  command -v g++     >/dev/null 2>&1 || missing+=(gcc-c++ make)
  command -v git     >/dev/null 2>&1 || missing+=(git)
  command -v pkg-config >/dev/null 2>&1 || missing+=(pkgconf-pkg-config)
  pkg-config --exists sdl2 2>/dev/null || missing+=(SDL2-devel)
  pkg-config --exists libcurl 2>/dev/null || missing+=(libcurl-devel)
  if [[ ${#missing[@]} -gt 0 ]]; then
    log "$sudo_cmd dnf install -y ${missing[*]}"
    $sudo_cmd dnf install -y "${missing[@]}"
  else
    log "Linux (dnf) dependencies already present"
  fi
}

if [[ "$SKIP_INSTALL" != "1" ]]; then
  case "$OS" in
    Darwin)
      install_macos
      ;;
    Linux)
      if command -v apt-get >/dev/null 2>&1; then
        install_linux_apt
      elif command -v dnf >/dev/null 2>&1; then
        install_linux_dnf
      else
        err "Unsupported Linux package manager. Install these manually then re-run with SC_SKIP_INSTALL=1:"
        err "  cmake python3 git pkg-config SDL2 and libcurl development packages plus a C++ compiler"
        exit 1
      fi
      ;;
    MINGW*|MSYS*|CYGWIN*)
      err "Native Windows is not supported by this script. Use WSL, or install"
      err "SDL2 + CMake + Python via MSYS2/vcpkg and rerun with SC_SKIP_INSTALL=1."
      exit 1
      ;;
    *)
      err "Unrecognised OS: $OS. Set SC_SKIP_INSTALL=1 and install deps manually."
      exit 1
      ;;
  esac
else
  log "SC_SKIP_INSTALL=1 — skipping dependency install"
fi

# -----------------------------------------------------------------------------
# Step 3: ensure the FreeInk SDK submodule is checked out
# -----------------------------------------------------------------------------
if [[ ! -f freeink-sdk/libs/display/FreeInkDisplay/include/EInkDisplay.h ]]; then
  log "git submodule update --init freeink-sdk"
  git submodule update --init freeink-sdk
fi

# -----------------------------------------------------------------------------
# Step 4: configure + build
# -----------------------------------------------------------------------------
if [[ "$CLEAN" == "1" ]]; then
  log "SC_CLEAN=1 — removing simulator/build/"
  rm -rf simulator/build
fi

log "cmake -S simulator -B simulator/build"
cmake -S simulator -B simulator/build

log "cmake --build simulator/build -j $JOBS"
cmake --build simulator/build -j "$JOBS"

# -----------------------------------------------------------------------------
# Step 5: ensure sd_root exists, then exec the binary
# -----------------------------------------------------------------------------
mkdir -p simulator/sd_root/.crosspoint

log "launching simulator (Ctrl+C or close the window to quit)"
exec ./simulator/build/crosspoint_simulator --sd-root ./simulator/sd_root "$@"
