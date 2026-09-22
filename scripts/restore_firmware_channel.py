#!/usr/bin/env python3
"""Restore a missing GitHub channel from the current, verified COS release."""

import argparse
import copy
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from publish_firmware import REPO, download, publish_channel, release, same_asset, release_identity, channel_alias_data


def release_state(value):
    if value is None:
        return None
    return (value['id'], value['draft'], value['updated_at'],
            [(a['id'], a['name'], a.get('digest'), a['updated_at']) for a in value['assets']])


def identity(index):
    value = copy.deepcopy(index)
    value.pop('updatedAt', None)
    for target in value['targets'].values():
        for pointer in target['variants'].values():
            pointer.pop('manifestUrl', None)
            pointer.pop('publishedAt', None)
    return value


def global_index(source, tag):
    result = copy.deepcopy(source)
    for target in result['targets'].values():
        for pointer in target['variants'].values():
            name = pointer['manifestUrl'].rsplit('/', 1)[1]
            pointer['manifestUrl'] = f'https://github.com/{REPO}/releases/download/{tag}/{name}'
    return result


def verify_snapshot(index, index_url, sha, root):
    candidate = root / 'candidate.json'
    candidate.write_text(json.dumps(index))
    # Use the target contract that produced the release, including pre-Sticky-Stable builds.
    code = '''import pathlib, sys
sys.path.insert(0, str(pathlib.Path.cwd() / 'scripts'))
from verify_nightly_release import verify_release, fetch_bytes
candidate, index_url, sha, channel = sys.argv[1:]
def fetch(url):
    data = pathlib.Path(candidate).read_bytes() if url == index_url else fetch_bytes(url)
    return data
print(verify_release(index_url, sha, channel, fetch=fetch))
'''
    subprocess.run([sys.executable, '-c', code, str(candidate), index_url, sha, index['channel']],
                   cwd=root, check=True)


def restore(channel):
    source_url = f'https://assets.crossmux.cn/firmware/releases/{channel}/index.json'
    index_url = f'https://github.com/{REPO}/releases/download/{channel}/release-index.json'
    source = json.loads(download(source_url))
    match = re.fullmatch(rf'{channel}-build-([0-9a-f]{{40}})-[0-9]+-[0-9]+', source.get('buildId', ''))
    if source.get('channel') != channel or not match:
        raise ValueError('invalid source channel/build ID')
    sha = match[1]
    existing = release(channel)
    current = None
    if existing and existing['draft']:
        raise ValueError('channel exists as a draft; inspect it before recovery')
    if existing and any(asset['name'] == 'release-index.json' for asset in existing['assets']):
        current = json.loads(download(index_url))
        if identity(current) != identity(source):
            raise ValueError('regional channels differ; refusing to replace or roll back the GitHub channel')
    tag = source['buildId']
    archive = release(tag)
    if archive is None and channel == 'stable':
        _sha, tag = release_identity(source)
        if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', tag):
            raise ValueError('invalid Stable version')
        archive = release(tag)
    if archive is None or archive['draft']:
        raise ValueError('published GitHub build resources are missing; recovery will not rebuild firmware')
    candidate = current or global_index(source, tag)
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        with (root / 'contract.tar').open('wb') as output:
            subprocess.run(['git', 'archive', '--format=tar', sha, 'scripts'], stdout=output, check=True)
        subprocess.run(['tar', '-xf', str(root / 'contract.tar'), '-C', str(root)], check=True)
        verify_snapshot(source, source_url, sha, root)
        for target_id, target in source['targets'].items():
            for flavor, pointer in target['variants'].items():
                remote = candidate['targets'][target_id]['variants'][flavor]['manifestUrl']
                if json.loads(download(pointer['manifestUrl'])) != json.loads(download(remote)):
                    raise ValueError(f'regional manifests differ: {target_id}/{flavor}')
        verify_snapshot(candidate, index_url, sha, root)
        aliases = channel_alias_data(candidate)
        missing_alias = False
        for name, data in aliases.items():
            path = root / name
            path.write_bytes(data)
            alias = next((asset for asset in (existing or {}).get('assets', []) if asset['name'] == name), None)
            if alias and not same_asset(path, alias):
                raise ValueError('existing channel firmware differs; inspect it before recovery')
            missing_alias |= alias is None
        # The workflow shares the publication lock; also reject changes during a local invocation.
        if json.loads(download(source_url)) != source or release_state(release(channel)) != release_state(existing):
            raise ValueError('channel changed during verification; retry after the publisher finishes')
        if current is not None and not missing_alias:
            print(f'{channel}: healthy channel already exists; unchanged')
            return
        index_file = root / 'release-index.json'
        index_file.write_text(json.dumps(candidate, indent=2) + '\n')
        publish_channel(channel, index_file)
        verify_snapshot(json.loads(download(index_url)), index_url, sha, root)
        print(f'Restored {channel}: {tag}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--channel', choices=['stable', 'nightly'], required=True)
    restore(parser.parse_args().channel)
