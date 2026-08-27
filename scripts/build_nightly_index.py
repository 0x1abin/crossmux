#!/usr/bin/env python3
"""Merge successful target pairs into a regional rolling Nightly index."""

import argparse
import json
from pathlib import Path
from urllib.parse import urljoin

from nightly_targets import FLAVOR_TOKENS, TARGETS, environment_for, manifest_name


def read_json(path):
    return json.loads(path.read_text())


def valid_manifest(manifest, target_id, flavor):
    target = TARGETS[target_id]
    return (
        manifest.get('schemaVersion') == 1
        and manifest.get('channel') == 'nightly'
        and manifest.get('targetId') == target_id
        and manifest.get('models') == target['models']
        and manifest.get('deviceSlug') == target['deviceSlug']
        and manifest.get('boardTag') == target['boardTag']
        and manifest.get('supportedChannels') == target['supportedChannels']
        and manifest.get('environment') == environment_for(target_id, flavor)
        and manifest.get('flavor') == flavor
        and isinstance(manifest.get('crossmuxSha'), str)
        and len(manifest['crossmuxSha']) == 40
        and isinstance(manifest.get('sdkSha'), str)
        and len(manifest['sdkSha']) == 40
        and isinstance(manifest.get('assets'), list)
        and any(
            asset.get('role') == 'firmware'
            and isinstance(asset.get('sha256'), str)
            and len(asset['sha256']) == 64
            for asset in manifest['assets']
        )
    )


def manifest_url(base_url, region, target_id, flavor):
    name = manifest_name(target_id, flavor)
    if region == 'global':
        return urljoin(base_url, name)
    return urljoin(base_url, f'{target_id}/{FLAVOR_TOKENS[flavor]}/{name}')


def firmware_sha(manifest):
    return next(asset.get('sha256') for asset in manifest['assets'] if asset.get('role') == 'firmware')


def build_index(manifest_root, previous, region, base_url, updated_at, build_id):
    previous_targets = previous.get('targets', {}) if previous else {}
    targets = {
        target_id: target
        for target_id, target in previous_targets.items()
        if target_id in TARGETS
    }
    for target_id, target in TARGETS.items():
        manifests = {}
        for flavor in FLAVOR_TOKENS:
            matches = list(manifest_root.rglob(manifest_name(target_id, flavor)))
            if len(matches) != 1:
                manifests = {}
                break
            manifest = read_json(matches[0])
            if not valid_manifest(manifest, target_id, flavor):
                raise ValueError(f'invalid {target_id}/{flavor} manifest')
            manifests[flavor] = manifest
        if len(manifests) != len(FLAVOR_TOKENS):
            continue
        revisions = {manifest['crossmuxSha'] for manifest in manifests.values()}
        if len(revisions) != 1:
            raise ValueError(f'{target_id} flavor revisions do not match')
        sdk_revisions = {manifest['sdkSha'] for manifest in manifests.values()}
        if len(sdk_revisions) != 1:
            raise ValueError(f'{target_id} flavor SDK revisions do not match')
        if len({manifest['version'] for manifest in manifests.values()}) != 1:
            raise ValueError(f'{target_id} flavor versions do not match')
        if len({firmware_sha(manifest) for manifest in manifests.values()}) != 1:
            raise ValueError(f'{target_id} flavor firmware hashes do not match')
        targets[target_id] = {
            'targetId': target_id,
            'models': target['models'],
            'deviceSlug': target['deviceSlug'],
            'boardTag': target['boardTag'],
            'supportedChannels': target['supportedChannels'],
            'variants': {
                flavor: {
                    'version': manifest['version'],
                    'crossmuxSha': manifest['crossmuxSha'],
                    'sdkSha': manifest['sdkSha'],
                    'publishedAt': updated_at,
                    'manifestUrl': manifest_url(base_url, region, target_id, flavor),
                }
                for flavor, manifest in manifests.items()
            },
        }
    return {
        'schemaVersion': 1,
        'channel': 'nightly',
        'updatedAt': updated_at,
        'buildId': build_id,
        'targets': targets,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--manifests', type=Path, required=True)
    parser.add_argument('--previous', type=Path)
    parser.add_argument('--region', choices=('global', 'cn'), required=True)
    parser.add_argument('--base-url', required=True)
    parser.add_argument('--updated-at', required=True)
    parser.add_argument('--build-id', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()

    previous = read_json(args.previous) if args.previous and args.previous.is_file() else None
    if previous and (previous.get('schemaVersion') != 1 or previous.get('channel') != 'nightly'):
        raise SystemExit('previous index has an unsupported schema')
    index = build_index(
        args.manifests,
        previous,
        args.region,
        args.base_url.rstrip('/') + '/',
        args.updated_at,
        args.build_id,
    )
    args.output.write_text(json.dumps(index, indent=2) + '\n')


if __name__ == '__main__':
    main()
