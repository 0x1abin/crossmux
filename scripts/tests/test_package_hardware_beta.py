import importlib.util
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / 'package_hardware_beta.py'
SPEC = importlib.util.spec_from_file_location('package_hardware_beta', SCRIPT)
package_hardware_beta = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package_hardware_beta)


class FirmwareValidationTest(unittest.TestCase):
    def test_all_s3_beta_device_mappings(self):
        self.assertEqual(package_hardware_beta.DEVICES, {
            'sticky': 'sticky',
            'x4pro': 'x4pro',
            'papermono': 'papermono',
            'eego-a4': 'eego_a4',
            'murphy-m4': 'murphy_m4',
            'waveshare-epaper-397': 'waveshare_epaper_397',
        })

    def write_image(self, chip_id=package_hardware_beta.ESP32S3_CHIP_ID, board='eego_a4'):
        image = bytearray(24)
        image[0] = 0xE9
        image[12:14] = chip_id.to_bytes(2, 'little')
        image.extend(f'CROSSPOINT-BOARD-V1:{board};'.encode())
        temp = tempfile.NamedTemporaryFile(delete=False)
        temp.write(image)
        temp.close()
        self.addCleanup(Path(temp.name).unlink)
        return Path(temp.name)

    def test_accepts_matching_s3_board_tag(self):
        package_hardware_beta.verify_firmware(self.write_image(), 'eego_a4')

    def test_rejects_wrong_chip_or_board(self):
        with self.assertRaises(SystemExit):
            package_hardware_beta.verify_firmware(self.write_image(chip_id=5), 'eego_a4')
        with self.assertRaises(SystemExit):
            package_hardware_beta.verify_firmware(self.write_image(board='murphy_m4'), 'eego_a4')


if __name__ == '__main__':
    unittest.main()
