#!/usr/bin/env python3
"""Publish firmware assets without replacing immutable bytes; update channels last."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import urllib.error
import urllib.request
from urllib.parse import quote, urljoin, urlparse

from nightly_targets import compatibility_aliases, manifest_name

REPO = '0x1abin/crossmux'


def gh(*args):
    return subprocess.check_output(['gh', *args], text=True)


def release(tag):
    result = subprocess.run(
        ['gh', 'api', f'repos/{REPO}/releases/tags/{quote(tag, safe="")}'],
        capture_output=True, text=True,
    )
    if result.returncode:
        if 'HTTP 404' in result.stderr:
            return None
        raise RuntimeError(result.stderr)
    return json.loads(result.stdout)


def download(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'crossmux-release'})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()


def same_asset(path, asset):
    data = path.read_bytes()
    digest = 'sha256:' + hashlib.sha256(data).hexdigest()
    if asset.get('size') != len(data):
        return False
    if asset.get('digest'):
        return asset['digest'] == digest
    # gh can read draft assets as well as published assets.
    remote = subprocess.check_output([
        'gh', 'api', f'repos/{REPO}/releases/assets/{asset["id"]}',
        '-H', 'Accept: application/octet-stream',
    ])
    return remote == data


def upload_assets(tag, paths, existing, mutable=False):
    assets = {asset['name']: asset for asset in existing['assets']}
    pending = []
    names = set()
    for path in paths:
        if path.name in names:
            raise ValueError(f'duplicate asset name: {path.name}')
        names.add(path.name)
        asset = assets.get(path.name)
        if asset and same_asset(path, asset):
            continue
        if asset and not mutable:
            raise ValueError(f'immutable asset differs: {tag}/{path.name}')
        pending.append(path)
    # Check every conflict before uploading even the first missing file.
    for path in pending:
        args = ['release', 'upload', tag, str(path), '--repo', REPO]
        if mutable:
            args.append('--clobber')
        gh(*args)


def publish_assets(tag, sha, channel, title, notes, paths):
    existing = release(tag)
    if existing is None:
        gh('release', 'create', tag, '--repo', REPO, '--target', sha,
           '--title', title, '--notes-file', str(notes), '--draft', '--latest=false',
           *(['--prerelease'] if channel == 'nightly' else ['--verify-tag']))
        existing = release(tag)
    upload_assets(tag, paths, existing)
    if existing['draft']:
        gh('release', 'edit', tag, '--repo', REPO, '--draft=false', '--latest=false')


def release_identity(index):
    pointers = [pointer for target in index['targets'].values() for pointer in target['variants'].values()]
    if not pointers or len({p['crossmuxSha'] for p in pointers}) != 1 or len({p['sdkSha'] for p in pointers}) != 1:
        raise ValueError('release targets do not share a source revision')
    versions = {p['version'] for p in pointers}
    if index['channel'] == 'stable' and len(versions) != 1:
        raise ValueError('Stable targets do not share a version')
    return pointers[0]['crossmuxSha'], pointers[0]['version']


def channel_alias_data(index):
    result = {}
    for name, (target_id, role) in compatibility_aliases(index['channel'], 'channel').items():
        pointer = index['targets'][target_id]['variants']['global']
        manifest = json.loads(download(pointer['manifestUrl']))
        if any(manifest.get(key) != pointer[key] for key in ('version', 'crossmuxSha', 'sdkSha')):
            raise ValueError('compatibility manifest differs from index')
        assets = [asset for asset in manifest['assets'] if asset['role'] == role]
        if len(assets) != 1:
            raise ValueError(f'missing compatibility resource: {name}')
        asset = assets[0]
        data = download(urljoin(pointer['manifestUrl'], asset['name']))
        if len(data) != asset['size'] or hashlib.sha256(data).hexdigest() != asset['sha256']:
            raise ValueError(f'compatibility resource failed verification: {name}')
        result[name] = data
    return result


def package_aliases(root, channel):
    for name, (target_id, role) in compatibility_aliases(channel, channel).items():
        matches = list(root.rglob(manifest_name(target_id, 'global')))
        if len(matches) != 1:
            raise ValueError(f'missing compatibility manifest: {target_id}')
        manifest = json.loads(matches[0].read_text())
        assets = [asset for asset in manifest['assets'] if asset['role'] == role]
        if len(assets) != 1:
            raise ValueError(f'missing compatibility resource: {name}')
        asset = assets[0]
        data = (matches[0].parent / asset['name']).read_bytes()
        if len(data) != asset['size'] or hashlib.sha256(data).hexdigest() != asset['sha256']:
            raise ValueError(f'compatibility resource failed verification: {name}')
        output = root / 'compatibility' / name
        if output.exists() and output.read_bytes() != data:
            raise ValueError(f'compatibility resource differs: {name}')
        output.parent.mkdir(exist_ok=True)
        output.write_bytes(data)


def publish_channel(channel, index_file):
    index = json.loads(index_file.read_text())
    if index.get('channel') != channel:
        raise ValueError('candidate index channel does not match publication channel')
    sha, _version = release_identity(index)
    tags = set()
    for target in index['targets'].values():
        for pointer in target['variants'].values():
            url = urlparse(pointer['manifestUrl'])
            prefix = f'/{REPO}/releases/download/'
            if url.scheme != 'https' or url.netloc != 'github.com' or not url.path.startswith(prefix):
                raise ValueError('channel manifest is not in the firmware repository')
            tags.add(url.path[len(prefix):].split('/')[0])
    if len(tags) != 1:
        raise ValueError('channel manifests do not share a release')
    tag = tags.pop()
    aliases = channel_alias_data(index)
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        notes = root / 'body.md'
        notes.write_text(
            ('最新正式版固件 / Latest stable firmware.\n\n' if channel == 'stable'
             else '最新测试版固件 / Latest nightly firmware.\n\n')
            + '[官网烧录工具 / Web flasher](https://crossmux.com/#flash-tools) · '
            + f'[版本详情 / Release details](https://github.com/{REPO}/releases/tag/{tag})\n'
        )
        existing = release(channel)
        if existing is None:
            gh('release', 'create', channel, '--repo', REPO, '--target', sha,
               '--title', f'CrossMux {channel.title()}', '--notes-file', str(notes),
               '--prerelease', '--latest=false', '--draft')
            existing = release(channel)
        paths = []
        for name, data in aliases.items():
            path = root / name
            path.write_bytes(data)
            paths.append(path)
        rolling = root / 'release-index.json'
        rolling.write_bytes(index_file.read_bytes())
        upload_assets(channel, [*paths, rolling], existing, mutable=True)
        gh('release', 'edit', channel, '--repo', REPO, '--draft=false', '--prerelease',
           '--latest=false', '--title', f'CrossMux {channel.title()}', '--notes-file', str(notes))


def publish_cos(root, build_id, cli):
    args = [str(cli), 'cp', '--init-skip', '--disable-log', '-i', os.environ['COS_SECRET_ID'],
            '-k', os.environ['COS_SECRET_KEY'], '-e', f'cos.{os.environ["COS_REGION"]}.myqcloud.com']
    if os.environ.get('COS_SESSION_TOKEN'):
        args += ['--token', os.environ['COS_SESSION_TOKEN']]
    pending = []
    for folder in sorted(root.glob('firmware-*')):
        target = folder.name.rsplit('-', 1)[1]
        for path in sorted(folder.iterdir()):
            key = f'firmware/builds/{build_id}/{target}/{path.name}'
            try:
                remote = download(f'https://assets.crossmux.cn/{key}')
            except urllib.error.HTTPError as error:
                if error.code != 404:
                    raise
                pending.append((path, key))
                continue
            if remote != path.read_bytes():
                raise ValueError(f'immutable COS object differs: {key}')
    for path, key in pending:
        result = subprocess.run(args + [str(path), f'cos://{os.environ["COS_BUCKET"]}/{key}',
                                '--forbid-overwrite', '--meta', 'Cache-Control:public,max-age=31536000,immutable'])
        if result.returncode:
            raise RuntimeError(f'COS upload failed: {key}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    assets = commands.add_parser('github-assets')
    assets.add_argument('--tag', required=True)
    assets.add_argument('--sha', required=True)
    assets.add_argument('--channel', choices=['stable', 'nightly'], required=True)
    assets.add_argument('--title', required=True)
    assets.add_argument('--notes', type=Path, required=True)
    assets.add_argument('--assets', type=Path, required=True)
    channel = commands.add_parser('channel')
    channel.add_argument('--channel', choices=['stable', 'nightly'], required=True)
    channel.add_argument('--index', type=Path, required=True)
    cos = commands.add_parser('cos-assets')
    cos.add_argument('--assets', type=Path, required=True)
    cos.add_argument('--build-id', required=True)
    cos.add_argument('--cli', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'github-assets':
        package_aliases(args.assets, args.channel)
        publish_assets(args.tag, args.sha, args.channel, args.title, args.notes,
                       sorted(path for path in args.assets.rglob('*') if path.is_file()))
    elif args.command == 'channel':
        publish_channel(args.channel, args.index)
    else:
        publish_cos(args.assets, args.build_id, args.cli)


if __name__ == '__main__':
    main()
