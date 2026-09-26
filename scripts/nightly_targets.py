"""Canonical firmware target mapping and artifact naming."""


TARGETS = {
    'xteink_x4': {
        'deviceSlug': 'xteink',
        'models': ['xteink_x3', 'xteink_x4'],
        'boardTag': 'x4',
        'chip': 'ESP32-C3',
        'chipId': 0x0005,
        'environments': {'stable': 'gh_release', 'nightly': 'gh_release_rc'},
        'assetProfile': 'c3-ota-v1',
        'legacyAssetProfiles': {'stable': 'c3-ota-v1', 'nightly': 'app-only-v1'},
        'installPolicy': 'c3-migrate',
        'nightlyVersionSuffix': '',
        'legacyChineseEnvironment': 'gh_release_cn_rc',
        'compatibilityAliases': {
            'channel': {'firmware.bin': 'firmware'},
            'stable': {'firmware.bin': 'firmware', 'firmware-cn.bin': 'firmware',
                       'bootloader.bin': 'bootloader', 'partitions.bin': 'partitions'},
        },
    },
    'sticky': {
        'deviceSlug': 'sticky',
        'models': ['sticky'],
        'boardTag': 'sticky',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'stable': 'sticky-gh_release', 'nightly': 'sticky_nightly'},
        'assetProfile': 's3-ota-v1',
        'installPolicy': 'sticky-install',
        'legacyChineseEnvironment': 'sticky_cn_nightly',
    },
    'xteink_x4_pro': {
        'deviceSlug': 'x4pro',
        'models': ['xteink_x4_pro'],
        'boardTag': 'x4pro',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'nightly': 'x4pro_nightly'},
        'assetProfile': 's3-ota-v1',
        'installPolicy': 'preserve-partitions',
        'legacyChineseEnvironment': 'x4pro_cn_nightly',
    },
    'm5stack_paper_mono': {
        'deviceSlug': 'papermono',
        'models': ['m5stack_paper_mono'],
        'boardTag': 'papermono',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'nightly': 'papermono_nightly'},
        'assetProfile': 's3-ota-v1',
        'installPolicy': 'papermono-install',
        'legacyChineseEnvironment': 'papermono_cn_nightly',
    },
    'eego_a4': {
        'deviceSlug': 'eego-a4',
        'models': ['eego_a4'],
        'boardTag': 'eego_a4',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'nightly': 'eego_a4_nightly'},
        'assetProfile': 's3-ota-v1',
        'installPolicy': 'ota-or-install',
        'legacyChineseEnvironment': 'eego_a4_cn_nightly',
    },
    'murphy_m4': {
        'deviceSlug': 'murphy-m4',
        'models': ['murphy_m4'],
        'boardTag': 'murphy_m4',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'nightly': 'murphy_m4_nightly'},
        'assetProfile': 's3-ota-v1',
        'installPolicy': 'ota-or-install',
        'legacyChineseEnvironment': 'murphy_m4_cn_nightly',
    },
    'waveshare_epaper_397': {
        'deviceSlug': 'waveshare-epaper-397',
        'models': ['waveshare_epaper_397'],
        'boardTag': 'waveshare_epaper_397',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'nightly': 'waveshare_epaper_397_nightly'},
        'assetProfile': 's3-ota-v1',
        'installPolicy': 'ota-or-install',
        'legacyChineseEnvironment': 'waveshare_epaper_397_cn_nightly',
    },
    'metalio_eink4': {
        'deviceSlug': 'metalio-eink4',
        'models': ['metalio_eink4'],
        'boardTag': 'metalio_eink4',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'nightly': 'metalio_eink4_nightly'},
        'assetProfile': 's3-ota-v1',
        'installPolicy': 'ota-or-install',
    },
}

FLAVOR_TOKENS = {'global': 'global', 'zh-CN': 'cn'}
CHANNELS = ('stable', 'nightly')


def targets_for(channel):
    if channel not in CHANNELS:
        raise KeyError(channel)
    return {target_id: target for target_id, target in TARGETS.items() if channel in target['environments']}


def environment_for(target_id, channel, flavor):
    if flavor not in FLAVOR_TOKENS:
        raise KeyError(flavor)
    return TARGETS[target_id]['environments'][channel]


def version_for(base_version, target_id, channel, flavor, short_sha):
    target = TARGETS[target_id]
    if channel == 'stable':
        return base_version
    if channel != 'nightly':
        raise KeyError(channel)
    parts = [base_version]
    if suffix := target.get('nightlyVersionSuffix', target['deviceSlug']):
        parts.append(suffix)
    if flavor not in FLAVOR_TOKENS:
        raise KeyError(flavor)
    return f"{'-'.join(parts)}-rc+{short_sha[:7]}"


def asset_name(target_id, source_name):
    target = TARGETS[target_id]
    return f"{target['deviceSlug']}-{source_name}"


def manifest_name(target_id, flavor):
    target = TARGETS[target_id]
    return f"{target['deviceSlug']}-{FLAVOR_TOKENS[flavor]}-manifest.json"


def matrix(channel):
    return {
        'include': [
            {
                'targetId': target_id,
                'deviceSlug': target['deviceSlug'],
                'environment': environment_for(target_id, channel, 'global'),
            }
            for target_id, target in targets_for(channel).items()
        ]
    }


ASSET_PROFILES = {
    'c3-ota-v1': ['bootloader', 'partitions', 'firmware'],
    's3-ota-v1': ['bootloader', 'partitions', 'boot_app0', 'firmware'],
    'app-only-v1': ['firmware'],
}
ASSET_OFFSETS = {'bootloader': 0, 'partitions': 0x8000, 'boot_app0': 0xE000, 'firmware': 0x10000}


def supported_channels(target):
    return [channel for channel in CHANNELS if channel in target['environments']]


def asset_roles(target, channel, profile=None):
    if profile is None:
        profile = target.get('legacyAssetProfiles', {}).get(channel, target['assetProfile'])
    elif profile != target['assetProfile']:
        raise ValueError('unexpected assetProfile')
    return ASSET_PROFILES[profile]


def compatibility_aliases(channel, scope):
    aliases = {}
    for target_id, target in targets_for(channel).items():
        for name, role in target.get('compatibilityAliases', {}).get(scope, {}).items():
            if name in aliases:
                raise ValueError(f'duplicate compatibility alias: {name}')
            aliases[name] = (target_id, role)
    return aliases
