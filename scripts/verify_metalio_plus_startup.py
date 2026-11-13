"""Verify deferred input startup in an actual, already-built Plus ELF.

Run with the target toolchain's objdump. This check does not access hardware.
It covers the HAL wiring omitted by isolated input/backend host harnesses.
"""

import argparse
from pathlib import Path
import re
import subprocess


def validate(disassembly):
    expected = ('HalDisplay::begin(bool)', 'HalGPIO::beginInput()', 'GfxRenderer::begin()')
    locations = []
    for name in expected:
        calls = list(re.finditer(r'\b(?:jal|jalr|call)\s+[^\n]*<' + re.escape(name) + r'>', disassembly))
        if len(calls) != 1:
            return [f'Expected one compiled startup call to {name}; found {len(calls)}']
        locations.append(calls[0].start())
    if locations != sorted(locations):
        return ['Input must begin after display and before renderer startup']
    return []


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--objdump', default='riscv32-esp-elf-objdump')
    parser.add_argument('--save-disassembly', type=Path)
    args = parser.parse_args(argv)
    result = subprocess.run([args.objdump, '-d', '-C',
                             '--disassemble=setupDisplayAndFonts(bool, bool)', str(args.elf)],
                            check=True, capture_output=True, text=True)
    if args.save_disassembly:
        args.save_disassembly.write_text(result.stdout)
    errors = validate(result.stdout)
    for error in errors:
        print(error)
    if errors:
        return 1
    print('Compiled Plus startup verified: display -> input -> renderer')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
