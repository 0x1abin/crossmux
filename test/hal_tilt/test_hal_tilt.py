"""Exercise the real HAL gesture path with controlled IMU samples and time."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run():
    with tempfile.TemporaryDirectory(prefix="crossmux-tilt-") as directory:
        root = Path(directory)
        env = dict(os.environ, CCACHE_DIR=str(root / "ccache"))
        (root / "Arduino.h").write_text("#pragma once\n#include <cstdint>\n#include <cmath>\nextern unsigned long clockMs;\ninline unsigned long millis() { return clockMs; }\n")
        (root / "Logging.h").write_text("#pragma once\n#define LOG_ERR(...) ((void)0)\n#define LOG_INF(...) ((void)0)\n")
        (root / "BoardConfig.h").write_text("#pragma once\nnamespace BoardConfig { enum class ImuType { Sc7a20h }; struct Profile { struct { ImuType imuType; } sensors; }; inline constexpr Profile ACTIVE{{ImuType::Sc7a20h}}; }\n")
        (root / "Imu.h").write_text('''#pragma once
class Imu {
 public:
  struct Sample { float ax=0,ay=0,az=0,gx=0,gy=0,gz=0; };
  static Sample sample;
  static int reads;
  static bool readOk;
  bool begin() { return true; }
  bool wake() { return true; }
  bool sleep() { return true; }
  bool read(Sample& out) { ++reads; out=sample; return readOk; }
};
''')
        for pico in (0, 1):
            binary = root / f"tilt-{pico}"
            subprocess.run(["c++", "-std=c++17", f"-DFREEINK_DEVICE_READPICO={pico}",
                            "-I" + str(root), "-I" + str(ROOT / "lib/hal"),
                            str(ROOT / "lib/hal/HalTiltSensor.cpp"),
                            str(ROOT / "test/hal_tilt/gestures.cpp"), "-o", str(binary)], check=True, env=env)
            subprocess.run([str(binary)], check=True)
        print("Accelerometer cadence/baselines and existing gyro gestures passed")


if __name__ == "__main__":
    run()
