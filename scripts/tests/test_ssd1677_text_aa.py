"""Keep SDK text/image routing regressions in CrossMux's Python CI suite."""
from pathlib import Path
import subprocess
import sys
import unittest


class Ssd1677TextAaTest(unittest.TestCase):
    def test_sdk_driver_and_routing(self):
        host = (Path(__file__).resolve().parents[2] / "freeink-sdk/libs/display/FreeInkDisplay/test/host")
        for script in ("test_sticky_combined_aa.py", "test_ssd1677_text_route.py"):
            with self.subTest(script=script):
                subprocess.run([sys.executable, str(host / script)], check=True)


if __name__ == "__main__":
    unittest.main()
