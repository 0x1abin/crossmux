"""Reject mismatched generated cores before considering a hardware test."""

import importlib.util
from pathlib import Path
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "verify_metalio_plus_core.py"
spec = importlib.util.spec_from_file_location("metalio_plus_core", SCRIPT)
core = importlib.util.module_from_spec(spec)
spec.loader.exec_module(core)

VALID = '''CONFIG_IDF_TARGET="esp32s31"
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_200M=y
CONFIG_SPIRAM_SPEED=200
CONFIG_SPIRAM_BOOT_INIT=y
CONFIG_SPIRAM_USE_MALLOC=y
# CONFIG_SPIRAM_IGNORE_NOTFOUND is not set
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y
CONFIG_ESPTOOLPY_FLASHMODE="dio"
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESPTOOLPY_FLASHFREQ="80m"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_metalio_plus.csv"
CONFIG_PARTITION_TABLE_OFFSET=0x8000
'''


class CoreContractTest(unittest.TestCase):
    def test_accepted_memory_configuration(self):
        self.assertEqual(core.validate_config(VALID), [])

    def test_stale_250mhz_core_is_rejected(self):
        self.assertTrue(core.validate_config(VALID.replace("SPEED_200M", "SPEED_250M").replace("SPEED=200", "SPEED=250")))

    def test_wrong_flash_mode_frequency_or_capacity_is_rejected(self):
        for old, new in (("FLASHMODE_DIO", "FLASHMODE_QIO"), ('FLASHMODE="dio"', 'FLASHMODE="qio"'),
                         ("FLASHFREQ_80M", "FLASHFREQ_40M"), ('FLASHSIZE="16MB"', 'FLASHSIZE="8MB"')):
            with self.subTest(old=old):
                self.assertTrue(core.validate_config(VALID.replace(old, new)))

    def test_missing_memory_prerequisites_are_rejected(self):
        for line in ("CONFIG_SPIRAM=y", "CONFIG_SPIRAM_BOOT_INIT=y", "CONFIG_SPIRAM_MODE_OCT=y",
                     "CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536"):
            with self.subTest(line=line):
                self.assertTrue(core.validate_config(VALID.replace(line, "")))
        self.assertTrue(core.validate_config(VALID.replace("# CONFIG_SPIRAM_IGNORE_NOTFOUND is not set",
                                                        "CONFIG_SPIRAM_IGNORE_NOTFOUND=y")))

    def test_conflicting_active_clock_and_flash_choices_are_rejected(self):
        for extra in ("CONFIG_SPIRAM_SPEED_250M=y", "CONFIG_ESPTOOLPY_FLASHMODE_QIO=y"):
            with self.subTest(extra=extra):
                self.assertTrue(core.validate_config(VALID + extra + "\n"))

    def test_wrong_partition_contract_is_rejected(self):
        for old, new in (("partitions_metalio_plus.csv", "partitions.csv"), ("OFFSET=0x8000", "OFFSET=0x10000"),
                         ("CONFIG_PARTITION_TABLE_CUSTOM=y", "CONFIG_PARTITION_TABLE_SINGLE_APP=y")):
            with self.subTest(old=old):
                self.assertTrue(core.validate_config(VALID.replace(old, new)))


if __name__ == "__main__":
    unittest.main()
