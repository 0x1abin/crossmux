"""Check a generated sdkconfig; archive/build provenance must also be recorded.

Passing this check permits further engineering validation, never programming.
"""

import argparse
from pathlib import Path


def validate_config(text: str) -> list[str]:
    values = {}
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
    required = {
        "CONFIG_IDF_TARGET": '"esp32s31"',
        "CONFIG_SPIRAM": "y",
        "CONFIG_SPIRAM_MODE_OCT": "y",
        "CONFIG_SPIRAM_SPEED_200M": "y",
        "CONFIG_SPIRAM_SPEED": "200",
        "CONFIG_SPIRAM_BOOT_INIT": "y",
        "CONFIG_SPIRAM_USE_MALLOC": "y",
        "CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL": "65536",
        "CONFIG_ESPTOOLPY_FLASHMODE_DIO": "y",
        "CONFIG_ESPTOOLPY_FLASHMODE": '"dio"',
        "CONFIG_ESPTOOLPY_FLASHFREQ_80M": "y",
        "CONFIG_ESPTOOLPY_FLASHFREQ": '"80m"',
        "CONFIG_ESPTOOLPY_FLASHSIZE_16MB": "y",
        "CONFIG_ESPTOOLPY_FLASHSIZE": '"16MB"',
        "CONFIG_PARTITION_TABLE_CUSTOM": "y",
        "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME": '"partitions_metalio_plus.csv"',
        "CONFIG_PARTITION_TABLE_OFFSET": "0x8000",
    }
    errors = [f"{key}: expected {expected}, got {values.get(key, 'unset')}"
              for key, expected in required.items() if values.get(key) != expected]
    forbidden = ("CONFIG_SPIRAM_IGNORE_NOTFOUND", "CONFIG_SPIRAM_SPEED_250M",
                 "CONFIG_SPIRAM_SPEED_100M", "CONFIG_SPIRAM_SPEED_40M",
                 "CONFIG_ESPTOOLPY_FLASHMODE_QIO", "CONFIG_ESPTOOLPY_FLASHMODE_QOUT",
                 "CONFIG_ESPTOOLPY_FLASHMODE_DOUT")
    errors.extend(f"{key} must be disabled" for key in forbidden if values.get(key) == "y")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdkconfig", type=Path, help="Generated core sdkconfig, not the defaults file")
    args = parser.parse_args()
    errors = validate_config(args.sdkconfig.read_text())
    for error in errors:
        print(error)
    if errors:
        return 1
    print("S31 memory configuration matches the 200MHz contract; runtime acceptance remains pending.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
