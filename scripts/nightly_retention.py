#!/usr/bin/env python3
"""Plan deletion of Nightly builds not used by the current or previous index."""

import argparse
import json
import re
from pathlib import Path
from urllib.parse import urlparse

from nightly_targets import FLAVOR_TOKENS, TARGETS


BUILD_ID = r'[0-9a-f]{40}-[0-9]+-[0-9]+'
NIGHTLY_BUILD = rf'nightly-build-{BUILD_ID}'
NAMESPACE_TEXT = {'github': NIGHTLY_BUILD, 'cos': rf'(?:nightly-build-)?{BUILD_ID}'}
NAMESPACE = {storage: re.compile(rf'^{pattern}$') for storage, pattern in NAMESPACE_TEXT.items()}
FIND_NAMESPACE = {
    storage: re.compile(rf'(?<![a-z0-9-])({pattern})(?![a-z0-9-])')
    for storage, pattern in NAMESPACE_TEXT.items()
}
MANIFEST_PATH = {
    'github': re.compile(
        rf'^/0x1abin/crossmux/releases/download/({NIGHTLY_BUILD})/[^/]+-manifest\.json$'
    ),
    'cos': re.compile(rf'^/firmware/builds/((?:nightly-build-)?{BUILD_ID})/'),
}
HOST = {'github': 'github.com', 'cos': 'assets.crossmux.cn'}


def referenced_builds(index, storage):
    if not isinstance(index, dict) or index.get('schemaVersion') != 1 or index.get('channel') != 'nightly':
        raise ValueError('invalid previous Nightly index envelope')
    targets = index.get('targets')
    if not isinstance(targets, dict) or set(targets) != set(TARGETS):
        raise ValueError('previous Nightly index does not contain the canonical target set')

    builds = set()
    for target_id, entry in targets.items():
        variants = entry.get('variants') if isinstance(entry, dict) else None
        if not isinstance(entry, dict) or entry.get('targetId') != target_id or not isinstance(
            variants, dict
        ) or set(variants) != set(FLAVOR_TOKENS):
            raise ValueError(f'invalid previous {target_id} variant set')
        for flavor, pointer in variants.items():
            manifest_url = pointer.get('manifestUrl') if isinstance(pointer, dict) else None
            if not isinstance(manifest_url, str):
                raise ValueError(f'invalid previous {target_id}/{flavor} manifest URL')
            parsed = urlparse(manifest_url)
            match = MANIFEST_PATH[storage].match(parsed.path)
            if parsed.scheme != 'https' or parsed.hostname != HOST[storage] or parsed.query or not match:
                raise ValueError(f'unexpected previous {target_id}/{flavor} manifest URL')
            builds.add(match.group(1))
    return builds


def obsolete_builds(storage, current, previous_index, candidates):
    if not NAMESPACE[storage].fullmatch(current):
        raise ValueError(f'invalid current {storage} build name')
    found = set(FIND_NAMESPACE[storage].findall(candidates))
    if current not in found:
        raise ValueError(f'current {storage} build is missing from the candidate list')
    keep = referenced_builds(previous_index, storage) | {current}
    return sorted(found - keep)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--storage', choices=NAMESPACE, required=True)
    parser.add_argument('--current', required=True)
    parser.add_argument('--previous-index', type=Path, required=True)
    parser.add_argument('--candidates', type=Path, required=True)
    args = parser.parse_args()
    previous_index = json.loads(args.previous_index.read_text())
    candidates = args.candidates.read_text()
    for build in obsolete_builds(args.storage, args.current, previous_index, candidates):
        print(build)


if __name__ == '__main__':
    main()
