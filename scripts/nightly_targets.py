"""Canonical Nightly target mapping and artifact naming."""


TARGETS = {
    'xteink_x4': {
        'deviceSlug': 'xteink',
        'models': ['xteink_x3', 'xteink_x4'],
        'boardTag': 'x4',
        'chip': 'ESP32-C3',
        'chipId': 0x0005,
        'environments': {'global': 'gh_release_rc', 'zh-CN': 'gh_release_cn_rc'},
        'supportedChannels': ['stable', 'nightly'],
        'fullInstall': False,
    },
    'sticky': {
        'deviceSlug': 'sticky',
        'models': ['sticky'],
        'boardTag': 'sticky',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'global': 'sticky_nightly', 'zh-CN': 'sticky_cn_nightly'},
        'supportedChannels': ['nightly'],
        'fullInstall': True,
    },
    'xteink_x4_pro': {
        'deviceSlug': 'x4pro',
        'models': ['xteink_x4_pro'],
        'boardTag': 'x4pro',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'global': 'x4pro_nightly', 'zh-CN': 'x4pro_cn_nightly'},
        'supportedChannels': ['nightly'],
        'fullInstall': True,
    },
    'm5stack_paper_mono': {
        'deviceSlug': 'papermono',
        'models': ['m5stack_paper_mono'],
        'boardTag': 'papermono',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'global': 'papermono_nightly', 'zh-CN': 'papermono_cn_nightly'},
        'supportedChannels': ['nightly'],
        'fullInstall': True,
    },
    'eego_a4': {
        'deviceSlug': 'eego-a4',
        'models': ['eego_a4'],
        'boardTag': 'eego_a4',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'global': 'eego_a4_nightly', 'zh-CN': 'eego_a4_cn_nightly'},
        'supportedChannels': ['nightly'],
        'fullInstall': True,
    },
    'murphy_m4': {
        'deviceSlug': 'murphy-m4',
        'models': ['murphy_m4'],
        'boardTag': 'murphy_m4',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'global': 'murphy_m4_nightly', 'zh-CN': 'murphy_m4_cn_nightly'},
        'supportedChannels': ['nightly'],
        'fullInstall': True,
    },
    'waveshare_epaper_397': {
        'deviceSlug': 'waveshare-epaper-397',
        'models': ['waveshare_epaper_397'],
        'boardTag': 'waveshare_epaper_397',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {
            'global': 'waveshare_epaper_397_nightly',
            'zh-CN': 'waveshare_epaper_397_cn_nightly',
        },
        'supportedChannels': ['nightly'],
        'fullInstall': True,
    },
}

FLAVOR_TOKENS = {'global': 'global', 'zh-CN': 'cn'}


def environment_for(target_id, flavor):
    return TARGETS[target_id]['environments'][flavor]


def version_for(base_version, target_id, flavor, short_sha):
    target = TARGETS[target_id]
    parts = [base_version]
    if target_id != 'xteink_x4':
        parts.append(target['deviceSlug'])
    if flavor == 'zh-CN':
        parts.append('cn')
    elif flavor != 'global':
        raise KeyError(flavor)
    return f"{'-'.join(parts)}-rc+{short_sha[:7]}"


def asset_name(target_id, flavor, source_name):
    target = TARGETS[target_id]
    return f"{target['deviceSlug']}-{FLAVOR_TOKENS[flavor]}-{source_name}"


def manifest_name(target_id, flavor):
    target = TARGETS[target_id]
    return f"{target['deviceSlug']}-{FLAVOR_TOKENS[flavor]}-manifest.json"


def matrix():
    return {
        'include': [
            {
                'targetId': target_id,
                'deviceSlug': target['deviceSlug'],
                'globalEnv': target['environments']['global'],
                'cnEnv': target['environments']['zh-CN'],
            }
            for target_id, target in TARGETS.items()
        ]
    }
