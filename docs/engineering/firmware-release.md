# Firmware Release Architecture

CrossMux has two release channels, `stable` and `nightly`, managed by one
channel-aware pipeline. Hardware identity is not a release channel. Stable
currently contains the shared X3/X4 image; X3/X4 and the six ESP32-S3 targets
share Nightly, with S3 targets declaring only `nightly` in `supportedChannels`.

## Canonical targets

[`scripts/nightly_targets.py`](../../scripts/nightly_targets.py) is the release
source of truth despite its compatibility filename. Each target defines its
runtime models, artifact slug, embedded board tag, per-channel PlatformIO
environments, chip, install capability, and supported channels. The workflow,
packager, index builder, and tests import this table rather than copy it.

The X3/X4 target accepts `xteink_x3` and `xteink_x4` and produces one ESP32-C3
image. Stable uses `gh_release`; Nightly uses `gh_release_rc`. Sticky, X4 Pro,
Paper Mono, EEGO A4, Murphy M4, and Waveshare ePaper 3.97 each produce their own
ESP32-S3 Nightly image. Each image is aliased by the compatibility `global` and
`zh-CN` pointers.

## Publishing

Each target job builds once and packages one binary set plus two compatibility
manifests. Packaging checks the ESP image chip ID, required board tag, partition
layout, app-slot size, and SHA-256 before emitting the manifests.

The global and China publish jobs run independently. Each writes in this order:

1. immutable binaries and checksum files;
2. immutable target manifests;
3. rolling regional indexes.

Every target selected for a channel must build successfully before either region
publishes. Nightly also requires its previous rolling index because that index
protects the immediately preceding build during cleanup. Once both regions
publish, CI resolves every manifest and verifies each distinct asset's size and
SHA-256. Cleanup then runs for Nightly only; Stable builds are retained.

The global index is the `release-index.json` asset of the rolling `stable` or
`nightly` GitHub Release. Binaries and compatibility manifests live in an
immutable `<channel>-build-<sha>-<run>-<attempt>` GitHub Release. China indexes
are `/firmware/releases/<channel>/index.json`; target assets live under
`/firmware/builds/<channel>-build-<sha>-<run>-<attempt>/<target>/` in COS. Region chooses the storage
provider; both variant manifests reference the same neutral binary names and
differing hashes fail publication. After Stable verification, the version tag
also receives the legacy `firmware.bin`, `firmware-cn.bin`, `bootloader.bin`,
and `partitions.bin` assets.

At steady state, GitHub and COS retain the current build and the build or builds
referenced by the previous index. A scheduled successful Nightly therefore
keeps roughly 24 hours of rollback data. Failed builds do not publish or clean
up anything. The first complete run can temporarily retain more than two build
names when the preceding index contains target-level fallbacks; the next
complete run converges to exactly the current and previous build.

COS publishing runs only on the H2O self-hosted runner and does not fall back to
a GitHub-hosted runner. It uses a version-pinned, SHA-256-verified COSCLI binary
from the runner's temporary directory. The workflow verifies COSCLI before
writing any immutable COS object and passes credentials directly to each COS
command instead of persisting a CLI config file. Required repository secrets
are `COS_SECRET_ID`, `COS_SECRET_KEY`, `COS_BUCKET`, and `COS_REGION`;
`COS_SESSION_TOKEN` is optional. Gitee is not a firmware release destination.
The COS identity also needs `cos:GetBucket` for the current-object listing,
`cos:GetBucketVersioning` to detect the bucket state,
`cos:GetBucketObjectVersions` for versioned cleanup, `cos:DeleteObject`, and
`cos:DeleteMultipleObjects`. Cleanup detects versioned buckets and removes every
version of an obsolete build prefix rather than leaving hidden historical objects.

## Index contract and failure behavior

The schema-v1 index contains `channel`, `updatedAt`, `buildId`, and a `targets`
map, plus optional regional `releaseNotes`. Each target repeats its identity and
channel capabilities and contains `global` and `zh-CN` pointers with version,
CrossMux SHA, SDK SHA, publish time, and immutable manifest URL. Stable requires
both release-note locales.

Every target advances together only when both compatibility manifests are valid
and have the same CrossMux revision, SDK revision, version, and assets. A
missing or malformed manifest prevents the whole channel from publishing.
Build objects are never overwritten; cleanup runs only after the new rolling
indexes and their assets pass verification.

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
target, one X3/X4, both content profiles, and one wrong-board negative case.
