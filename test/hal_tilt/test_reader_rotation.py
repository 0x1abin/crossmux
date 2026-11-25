"""Compile the actual reader rotation admission policy; no device UI mocks."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="reader-rotation-") as temporary:
    root = Path(temporary)
    source = root / "test.cpp"
    source.write_text('''#include "ReaderAutoRotation.h"
#include <cassert>
#include <cstdio>
int main() {
 ReaderRotationContext state{true,true,false,false,false,0,1};
 assert(readerRotationAllowed(state));
 state.enabled=false; assert(!readerRotationAllowed(state)); state.enabled=true;
 state.pageReady=false; assert(!readerRotationAllowed(state)); state.pageReady=true;
 state.renderBusy=true; assert(!readerRotationAllowed(state)); state.renderBusy=false;
 state.overlay=true; assert(!readerRotationAllowed(state)); state.overlay=false;
 state.gesture=true; assert(!readerRotationAllowed(state)); state.gesture=false;
 state.desired=0; assert(!readerRotationAllowed(state));
 state.desired=4; assert(!readerRotationAllowed(state));
 state.desired=3; assert(readerRotationAllowed(state));
 std::puts("Reader rotation: disabled/loading/render/overlay/gesture/same/invalid targets rejected");
}
''')
    binary = root / "test"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(ROOT / "src/activities/reader"), str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
