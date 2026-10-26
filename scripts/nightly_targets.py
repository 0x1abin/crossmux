"""Canonical firmware build, public device metadata, and artifact naming."""

from copy import deepcopy
import re


TARGETS = {
    'xteink_x4': {
        'name': 'Xteink X3 / X4',
        'devices': [{'model': 'xteink_x4', 'name': 'Xteink X4', 'resolution': '480 × 800'},
                    {'model': 'xteink_x3', 'name': 'Xteink X3', 'resolution': '528 × 792'}],
        'partitionProfile': 'crossmux-sticky-v1',
        'flash': {'size': 0x1000000, 'mode': 'dio', 'frequency': '80m'},
        'usbFilters': [{'usbVendorId': 0x303A, 'usbProductId': 0x1001}],
        'installStrategy': 'xteink-ota',
        'factoryOptions': [
            {'id': 'stock-en', 'kind': 'xteink-update', 'lang': 'en',
             'deviceTypes': {'xteink_x3': 'ESP32C3_X3', 'xteink_x4': 'ESP32C3'}},
            {'id': 'stock-cn', 'kind': 'xteink-update', 'lang': 'cn',
             'deviceTypes': {'xteink_x3': 'ESP32C3_X3', 'xteink_x4': 'ESP32C3'}},
        ],
        'deviceSlug': 'xteink',
        'models': ['xteink_x3', 'xteink_x4'],
        'boardTag': 'x4',
        'chip': 'ESP32-C3',
        'chipId': 0x0005,
        'environments': {'stable': 'gh_release', 'nightly': 'gh_release_rc'},
        'supportedChannels': ['stable', 'nightly'],
        'fullInstall': True,
    },
    'sticky': {
        'name': 'Seeed Sticky',
        'devices': [{'model': 'sticky', 'name': 'Seeed Sticky', 'resolution': '480 × 800'}],
        'partitionProfile': 'crossmux-sticky-v1',
        'flash': {'size': 0x1000000, 'mode': 'dio', 'frequency': '80m'},
        'usbFilters': [{'usbVendorId': 0x303A}, {'usbVendorId': 0x2886},
                       {'usbVendorId': 0x10C4}, {'usbVendorId': 0x1A86}],
        'installStrategy': 'sticky',
        'deviceSlug': 'sticky',
        'models': ['sticky'],
        'boardTag': 'sticky',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'stable': 'sticky-gh_release', 'nightly': 'sticky_nightly'},
        'supportedChannels': ['stable', 'nightly'],
        'fullInstall': True,
    },
    'xteink_x4_pro': {
        'name': 'Xteink X4 Pro',
        'devices': [{'model': 'xteink_x4_pro', 'name': 'Xteink X4 Pro', 'resolution': '480 × 800'}],
        'partitionProfile': 'crossmux-sticky-v1',
        'flash': {'size': 0x1000000, 'mode': 'dio', 'frequency': '80m'},
        'usbFilters': [{'usbVendorId': 0x303A}, {'usbVendorId': 0x2886},
                       {'usbVendorId': 0x10C4}, {'usbVendorId': 0x1A86}],
        'installStrategy': 'x4pro',
        'deviceSlug': 'x4pro',
        'models': ['xteink_x4_pro'],
        'boardTag': 'x4pro',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'stable': 'x4pro-gh_release', 'nightly': 'x4pro_nightly'},
        'supportedChannels': ['stable', 'nightly'],
        'fullInstall': True,
    },
    'm5stack_paper_mono': {
        'name': 'M5Stack Paper Mono',
        'devices': [{'model': 'm5stack_paper_mono', 'name': 'M5Stack Paper Mono', 'resolution': '480 × 800'}],
        'partitionProfile': 'crossmux-sticky-v1',
        'flash': {'size': 0x1000000, 'mode': 'dio', 'frequency': '80m'},
        'usbFilters': [{'usbVendorId': 0x303A}, {'usbVendorId': 0x2886},
                       {'usbVendorId': 0x10C4}, {'usbVendorId': 0x1A86}],
        'installStrategy': 'papermono',
        'deviceSlug': 'papermono',
        'models': ['m5stack_paper_mono'],
        'boardTag': 'papermono',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'stable': 'papermono-gh_release', 'nightly': 'papermono_nightly'},
        'supportedChannels': ['nightly'],
        'fullInstall': True,
    },
    'eego_a4': {
        'name': 'EEGO A4',
        'devices': [{'model': 'eego_a4', 'name': 'EEGO A4', 'resolution': '552 × 768'}],
        'partitionProfile': 'crossmux-sticky-v1',
        'flash': {'size': 0x1000000, 'mode': 'dio', 'frequency': '80m'},
        'usbFilters': [{'usbVendorId': 0x303A}, {'usbVendorId': 0x2886},
                       {'usbVendorId': 0x10C4}, {'usbVendorId': 0x1A86}],
        'installStrategy': 'standard',
        'deviceSlug': 'eego-a4',
        'models': ['eego_a4'],
        'boardTag': 'eego_a4',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'stable': 'eego_a4-gh_release', 'nightly': 'eego_a4_nightly'},
        'supportedChannels': ['nightly'],
        'fullInstall': True,
    },
    'murphy_m4': {
        'name': 'Murphy M4',
        'devices': [{'model': 'murphy_m4', 'name': 'Murphy M4', 'resolution': '480 × 800'}],
        'partitionProfile': 'crossmux-sticky-v1',
        'flash': {'size': 0x1000000, 'mode': 'dio', 'frequency': '80m'},
        'usbFilters': [{'usbVendorId': 0x303A}, {'usbVendorId': 0x2886},
                       {'usbVendorId': 0x10C4}, {'usbVendorId': 0x1A86}],
        'installStrategy': 'standard',
        'deviceSlug': 'murphy-m4',
        'models': ['murphy_m4'],
        'boardTag': 'murphy_m4',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'stable': 'murphy_m4-gh_release', 'nightly': 'murphy_m4_nightly'},
        'supportedChannels': ['nightly'],
        'fullInstall': True,
    },
    'waveshare_epaper_397': {
        'name': 'Waveshare ePaper 3.97',
        'devices': [{'model': 'waveshare_epaper_397', 'name': 'Waveshare ePaper 3.97', 'resolution': '480 × 800'}],
        'partitionProfile': 'crossmux-sticky-v1',
        'flash': {'size': 0x1000000, 'mode': 'dio', 'frequency': '80m'},
        'usbFilters': [{'usbVendorId': 0x303A}, {'usbVendorId': 0x2886},
                       {'usbVendorId': 0x10C4}, {'usbVendorId': 0x1A86}],
        'installStrategy': 'standard',
        'deviceSlug': 'waveshare-epaper-397',
        'models': ['waveshare_epaper_397'],
        'boardTag': 'waveshare_epaper_397',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'stable': 'waveshare_epaper_397-gh_release', 'nightly': 'waveshare_epaper_397_nightly'},
        'supportedChannels': ['nightly'],
        'fullInstall': True,
    },
    'metalio_eink4': {
        'name': 'Metalio E-Ink 4',
        'devices': [{'model': 'metalio_eink4', 'name': 'Metalio E-Ink 4', 'resolution': '480 × 800'}],
        'partitionProfile': 'crossmux-sticky-v1',
        'flash': {'size': 0x1000000, 'mode': 'dio', 'frequency': '80m'},
        'usbFilters': [{'usbVendorId': 0x303A}, {'usbVendorId': 0x2886},
                       {'usbVendorId': 0x10C4}, {'usbVendorId': 0x1A86}],
        'installStrategy': 'standard',
        'deviceSlug': 'metalio-eink4',
        'models': ['metalio_eink4'],
        'boardTag': 'metalio_eink4',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'stable': 'metalio_eink4-gh_release', 'nightly': 'metalio_eink4_nightly'},
        'supportedChannels': ['stable', 'nightly'],
        'fullInstall': True,
    },
    'readpico': {
        'factoryOptions': [{
            'id': 'factory',
            'kind': 'pinned-image',
            'version': '28cde68',
            'url': 'https://assets.crossmux.cn/firmware/builds/readpico-factory/'
                   '28cde682a4468a581c278761922724f57d976418/readpico-factory.bin',
            'size': 0x1000000,
            'sha256': '3dda5bfa22b8dbd27d411b960e6153c184a9c2fca7d97ad0bc8a5c7ac219a719',
            'sourceRepo': 'https://github.com/MindReset/read_pico_firmware',
            'sourceSha': '28cde682a4468a581c278761922724f57d976418',
            'validation': 'readpico-factory',
        }],
        'name': 'MindReset Read Pico',
        'devices': [{'model': 'readpico', 'name': 'MindReset Read Pico', 'resolution': '684 × 1216'}],
        'partitionProfile': 'crossmux-sticky-v1',
        'flash': {'size': 0x1000000, 'mode': 'dio', 'frequency': '80m'},
        'usbFilters': [{'usbVendorId': 0x303A}, {'usbVendorId': 0x2886},
                       {'usbVendorId': 0x10C4}, {'usbVendorId': 0x1A86}],
        'installStrategy': 'standard',
        'deviceSlug': 'readpico',
        'models': ['readpico', 'read_pico'],
        'boardTag': 'readpico',
        'chip': 'ESP32-S3',
        'chipId': 0x0009,
        'environments': {'stable': 'readpico-gh_release', 'nightly': 'readpico_nightly'},
        'supportedChannels': ['nightly'],
        'fullInstall': True,
    },
}

FLAVOR_TOKENS = {'global': 'global', 'zh-CN': 'cn'}
CHANNELS = ('stable', 'nightly')


def targets_for(channel):
    if channel not in CHANNELS:
        raise KeyError(channel)
    return {target_id: target for target_id, target in TARGETS.items() if channel in target['supportedChannels']}


def environment_for(target_id, channel, flavor):
    if flavor not in FLAVOR_TOKENS:
        raise KeyError(flavor)
    return TARGETS[target_id]['environments'][channel]


def version_for(base_version, target_id, channel, flavor, short_sha, branch='unknown'):
    if flavor not in FLAVOR_TOKENS:
        raise KeyError(flavor)
    if channel in CHANNELS and not re.fullmatch(r'[0-9a-fA-F]{7,}', short_sha):
        raise ValueError('Published firmware requires a valid Git HEAD SHA')
    if channel == 'stable':
        return base_version
    if channel == 'nightly':
        return f'{base_version}-rc+{short_sha[:7]}'
    if channel == 'dev':
        return f'{base_version}-dev-{branch}-{short_sha[:7]}'
    raise KeyError(channel)


def public_target(target_id):
    return {'targetId': target_id, **deepcopy({
        key: value for key, value in TARGETS[target_id].items()
        if key not in ('environments', 'chipId')
    })}


def bundle_name(target_id, version):
    return f"crossmux-{version}-{TARGETS[target_id]['deviceSlug']}.zip"


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
