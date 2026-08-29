#!/usr/bin/env python3
"""Build a complete regional rolling firmware index."""

import argparse
import json
from pathlib import Path
from urllib.parse import urljoin

from generate_ota_notes import extract_notes
from nightly_targets import CHANNELS, FLAVOR_TOKENS, TARGETS, environment_for, manifest_name, targets_for


def read_json(path):
    return json.loads(path.read_text())


def valid_manifest(manifest, target_id, flavor, channel):
    target = TARGETS[target_id]
    return (
        manifest.get('schemaVersion') == 1
        and manifest.get('channel') == channel
        and manifest.get('targetId') == target_id
        and manifest.get('models') == target['models']
        and manifest.get('deviceSlug') == target['deviceSlug']
        and manifest.get('boardTag') == target['boardTag']
        and manifest.get('supportedChannels') == target['supportedChannels']
        and manifest.get('environment') == environment_for(target_id, channel, flavor)
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
    return urljoin(base_url, f'{target_id}/{name}')


def build_index(manifest_root, region, base_url, updated_at, build_id, channel, release_notes=None):
    targets = {}
    crossmux_revisions = set()
    sdk_revisions = set()
    for target_id, target in targets_for(channel).items():
        manifests = {}
        for flavor in FLAVOR_TOKENS:
            matches = list(manifest_root.rglob(manifest_name(target_id, flavor)))
            if len(matches) != 1:
                raise ValueError(f'expected one {target_id}/{flavor} manifest, found {len(matches)}')
            manifest = read_json(matches[0])
            if not valid_manifest(manifest, target_id, flavor, channel):
                raise ValueError(f'invalid {target_id}/{flavor} manifest')
            manifests[flavor] = manifest
        revisions = {manifest['crossmuxSha'] for manifest in manifests.values()}
        if len(revisions) != 1:
            raise ValueError(f'{target_id} flavor revisions do not match')
        target_sdk_revisions = {manifest['sdkSha'] for manifest in manifests.values()}
        if len(target_sdk_revisions) != 1:
            raise ValueError(f'{target_id} flavor SDK revisions do not match')
        if len({manifest['version'] for manifest in manifests.values()}) != 1:
            raise ValueError(f'{target_id} flavor versions do not match')
        if len({json.dumps(manifest['assets'], sort_keys=True) for manifest in manifests.values()}) != 1:
            raise ValueError(f'{target_id} flavor assets do not match')
        crossmux_revisions.update(revisions)
        sdk_revisions.update(target_sdk_revisions)
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
    if len(crossmux_revisions) != 1:
        raise ValueError('target CrossMux revisions do not match')
    if len(sdk_revisions) != 1:
        raise ValueError('target SDK revisions do not match')
    if channel == 'stable' and release_notes is None:
        raise ValueError('Stable release notes are required')
    index = {
        'schemaVersion': 1,
        'channel': channel,
        'updatedAt': updated_at,
        'buildId': build_id,
        'targets': targets,
    }
    if release_notes is not None:
        index['releaseNotes'] = {
            'global': release_notes['en'],
            'zh-CN': release_notes['zh'],
        }
    return index


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--manifests', type=Path, required=True)
    parser.add_argument('--channel', choices=CHANNELS, required=True)
    parser.add_argument('--region', choices=('global', 'cn'), required=True)
    parser.add_argument('--base-url', required=True)
    parser.add_argument('--updated-at', required=True)
    parser.add_argument('--build-id', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--release-notes', type=Path)
    args = parser.parse_args()

    release_notes = extract_notes(args.release_notes.read_text()) if args.release_notes else None
    index = build_index(
        args.manifests,
        args.region,
        args.base_url.rstrip('/') + '/',
        args.updated_at,
        args.build_id,
        args.channel,
        release_notes,
    )
    args.output.write_text(json.dumps(index, indent=2) + '\n')


if __name__ == '__main__':
    main()
