"""Exercise the actual reader cadence against the actual SDK panel driver."""
import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
RUNNER = ROOT / "freeink-sdk/libs/display/FreeInkDisplay/test/host/test_ssd1677.py"
spec = importlib.util.spec_from_file_location("ssd1677_trace", RUNNER)
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)


class MetalioDisplayTest(unittest.TestCase):
    def test_driver_and_reader_sequences(self):
        trace.run_trace(ROOT / "test/metalio_refresh/test_reader_refresh.cpp", (
            ROOT / "test/metalio_refresh/stubs", ROOT / "lib/hal",
            ROOT / "src/activities/reader"))


if __name__ == "__main__":
    unittest.main()
