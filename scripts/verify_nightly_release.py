#!/usr/bin/env python3
"""Verify published Nightly indexes, manifests, and binary assets."""

import argparse
import hashlib
import json
import re
import time
import urllib.error
import urllib.request
from urllib.parse import urljoin, urlparse

from build_nightly_index import valid_manifest
from nightly_targets import FLAVOR_TOKENS, TARGETS, asset_name, manifest_name


SHA40 = re.compile(r'^[0-9a-f]{40}$')
SHA256 = re.compile(r'^[0-9a-f]{64}$')
ASSET_NAME = re.compile(r'^[a-z0-9][a-z0-9._-]*\.bin$')
OFFSETS = {'bootloader': 0x0000, 'partitions': 0x8000, 'boot_app0': 0xE000, 'firmware': 0x10000}


def fetch_bytes(url, attempts=3):
    request = urllib.request.Request(url, headers={'User-Agent': 'crossmux-nightly-verifier'})
    for attempt in range(attempts):
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                return response.read()
        except (urllib.error.URLError, TimeoutError) as error:
            if attempt + 1 == attempts:
                raise RuntimeError(f'failed to download {url}: {error}') from error
            time.sleep(2 ** attempt)


def read_json(url, fetch):
    try:
        return json.loads(fetch(url))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f'invalid JSON at {url}') from error


def validate_url(url, index_url):
    parsed = urlparse(url)
    index_host = urlparse(index_url).hostname
    if parsed.scheme != 'https':
        raise ValueError(f'published URL is not HTTPS: {url}')
    if index_host == 'github.com':
        valid = parsed.hostname == 'github.com' and parsed.path.startswith(
            '/0x1abin/crossmux/releases/download/nightly-build-'
        )
    elif index_host == 'assets.crossmux.cn':
        valid = parsed.hostname == 'assets.crossmux.cn' and parsed.path.startswith('/firmware/builds/')
    else:
        valid = False
    if not valid:
        raise ValueError(f'published URL is outside the expected release path: {url}')


def expected_assets(target_id):
    roles = tuple(OFFSETS) if TARGETS[target_id]['fullInstall'] else ('firmware',)
    return [(role, asset_name(target_id, f'{role}.bin'), OFFSETS[role]) for role in roles]


def verify_release(index_url, expected_sha, fetch=fetch_bytes):
    if not SHA40.fullmatch(expected_sha):
        raise ValueError('expected SHA must contain 40 lowercase hex characters')
    index = read_json(index_url, fetch)
    if not isinstance(index, dict) or index.get('schemaVersion') != 1 or index.get('channel') != 'nightly':
        raise ValueError('invalid Nightly index envelope')
    targets = index.get('targets')
    if not isinstance(targets, dict) or set(targets) != set(TARGETS):
        raise ValueError('published Nightly index does not contain the canonical target set')

    current_targets = 0
    asset_count = 0
    for target_id, target in TARGETS.items():
        entry = targets[target_id]
        if not isinstance(entry, dict) or any(
            entry.get(key) != target[key]
            for key in ('models', 'deviceSlug', 'boardTag', 'supportedChannels')
        ) or entry.get('targetId') != target_id:
            raise ValueError(f'invalid {target_id} index entry')
        variants = entry.get('variants')
        if not isinstance(variants, dict) or set(variants) != set(FLAVOR_TOKENS):
            raise ValueError(f'invalid {target_id} variant set')

        manifests = {}
        manifest_urls = {}
        for flavor in FLAVOR_TOKENS:
            pointer = variants[flavor]
            if not isinstance(pointer, dict) or not SHA40.fullmatch(str(pointer.get('crossmuxSha', ''))) or not SHA40.fullmatch(
                str(pointer.get('sdkSha', ''))
            ) or not isinstance(pointer.get('version'), str) or not isinstance(pointer.get('publishedAt'), str):
                raise ValueError(f'invalid {target_id}/{flavor} pointer')
            manifest_url = pointer.get('manifestUrl')
            if not isinstance(manifest_url, str) or not manifest_url.endswith(manifest_name(target_id, flavor)):
                raise ValueError(f'invalid {target_id}/{flavor} manifest URL')
            validate_url(manifest_url, index_url)
            manifest = read_json(manifest_url, fetch)
            if not valid_manifest(manifest, target_id, flavor):
                raise ValueError(f'invalid {target_id}/{flavor} manifest')
            for key in ('version', 'crossmuxSha', 'sdkSha'):
                if manifest.get(key) != pointer.get(key):
                    raise ValueError(f'{target_id}/{flavor} manifest does not match its index pointer')
            manifests[flavor] = manifest
            manifest_urls[flavor] = manifest_url

        revisions = {manifest['crossmuxSha'] for manifest in manifests.values()}
        sdk_revisions = {manifest['sdkSha'] for manifest in manifests.values()}
        versions = {manifest['version'] for manifest in manifests.values()}
        if len(revisions) != 1 or len(sdk_revisions) != 1 or len(versions) != 1:
            raise ValueError(f'{target_id} compatibility manifest revisions differ')
        is_current = manifests['global']['crossmuxSha'] == expected_sha
        if is_current:
            comparable = [
                {key: value for key, value in manifest.items() if key != 'flavor'}
                for manifest in manifests.values()
            ]
            if comparable[0] != comparable[1]:
                raise ValueError(f'{target_id} compatibility manifests differ beyond flavor')
            current_targets += 1

        expected = expected_assets(target_id)
        seen_urls = set()
        role_urls = {role: set() for role, _name, _offset in expected}
        for flavor, manifest in manifests.items():
            assets = manifest['assets']
            if len(assets) != len(expected):
                raise ValueError(f'{target_id}/{flavor} has an unexpected asset count')
            for asset, (role, neutral_name, offset) in zip(assets, expected):
                name = asset.get('name') if isinstance(asset, dict) else None
                if not isinstance(asset, dict) or asset.get('role') != role or not isinstance(
                    name, str
                ) or not ASSET_NAME.fullmatch(name) or (is_current and name != neutral_name) or asset.get(
                    'offset'
                ) != offset or type(asset.get('size')) is not int or asset['size'] <= 0 or not SHA256.fullmatch(
                    str(asset.get('sha256', ''))
                ):
                    raise ValueError(f'{target_id}/{flavor} has an invalid {role} asset')
                asset_url = urljoin(manifest_urls[flavor], name)
                validate_url(asset_url, index_url)
                role_urls[role].add(asset_url)
                if asset_url in seen_urls:
                    continue
                seen_urls.add(asset_url)
                data = fetch(asset_url)
                if len(data) != asset['size'] or hashlib.sha256(data).hexdigest() != asset['sha256']:
                    raise ValueError(f'{target_id}/{name} failed size or SHA-256 verification')
                asset_count += 1
        if is_current and any(len(urls) != 1 for urls in role_urls.values()):
            raise ValueError(f'{target_id} compatibility manifests do not share one binary set')

    if current_targets == 0:
        raise ValueError('published index did not advance any target to the current revision')
    return {'targets': len(targets), 'currentTargets': current_targets, 'assets': asset_count}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--index-url', required=True)
    parser.add_argument('--expected-sha', required=True)
    args = parser.parse_args()
    result = verify_release(args.index_url, args.expected_sha)
    print(
        f"Verified {result['targets']} targets ({result['currentTargets']} current) "
        f"and {result['assets']} assets from {args.index_url}"
    )


if __name__ == '__main__':
    main()
