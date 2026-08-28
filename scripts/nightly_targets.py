"""Canonical Nightly target mapping and artifact naming."""


TARGETS = {
    'xteink_x4': {
        'deviceSlug': 'xteink',
        'models': ['xteink_x3', 'xteink_x4'],
        'boardTag': 'x4',
        'chip': 'ESP32-C3',
        'chipId': 0x0005,
        'environment': 'gh_release_rc',
        'supportedChannels': ['stable', 'nightly'],
        'fullInstall': False,
    },
    'sticky': {
        'deviceSlug': 'sticky',
        'models': ['sticky'],
        'boardTag': 'sticky',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environment': 'sticky_nightly',
        'supportedChannels': ['nightly'],
        'fullInstall': True,
    },
    'xteink_x4_pro': {
        'deviceSlug': 'x4pro',
        'models': ['xteink_x4_pro'],
        'boardTag': 'x4pro',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environment': 'x4pro_nightly',
        'supportedChannels': ['nightly'],
        'fullInstall': True,
    },
    'm5stack_paper_mono': {
        'deviceSlug': 'papermono',
        'models': ['m5stack_paper_mono'],
        'boardTag': 'papermono',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environment': 'papermono_nightly',
        'supportedChannels': ['nightly'],
        'fullInstall': True,
    },
    'eego_a4': {
        'deviceSlug': 'eego-a4',
        'models': ['eego_a4'],
        'boardTag': 'eego_a4',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environment': 'eego_a4_nightly',
        'supportedChannels': ['nightly'],
        'fullInstall': True,
    },
    'murphy_m4': {
        'deviceSlug': 'murphy-m4',
        'models': ['murphy_m4'],
        'boardTag': 'murphy_m4',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environment': 'murphy_m4_nightly',
        'supportedChannels': ['nightly'],
        'fullInstall': True,
    },
    'waveshare_epaper_397': {
        'deviceSlug': 'waveshare-epaper-397',
        'models': ['waveshare_epaper_397'],
        'boardTag': 'waveshare_epaper_397',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environment': 'waveshare_epaper_397_nightly',
        'supportedChannels': ['nightly'],
        'fullInstall': True,
    },
}

FLAVOR_TOKENS = {'global': 'global', 'zh-CN': 'cn'}


def environment_for(target_id, flavor):
    if flavor not in FLAVOR_TOKENS:
        raise KeyError(flavor)
    return TARGETS[target_id]['environment']


def version_for(base_version, target_id, flavor, short_sha):
    target = TARGETS[target_id]
    parts = [base_version]
    if target_id != 'xteink_x4':
        parts.append(target['deviceSlug'])
    if flavor not in FLAVOR_TOKENS:
        raise KeyError(flavor)
    return f"{'-'.join(parts)}-rc+{short_sha[:7]}"


def asset_name(target_id, source_name):
    target = TARGETS[target_id]
    return f"{target['deviceSlug']}-{source_name}"


def manifest_name(target_id, flavor):
    target = TARGETS[target_id]
    return f"{target['deviceSlug']}-{FLAVOR_TOKENS[flavor]}-manifest.json"


def matrix():
    return {
        'include': [
            {
                'targetId': target_id,
                'deviceSlug': target['deviceSlug'],
                'environment': target['environment'],
            }
            for target_id, target in TARGETS.items()
        ]
    }
