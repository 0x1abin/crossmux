#!/usr/bin/env python3
"""Export the build target contract; --check verifies a web checkout's snapshot."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from nightly_targets import TARGETS, ASSET_PROFILES, ASSET_OFFSETS, supported_channels


def contract():
    source = Path(__file__).with_name('nightly_targets.py')
    return {
        'schemaVersion': 1,
        'sourceSha': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source.parent, text=True).strip(),
        'sourceDigest': hashlib.sha256(source.read_bytes()).hexdigest(),
        'sourceDirty': subprocess.check_output(['git', 'show', 'HEAD:scripts/nightly_targets.py'], cwd=source.parent) != source.read_bytes(),
        'assetProfiles': ASSET_PROFILES,
        'assetOffsets': ASSET_OFFSETS,
        'targets': {key: {**value, 'supportedChannels': supported_channels(value)} for key, value in TARGETS.items()},
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', type=Path)
    args = parser.parse_args()
    value = contract()
    if args.check:
        existing = json.loads(args.check.read_text())
        # The source digest identifies the actual contract, even before its commit.
        for key in ('schemaVersion', 'sourceDigest', 'assetProfiles', 'assetOffsets', 'targets'):
            if existing.get(key) != value[key]:
                raise SystemExit(f'Target snapshot differs: {key}; regenerate before deployment')
        print('Firmware target snapshot matches')
    else:
        print(json.dumps(value, indent=2) + '\n', end='')
