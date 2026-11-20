"""S31 software contracts; these checks do not replace physical acceptance."""
import configparser
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / 'freeink-sdk'


class MetalioPlusTest(unittest.TestCase):
    def test_driver_and_input(self):
        subprocess.run(['python3', str(SDK / 'libs/display/FreeInkDisplay/test/host/run_metalio_plus_native_gray.py')], check=True)
        subprocess.run(['python3', str(SDK / 'libs/display/FreeInkDisplay/test/host/run_epdiy_policy.py')], check=True)
        subprocess.run(['python3', str(SDK / 'libs/display/FreeInkDisplay/test/host/run_epdiy_sleep.py')], check=True)
        subprocess.run(['sh', str(SDK / 'libs/hardware/InputManager/test/host/run_metalioplus_touch.sh')], check=True)
        subprocess.run(['sh', str(SDK / 'libs/hardware/InputManager/test/host/run_buffered_touch.sh')], check=True)

    def test_usb_msc(self):
        subprocess.run(['python3', str(SDK / 'libs/hardware/UsbMassStorage/test/host/run_usb_msc.py')], check=True)

    def test_toolchain_and_layout_are_separate_from_s3(self):
        config = configparser.ConfigParser()
        config.read(ROOT / 'platformio.ini')
        board = config['metalio_eink4_plus_hardware']
        self.assertEqual(board['extends'], 'base')
        self.assertEqual(board['board'], 'esp32-s31-coreboard-1')
        self.assertIn('/61.04.00-RC1/', board['platform'])
        self.assertIn('-DCROSSMUX_UI_PROFILE_HIGH_DPI', board['build_flags'])
        self.assertNotIn('s3_ble', str(dict(board)))
        self.assertIn('-std=gnu23', board['build_unflags'])
        self.assertIn('pre:scripts/configure_s31_c.py', board['extra_scripts'])
        self.assertNotIn('metalio_eink4_plus-gh_release', config)
        self.assertEqual(config['env:metalio_eink4_plus']['extends'], 'metalio_eink4_plus_hardware, mode_dev')

    def test_board_tag_rejects_other_boards_at_every_split(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            (tmp / 'BoardConfig.h').write_text('#define FREEINK_DEVICE_METALIO_EINK4_PLUS 1\n')
            (tmp / 'test.cpp').write_text(r'''
#include <FirmwareBoardTag.h>
#include <cassert>
#include <cstring>
int main() {
 const char* tags[]={"CROSSPOINT-BOARD-V1:metalio_eink4_plus;", "CROSSPOINT-BOARD-V1:metalio_eink4;",
                     "CROSSPOINT-BOARD-V1:readpico;", "CROSSPOINT-BOARD-V1:x4;"};
 assert(board_tag::boardNameLen()==std::strlen("metalio_eink4_plus"));
 for(int i=0;i<4;++i)for(size_t split=0;split<=std::strlen(tags[i]);++split){
  board_tag::Scanner s;s.feed((const uint8_t*)tags[i],split);
  s.feed((const uint8_t*)tags[i]+split,std::strlen(tags[i])-split);
  assert(s.mismatch()==(i!=0));
 }
}
''')
            binary = tmp / 'test'
            subprocess.run(['c++', '-std=c++17', '-I' + str(tmp), '-I' + str(ROOT / 'src/network'),
                            str(tmp / 'test.cpp'), str(ROOT / 'src/network/FirmwareBoardTag.cpp'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
