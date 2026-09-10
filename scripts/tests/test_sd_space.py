"""Run SDK capacity methods against real SdFat partitions and controlled failures.

Usage: python3 scripts/tests/test_sd_space.py [path/to/SdFat/src]
Requires a hardware build's installed SdFat; does not download or patch it.
"""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / "freeink-sdk/libs/hardware/SDCardManager"


def method(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    sdfat = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / ".pio/libdeps/default/SdFat/src"
    if not (sdfat / "FatLib/FatPartition.cpp").is_file():
        sys.exit("SdFat source not found; run pio run -e default or pass its src directory.")
    source = (SDK / "src/SDCardManager.cpp").read_text()
    header = (SDK / "include/SDCardManager.h").read_text()
    with tempfile.TemporaryDirectory(prefix="crossmux-sd-space-") as directory:
        temp = Path(directory)
        # Compile the production bodies, not a second implementation of the calculation/cache.
        (temp / "methods.inc").write_text("\n".join(method(source, signature) for signature in (
            "uint64_t volumeTotalBytes(", "uint64_t SDCardManager::sdTotalBytes(",
            "bool SDCardManager::getSpace(", "uint64_t SDCardManager::sdUsedBytes(")))
        (temp / "cache.inc").write_text(header[header.index("  static constexpr uint32_t USED_BYTES_CACHE_TTL_MS"):
                                                header.index("  // All filesystem ops")])
        (temp / "host_compat.h").write_text("class __FlashStringHelper;\n")
        exe = temp / "sd_space_test"
        subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
            "-std=c++17", "-Wall", "-Wextra", "-DENABLE_ARDUINO_FEATURES=0",
            "-DENABLE_ARDUINO_SERIAL=0", "-DENABLE_ARDUINO_STRING=0",
            "-DUSE_BLOCK_DEVICE_INTERFACE=1", "-DSPI_DRIVER_SELECT=3",
            "-include", str(temp / "host_compat.h"), f"-I{sdfat}", f"-I{temp}",
            str(ROOT / "test/sd_space/sd_space_test.cpp"),
            str(sdfat / "FatLib/FatPartition.cpp"),
            str(sdfat / "ExFatLib/ExFatPartition.cpp"), str(sdfat / "common/FsCache.cpp"),
            "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
