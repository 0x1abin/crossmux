"""Guard the existing SDK-owned 2 KiB TLS fragment request (not peer acceptance)."""

import configparser
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


class WeReadTlsConfigTest(unittest.TestCase):
    def test_firmware_keeps_fragment_support_without_sni_wrapper(self):
        ini = (ROOT / "platformio.ini").read_text()
        config = configparser.ConfigParser(interpolation=None)
        config.read_string(ini)
        self.assertIn("-DHAVE_MAX_FRAGMENT", config["base"]["build_flags"].split())
        self.assertNotIn("--wrap=wolfSSL_UseSNI", ini)
        self.assertNotIn("wolfssl_max_fragment.cpp", ini)
        self.assertFalse((ROOT / "src/platform/wolfssl_max_fragment.cpp").exists())

    def test_pinned_sdk_requests_two_kib_once_before_handshake(self):
        path = ROOT / "freeink-sdk/libs/network/SecureNet/src/SecureClient.cpp"
        self.assertTrue(path.is_file(), "Initialize the pinned freeink-sdk submodule before testing")
        source = re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(), flags=re.S)
        call = r"wolfSSL_UseMaxFragment\s*\(\s*\w+\s*,\s*(\w+)\s*\)\s*;"
        self.assertEqual(re.findall(call, source), ["WOLFSSL_MFL_2_11"])
        self.assertRegex(source, r"#\s*ifdef\s+HAVE_MAX_FRAGMENT\s+" + call + r"\s*#\s*endif")
        self.assertLess(re.search(call, source).start(), re.search(r"wolfSSL_connect\s*\(", source).start())


if __name__ == "__main__":
    unittest.main()
