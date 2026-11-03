#!/usr/bin/env python3
"""Verify published firmware indexes, manifests, and binary assets."""

import argparse
import hashlib
import http.client
import io
import json
import re
import time
import urllib.error
import urllib.request
import zipfile
from pathlib import Path
from urllib.parse import urljoin, urlparse

from build_nightly_index import valid_manifest
from generate_ota_notes import validate_notes
from nightly_targets import (
    CHANNELS, FLAVOR_TOKENS, TARGETS, asset_name, bundle_name, manifest_name,
    public_target, targets_for,
)


SHA40 = re.compile(r'^[0-9a-f]{40}$')
SHA256 = re.compile(r'^[0-9a-f]{64}$')
ASSET_NAME = re.compile(r'^[a-z0-9][a-z0-9._-]*\.bin$')
OFFSETS = {'bootloader': 0x0000, 'partitions': 0x8000, 'boot_app0': 0xE000, 'firmware': 0x10000}


def fetch_bytes(url, attempts=3):
    request = urllib.request.Request(url, headers={'User-Agent': 'crossmux-firmware-verifier'})
    for attempt in range(attempts):
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                return response.read()
        except (http.client.IncompleteRead, urllib.error.URLError, TimeoutError) as error:
            if attempt + 1 == attempts:
                raise RuntimeError(f'failed to download {url}: {error}') from error
            time.sleep(2 ** attempt)


def read_json(url, fetch):
    try:
        return json.loads(fetch(url))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f'invalid JSON at {url}') from error


def local_fetch(index_url, index_file, artifacts):
    def fetch(url):
        if url == index_url:
            return index_file.read_bytes()
        name = urlparse(url).path.rsplit('/', 1)[-1]
        matches = [path for path in artifacts.rglob(name) if path.is_file()]
        if len(matches) != 1:
            raise ValueError(f'expected one local artifact {name}, found {len(matches)}')
        return matches[0].read_bytes()
    return fetch


def validate_url(url, index_url, channel):
    parsed = urlparse(url)
    index_host = urlparse(index_url).hostname
    if parsed.scheme != 'https':
        raise ValueError(f'published URL is not HTTPS: {url}')
    if index_host == 'github.com':
        valid = parsed.hostname == 'github.com' and parsed.path.startswith(
            f'/0x1abin/crossmux/releases/download/{channel}-build-'
        )
    elif index_host == 'assets.crossmux.cn':
        valid = parsed.hostname == 'assets.crossmux.cn' and parsed.path.startswith('/firmware/builds/')
    else:
        valid = False
    if not valid:
        raise ValueError(f'published URL is outside the expected release path: {url}')


def expected_assets(target_id, channel):
    return [(role, asset_name(target_id, f'{role}.bin'), offset) for role, offset in OFFSETS.items()]


def verify_bundle(entry, target_id, version, index_url, channel, manifests, fetch):
    bundle = entry.get('bundle')
    name = bundle_name(target_id, version)
    if not isinstance(bundle, dict) or bundle.get('name') != name or type(bundle.get('size')) is not int or (
        bundle['size'] <= 0 or not SHA256.fullmatch(str(bundle.get('sha256', '')))
    ) or not isinstance(bundle.get('url'), str):
        raise ValueError(f'invalid {target_id} bundle')
    validate_url(bundle['url'], index_url, channel)
    if urlparse(bundle['url']).path.rsplit('/', 1)[-1] != name:
        raise ValueError(f'invalid {target_id} bundle URL')
    data = fetch(bundle['url'])
    if len(data) != bundle['size'] or hashlib.sha256(data).hexdigest() != bundle['sha256']:
        raise ValueError(f'{target_id} bundle failed size or SHA-256 verification')
    assets = manifests['global']['assets']
    checksum_name = f"{TARGETS[target_id]['deviceSlug']}-SHA256SUMS"
    expected_names = {asset['name'] for asset in assets} | {
        manifest_name(target_id, flavor) for flavor in FLAVOR_TOKENS
    } | {checksum_name}
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            if set(archive.namelist()) != expected_names or len(archive.namelist()) != len(expected_names):
                raise ValueError(f'{target_id} bundle has unexpected contents')
            contents = {name: archive.read(name) for name in expected_names}
    except (zipfile.BadZipFile, RuntimeError) as error:
        raise ValueError(f'{target_id} bundle is not a readable ZIP') from error
    for asset in assets:
        content = contents[asset['name']]
        if len(content) != asset['size'] or hashlib.sha256(content).hexdigest() != asset['sha256']:
            raise ValueError(f'{target_id} bundle contains an invalid {asset["role"]}')
    for flavor, manifest in manifests.items():
        if json.loads(contents[manifest_name(target_id, flavor)]) != manifest:
            raise ValueError(f'{target_id} bundle manifest differs from published manifest')
    expected_checksums = {
        name: hashlib.sha256(content).hexdigest()
        for name, content in contents.items() if name != checksum_name
    }
    try:
        checksums = {}
        for line in contents[checksum_name].decode().splitlines():
            digest, filename = line.split('  ', 1)
            if filename in checksums:
                raise ValueError('duplicate checksum')
            checksums[filename] = digest
    except (UnicodeError, ValueError) as error:
        raise ValueError(f'{target_id} bundle has invalid checksums') from error
    if checksums != expected_checksums:
        raise ValueError(f'{target_id} bundle checksums do not match its contents')


def verify_release(index_url, expected_sha, channel, fetch=fetch_bytes):
    if not SHA40.fullmatch(expected_sha):
        raise ValueError('expected SHA must contain 40 lowercase hex characters')
    index = read_json(index_url, fetch)
    if not isinstance(index, dict) or index.get('schemaVersion') != 1 or index.get('channel') != channel:
        raise ValueError(f'invalid {channel} index envelope')
    release_notes = index.get('releaseNotes')
    if channel == 'stable' and release_notes is None:
        raise ValueError('Stable release notes are required')
    if release_notes is not None:
        if not isinstance(release_notes, dict) or set(release_notes) != set(FLAVOR_TOKENS):
            raise ValueError('invalid release notes')
        validate_notes({'en': release_notes['global'], 'zh': release_notes['zh-CN']})
    canonical_targets = targets_for(channel)
    targets = index.get('targets')
    if not isinstance(targets, dict) or set(targets) != set(canonical_targets):
        raise ValueError(f'published {channel} index does not contain the canonical target set')

    current_targets = 0
    asset_count = 0
    for target_id in canonical_targets:
        entry = targets[target_id]
        if not isinstance(entry, dict) or any(
            entry.get(key) != value
            for key, value in public_target(target_id).items()
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
            validate_url(manifest_url, index_url, channel)
            manifest = read_json(manifest_url, fetch)
            if not valid_manifest(manifest, target_id, flavor, channel):
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
        if revisions != {expected_sha}:
            raise ValueError(f'{target_id} does not point to the current revision')
        comparable = [
            {key: value for key, value in manifest.items() if key != 'flavor'}
            for manifest in manifests.values()
        ]
        if comparable[0] != comparable[1]:
            raise ValueError(f'{target_id} compatibility manifests differ beyond flavor')
        current_targets += 1

        expected = expected_assets(target_id, channel)
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
                ) or not ASSET_NAME.fullmatch(name) or name != neutral_name or asset.get(
                    'offset'
                ) != offset or type(asset.get('size')) is not int or asset['size'] <= 0 or not SHA256.fullmatch(
                    str(asset.get('sha256', ''))
                ):
                    raise ValueError(f'{target_id}/{flavor} has an invalid {role} asset')
                asset_url = urljoin(manifest_urls[flavor], name)
                validate_url(asset_url, index_url, channel)
                role_urls[role].add(asset_url)
                if asset_url in seen_urls:
                    continue
                seen_urls.add(asset_url)
                data = fetch(asset_url)
                if len(data) != asset['size'] or hashlib.sha256(data).hexdigest() != asset['sha256']:
                    raise ValueError(f'{target_id}/{name} failed size or SHA-256 verification')
                asset_count += 1
        if any(len(urls) != 1 for urls in role_urls.values()):
            raise ValueError(f'{target_id} compatibility manifests do not share one binary set')
        verify_bundle(entry, target_id, manifests['global']['version'], index_url, channel, manifests, fetch)

    return {'targets': len(targets), 'currentTargets': current_targets, 'assets': asset_count, 'bundles': len(targets)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--index-url', required=True)
    parser.add_argument('--expected-sha', required=True)
    parser.add_argument('--channel', choices=CHANNELS, required=True)
    parser.add_argument('--index-file', type=Path, help='Verify this local index instead of downloading it')
    parser.add_argument('--artifacts', type=Path, help='Resolve all indexed files under this local artifact directory')
    args = parser.parse_args()
    if (args.index_file is None) != (args.artifacts is None):
        parser.error('--index-file and --artifacts must be used together')
    fetch = local_fetch(args.index_url, args.index_file, args.artifacts) if args.index_file else fetch_bytes
    result = verify_release(args.index_url, args.expected_sha, args.channel, fetch)
    print(
        f"Verified {result['targets']} targets ({result['currentTargets']} current) "
        f"and {result['assets']} assets plus {result['bundles']} ZIP bundles from {args.index_url}"
    )


if __name__ == '__main__':
    main()
