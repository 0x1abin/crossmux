# Firmware Release Architecture

CrossMux has two release channels: `stable` and `nightly`. Hardware identity is
not a release channel. X3/X4 and the six ESP32-S3 targets share the Nightly
pipeline; S3 targets currently declare only `nightly` in `supportedChannels`.

## Canonical targets

[`scripts/nightly_targets.py`](../../scripts/nightly_targets.py) is the release
source of truth. Each target defines its runtime models, artifact slug, embedded
board tag, PlatformIO environments, chip, install capability, and supported
channels. The workflow, packager, index builder, and tests must import this table
rather than copy it.

The X3/X4 target accepts `xteink_x3` and `xteink_x4` and produces one ESP32-C3
image. Sticky, X4 Pro, Paper Mono, EEGO A4, Murphy M4, and Waveshare ePaper 3.97
each produce their own ESP32-S3 image. Every target has global and `zh-CN`
variants, for 14 Nightly environments in total.

## Publishing

Each target job builds and verifies both variants. Packaging checks the ESP
image chip ID, required board tag, partition layout, app-slot size, and SHA-256
before emitting an immutable target manifest.

The publish job writes in this order:

1. immutable binaries and checksum files;
2. immutable target manifests;
3. rolling regional indexes.

The global index is the `release-index.json` asset of the rolling `nightly`
GitHub Release. Both variants and their manifests live in an immutable
`nightly-build-<sha>-<run>-<attempt>` GitHub Release. The China index is
`/firmware/releases/nightly/index.json`; both variants live under
`/firmware/builds/<build-id>/<target>/<variant>/` in COS and are served through
`assets.crossmux.cn`. Region chooses the storage provider; firmware language
does not.

COS publishing uses a version-pinned, SHA-256-verified COSCLI binary. The
workflow verifies release tooling before writing any immutable release. Required
repository secrets are `COS_SECRET_ID`, `COS_SECRET_KEY`, `COS_BUCKET`, and
`COS_REGION`; `COS_SESSION_TOKEN` is optional. Gitee is not a firmware release
destination.

## Index contract and failure behavior

The schema-v1 index contains `channel`, `updatedAt`, `buildId`, and a `targets`
map. Each target repeats its identity and channel capabilities and contains
global and `zh-CN` pointers with version, CrossMux SHA, SDK SHA, publish time,
and immutable manifest URL.

A target advances only when both variants are valid and have the same CrossMux
and SDK revisions. A missing variant retains that target's previous pointer;
other targets may still advance. A malformed complete pair fails publishing.
Unknown targets from an older index are discarded. Historical objects are
never overwritten, so rollback changes only the rolling index pointer.

## Consumers and safety

The Web flasher reads the regional catalog and then the selected target's
install manifests. Device OTA keeps the GitHub-like response with one
`firmware.bin` asset and selects by exact model, variant, and channel. A target
that does not support Stable returns `ota_status: unsupported_channel` rather
than falling back to Nightly or another board.

Official packages must contain the board tag. The OTA stream aborts a tagged
image for another board before selecting the new partition. Untagged historical
or third-party images remain compatible, so chip and board checks in the
official packaging path are mandatory.

CI, indexes, and checksum checks do not replace real-device acceptance. Before
making a Nightly pipeline production-critical, test OTA and reboot on every S3
target, one X3/X4, both language variants, and one wrong-board negative case.
