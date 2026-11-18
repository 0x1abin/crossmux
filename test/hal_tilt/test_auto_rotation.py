"""Exercise the production gravity classifier without device or allocations."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="auto-rotation-") as temporary:
    exe = Path(temporary) / "test"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I" + str(ROOT / "lib/hal"), str(ROOT / "test/hal_tilt/auto_rotation.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
