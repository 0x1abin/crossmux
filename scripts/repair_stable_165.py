#!/usr/bin/env python3
"""One-time catalog correction; never rebuild firmware or change a tag."""

import argparse
import hashlib
import json
import os
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path

from generate_ota_notes import extract_notes
from nightly_targets import TARGETS, asset_name, bundle_name, targets_for
from package_nightly_target import verify_firmware
from verify_nightly_release import fetch_bytes, local_fetch, verify_release

REPO = '0x1abin/crossmux'
VERSION = '1.6.5'
SOURCE_SHA = '6ec3289261ff99f1f321480f23b9e3148913ec24'
SOURCE_RUN = '40158384505'
BUILD_ID = f'stable-build-{SOURCE_SHA}-184-1'
URLS = {
    'global': f'https://github.com/{REPO}/releases/download/stable/release-index.json',
    'cn': 'https://assets.crossmux.cn/firmware/releases/stable/index.json',
    'version': f'https://github.com/{REPO}/releases/download/{VERSION}/release-index.json',
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def gh(*args):
    return subprocess.check_output(['gh', *args], text=True)


def api(path):
    return json.loads(gh('api', f'repos/{REPO}/{path}'))


def digest(data):
    return 'sha256:' + hashlib.sha256(data).hexdigest()


def capture(root):
    backup = root / 'backup'
    backup.mkdir(parents=True, exist_ok=False)
    for name, tag in [('stable', 'stable'), ('version', VERSION), ('immutable', BUILD_ID),
                      ('nightly', 'nightly'), ('previous-version', '1.6.0')]:
        (backup / f'{name}-release.json').write_text(json.dumps(api(f'releases/tags/{tag}')))
    for tag, name in [('stable', 'stable-global'), (VERSION, 'version-index'), ('nightly', 'nightly-global')]:
        gh('release', 'download', tag, '--repo', REPO, '--pattern', 'release-index.json',
           '--output', str(backup / f'{name}.json'))
    for channel in ('stable', 'nightly'):
        url = f'https://assets.crossmux.cn/firmware/releases/{channel}/index.json?repair={time.time_ns()}'
        (backup / f'{channel}-cn.json').write_bytes(fetch_bytes(url))
    ref = api(f'git/ref/tags/{VERSION}')
    require(ref['object']['type'] == 'tag', 'Expected annotated version tag')
    tag = api('git/tags/' + ref['object']['sha'])
    require(tag['object']['sha'] == SOURCE_SHA, 'Version tag moved from the original firmware commit')
    (backup / 'tag-ref.json').write_text(json.dumps(ref))
    (backup / 'tag-object.json').write_text(json.dumps(tag))
    run = api(f'actions/runs/{SOURCE_RUN}')
    require(run['head_sha'] == SOURCE_SHA and run['conclusion'] == 'success', 'Unexpected source run')


def candidate(index, notes, updated_at):
    eligible = targets_for('stable')
    require(len(eligible) == 4, 'Repair requires exactly four eligible Stable targets')
    require(index.get('schemaVersion') == 1 and index.get('channel') == 'stable'
            and index.get('buildId') == BUILD_ID, 'Unexpected Stable source index')
    require(set(index['targets']) in (set(TARGETS), set(eligible)), 'Unexpected source target set')
    require(index.get('releaseNotes') == {'global': notes['en'], 'zh-CN': notes['zh']}, 'OTA notes changed')
    for target in index['targets'].values():
        require(all(p['version'] == VERSION and p['crossmuxSha'] == SOURCE_SHA
                    for p in target['variants'].values()), 'Unexpected source firmware provenance')
    return {**index, 'updatedAt': updated_at,
            'targets': {key: index['targets'][key] for key in eligible}}


def prepare(root):
    backup = root / 'backup'
    release = json.loads((backup / 'version-release.json').read_text())
    require(release['tag_name'] == VERSION and not release['draft'] and not release['prerelease']
            and not release['immutable'], 'Unexpected version Release state')
    notes = extract_notes(release['body'])
    require(len(notes['en']) == len(notes['zh']) == 5, 'Expected five bilingual OTA notes')
    original = json.loads((backup / 'stable-global.json').read_text())
    require(original == json.loads((backup / 'version-index.json').read_text()), 'Version and rolling indexes differ')
    updated_at = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    for region in ('global', 'cn'):
        index = candidate(json.loads((backup / f'stable-{region}.json').read_text()), notes, updated_at)
        directory = root / region
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / 'release-index.json'
        path.write_text(json.dumps(index, indent=2) + '\n')
        result = verify_release(URLS[region], SOURCE_SHA, 'stable', local_fetch(URLS[region], path, root / 'artifacts'))
        require(result == {'targets': 4, 'currentTargets': 4, 'assets': 16, 'bundles': 4}, 'Unexpected validation count')
        print(region, result)
    for target_id, target in targets_for('stable').items():
        matches = list((root / 'artifacts').rglob(asset_name(target_id, 'firmware.bin')))
        require(len(matches) == 1, f'Missing unique original firmware for {target_id}')
        verify_firmware(matches[0], target['chipId'], target['boardTag'], VERSION)


def guard(root, region):
    if region == 'cn':
        current = digest(fetch_bytes(URLS['cn'] + f'?repair={time.time_ns()}'))
        permitted = {digest((root / 'backup/stable-cn.json').read_bytes()), digest((root / 'cn/release-index.json').read_bytes())}
        require(current in permitted, 'China Stable changed after backup')
    else:
        for tag, filename in [('stable', 'stable-global'), (VERSION, 'version-index')]:
            release = api(f'releases/tags/{tag}')
            label = 'stable' if tag == 'stable' else 'version'
            before = json.loads((root / f'backup/{label}-release.json').read_text())
            require(release['id'] == before['id'] and release['body'] == before['body'], 'Release changed after backup')
            asset = next((a for a in release['assets'] if a['name'] == 'release-index.json'), None)
            # gh --clobber can delete an index before a failed upload; retry the validated replacement.
            if asset is None:
                continue
            permitted = {digest((root / f'backup/{filename}.json').read_bytes()), digest((root / 'global/release-index.json').read_bytes())}
            require(asset['digest'] in permitted, f'{tag} index changed after backup')


def verify(root):
    output = os.environ.get('GITHUB_OUTPUT')
    try:
        for region in ('global', 'cn'):
            url = URLS[region] + f'?repair={time.time_ns()}'
            result = verify_release(url, SOURCE_SHA, 'stable')
            require(result == {'targets': 4, 'currentTargets': 4, 'assets': 16, 'bundles': 4}, 'Unexpected published target set')
            require(fetch_bytes(url) == (root / region / 'release-index.json').read_bytes(), f'{region} index differs from validated candidate')
            print(region, result, flush=True)
        require(fetch_bytes(URLS['version'] + f'?repair={time.time_ns()}') ==
                (root / 'global/release-index.json').read_bytes(), 'Version index differs from rolling Stable')
    except ValueError:
        if output:
            with open(output, 'a') as stream:
                stream.write('content_failure=true\n')
        raise
    # Snapshot releases contain volatile download counters; only compare immutable identity/content.
    for name, tag in [('immutable', BUILD_ID), ('nightly', 'nightly'), ('previous-version', '1.6.0')]:
        before = json.loads((root / f'backup/{name}-release.json').read_text())
        after = api(f'releases/tags/{tag}')
        fields = ('id', 'tag_name', 'body', 'draft', 'prerelease', 'published_at')
        require(all(before[k] == after[k] for k in fields), f'Protected {tag} Release changed')
        signatures = lambda r: sorted((a['id'], a['name'], a['size'], a.get('digest')) for a in r['assets'])
        require(signatures(before) == signatures(after), f'Protected {tag} assets changed')
    for region, url in [('global', f'https://github.com/{REPO}/releases/download/nightly/release-index.json'),
                        ('cn', 'https://assets.crossmux.cn/firmware/releases/nightly/index.json')]:
        require(fetch_bytes(url + f'?repair={time.time_ns()}') == (root / f'backup/nightly-{region}.json').read_bytes(), 'Nightly index changed')
    require(api(f'git/ref/tags/{VERSION}') == json.loads((root / 'backup/tag-ref.json').read_text()), 'Version tag changed')


def retire(root):
    index = json.loads((root / 'global/release-index.json').read_text())
    keep = {t['bundle']['name']: t['bundle'] for t in index['targets'].values()}
    obsolete = {bundle_name(key, VERSION) for key in TARGETS} - set(keep)
    for tag in ('stable', VERSION):
        release = api(f'releases/tags/{tag}')
        original_name = 'stable' if tag == 'stable' else 'version'
        original = json.loads((root / f'backup/{original_name}-release.json').read_text())
        require(release['id'] == original['id'] and release['body'] == original['body'], 'Release identity or notes changed')
        assets = {a['name']: a for a in release['assets']}
        require(set(assets) <= set(keep) | obsolete | {'release-index.json'}, 'Unexpected Release asset; refusing cleanup')
        require(set(keep) | {'release-index.json'} <= set(assets), 'Missing retained Release asset')
        require(assets['release-index.json']['digest'] == digest((root / 'global/release-index.json').read_bytes()), 'Release index not corrected')
        for name, bundle in keep.items():
            require(assets[name]['size'] == bundle['size'] and assets[name]['digest'] == 'sha256:' + bundle['sha256'], 'Retained ZIP changed')
        for name in sorted(obsolete & set(assets)):
            gh('release', 'delete-asset', tag, name, '--repo', REPO, '--yes')
            print('Removed', tag, name, flush=True)
    require(api('releases/latest')['tag_name'] == VERSION, 'Latest Release changed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('capture', 'prepare', 'guard', 'verify', 'retire'))
    parser.add_argument('--root', type=Path, default=Path('repair'))
    parser.add_argument('--region', choices=('global', 'cn'))
    args = parser.parse_args()
    if args.command == 'guard':
        require(args.region is not None, 'Guard requires region')
        guard(args.root, args.region)
    else:
        {'capture': capture, 'prepare': prepare, 'verify': verify, 'retire': retire}[args.command](args.root)


if __name__ == '__main__':
    main()
