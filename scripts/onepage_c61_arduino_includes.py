"""Add the Arduino core/variant include paths for the C61 skeleton build.

The pioarduino C61 path assembles the Arduino core from
framework-arduino-c61-skeleton-lib after the pre-scripts run. Its generated
.dummy sketch and the application otherwise receive the IDF include graph but
not the Arduino core/variant directories, so Arduino.h is not found. Resolve
the paths from the selected board instead of hard-coding this checkout.
"""

Import("env")  # noqa: F821 - provided by PlatformIO

from pathlib import Path

package_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
if not package_dir:
    raise RuntimeError("framework-arduinoespressif32 is not installed")

variant = env.BoardConfig().get("build.variant", "esp32c61")
core = Path(package_dir) / "cores" / "esp32"
variant_dir = Path(package_dir) / "variants" / variant
if not (core / "Arduino.h").is_file():
    raise RuntimeError(f"Arduino core headers not found at {core}")
if not variant_dir.is_dir():
    raise RuntimeError(f"Arduino variant directory not found at {variant_dir}")

env.Append(BUILD_FLAGS=[f"-I{core}", f"-I{variant_dir}"])
