#!/usr/bin/env python3
"""Compare INX draw/hit traces with the pre-sync SDK (094976e1) golden data.

Optional argument: reference SDK root. This prints baseline hashes for review;
normal CTest runs compare the current SDK with the checked-in baseline.
"""
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SDK = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'freeink-sdk'
UI = SDK / 'libs/ui/FreeInkUI'
with tempfile.TemporaryDirectory(prefix='inx-style-', dir=os.environ.get('TMPDIR')) as tmp:
    binary = Path(tmp) / 'test'
    subprocess.run(['c++', '-std=c++17', '-I'+str(UI/'include'),
                    str(UI/'src/FreeInkUI.cpp'), str(HERE/'InxStyleParity.cpp'),
                    '-o', str(binary)], check=True)
    output = subprocess.check_output([str(binary)], text=True)
    # Compile the real drawAligned body, avoiding a duplicate of its arithmetic.
    adapter = (UI/'include/FreeInkUIGfxRenderer.h').read_text()
    start = adapter.index('const auto drawAligned = ')
    end = adapter.index('\n    };', start) + len('\n    };')
    alignment = Path(tmp)/'alignment.cpp'
    alignment.write_text((HERE/'InxTextCentering.cpp.in').read_text().replace(
        '@DRAW_ALIGNED@', adapter[start:end]))
    subprocess.run(['c++', '-std=c++17', '-I'+str(UI/'include'), str(alignment),
                    '-o', str(Path(tmp)/'alignment')], check=True)
    subprocess.run([str(Path(tmp)/'alignment')], check=True)

    if os.environ.get("INX_TRACE_PATH"):
        Path(os.environ["INX_TRACE_PATH"]).write_text(output)
    traces = {}
    boundary_cases = 0
    for block in output.split('SCENE ')[1:]:
        name, trace = block.split('\n', 1)
        # The new implementation draws the disjoint right scroll track last.
        # Normalize only that known non-overlapping order change, not text/row order.
        landscape, _, _, scene = map(int, name.split())
        width = 800 if landscape else 480
        lines = trace.splitlines()
        if scene == 154:
            # A short header can free room for another complete row. Validate
            # the repaired pagination separately from historical visual parity.
            hits = [list(map(int, line.split()[1:])) for line in lines if line.startswith("hit ")]
            bottom = 0
            for x, y, w, h, action, value in hits:
                assert y >= bottom and x >= 0 and x + w <= width
                assert y + h <= (480 if landscape else 800)
                bottom = y + h
            if landscape and int(name.split()[1]) and int(name.split()[2]) in (24, 32):
                old_count = 6 if int(name.split()[2]) == 24 else 4
                assert len(hits) == old_count + (len(sys.argv) == 1), len(hits)
            boundary_cases += 1
            continue
        track = [line for line in lines if (scene <= 3 or scene >= 151) and re.match(rf"fill {width-6} [0-9]+ 6 ", line)]
        # Long wrapped rows retain the new actual-height scrollbar calculation.
        # Compare every row/text/hit command, but validate the thumb separately.
        if scene == 3:
            for line in track:
                _, x, y, w, h, *_ = line.split()
                assert 0 <= int(y) and 0 < int(h) <= (480 if landscape else 800)
            compared_track = []
        else:
            compared_track = track
        normalized = "\n".join(compared_track + [line for line in lines if line not in track])
        traces[name] = hashlib.sha256(normalized.encode()).hexdigest()
    if len(sys.argv) > 1:
        print(json.dumps(traces, indent=2))
    else:
        expected = json.loads((HERE/'inx-style-baseline.json').read_text())
        mismatches = [name for name in sorted(set(expected) | set(traces))
                      if expected.get(name) != traces.get(name)]
        if mismatches:
            print('INX style mismatch:', ', '.join(mismatches))
            print(output)
            sys.exit(1)
        print(f'{len(traces)} INX visual/hit traces match SDK 094976e1; {boundary_cases} pagination boundary checks passed')
