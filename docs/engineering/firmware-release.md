# Firmware Release Architecture

CrossMux has two release channels, `stable` and `nightly`, managed by one
channel-aware pipeline. Hardware identity is not a release channel. Stable
contains the shared X3/X4 image and a separate Sticky image. Both targets support
`stable` and `nightly`; the other six ESP32-S3 release targets remain Nightly-only.

## Canonical targets

[`scripts/nightly_targets.py`](../../scripts/nightly_targets.py) is the release
source of truth despite its compatibility filename. Each target defines its
runtime models, artifact slug, embedded board tag, per-channel PlatformIO
environments, chip/chip ID, asset profile, install policy, and compatibility aliases.
Supported channels are derived from the environment keys. The workflow,
packager, index builder, and tests import this table rather than copy it.

The X3/X4 target accepts `xteink_x3` and `xteink_x4` and produces one ESP32-C3
image. Stable uses `gh_release`; Nightly uses `gh_release_rc`. Sticky Stable uses
`sticky-gh_release`. Sticky, X4 Pro,
Paper Mono, EEGO A4, Murphy M4, Waveshare ePaper 3.97, and Metalio E-Ink 4 each produce their own
ESP32-S3 Nightly image. Each image is aliased by the compatibility `global` and
`zh-CN` pointers.

## Publishing

Each target job builds once and packages one binary set plus two compatibility
manifests. Packaging checks the ESP image chip ID, required board tag, partition
layout, app-slot size, and SHA-256 before emitting the manifests.

The global and China publish jobs run independently. Each writes in this order:

1. immutable binaries and checksum files;
2. immutable target manifests;
3. verification of every published manifest and binary against a local candidate index;
4. rolling regional indexes.

Every target selected for a channel must build successfully before either region
publishes. Nightly also requires its previous rolling index because that index
protects the immediately preceding build during cleanup. Once both regions
publish, CI resolves every manifest and verifies each distinct asset's size and
SHA-256. Cleanup then runs for Nightly only; Stable builds are retained.

The global index is the `release-index.json` asset of the rolling `stable` or
`nightly` GitHub Release. Each channel also keeps the X3/X4 `firmware.bin` alias
and links to the actual release. New Stable assets live directly in the version
Release (`1.6.x`), titled `CrossMux <version>`; no duplicate GitHub
`stable-build-*` release is created. Nightly assets live in
`nightly-build-<sha>-<run>-<attempt>`, titled with the UTC date and short SHA.
Historical build releases and download URLs remain supported and untouched. China indexes
are `/firmware/releases/<channel>/index.json`; target assets live under
`/firmware/builds/<channel>-build-<sha>-<run>-<attempt>/<target>/` in COS. Region chooses the storage
provider; both variant manifests reference the same neutral binary names and
differing hashes fail publication. The Stable version Release includes legacy
`firmware.bin`, `firmware-cn.bin`, `bootloader.bin`, and `partitions.bin` aliases
alongside the neutral binaries, compatibility manifests, and checksum files.
It is published with `--latest=false` for public verification and becomes Latest
only after both regions pass. Channels and Nightly archives are prereleases.

GitHub and COS uploads reuse identical existing bytes and add only missing files.
A conflicting immutable file aborts publication; only rolling channel assets may
be replaced. A partial upload can be retried without overwriting existing firmware.
Workflow reruns reuse the first saved previous-index snapshot for that run;
notes, packages, and snapshots are kept as Actions artifacts for 14 days.
Artifact replacement on retry does not change immutable release assets.
Before enabling version-tag Stable publication, deploy the website path validator
that accepts exact Stable version tags while retaining legacy build paths.

For new Stable tags, `.github/release-notes/<version>.md` is the sole notes source.
Include one `<!-- OTA_NOTES {"en":[...],"zh":[...]} -->` marker using the existing
2–8 paired, validated OTA summaries. Both the Release body and regional index use
this file; the annotated Git tag no longer needs its own marker. Nightly retains
the two workflow inputs. Missing or invalid summaries stop the release.

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

## Restore a missing GitHub channel

Run **Restore Firmware Channel** and choose `stable` or `nightly`. The workflow
shares `firmware-<channel>-publish` concurrency with normal publishing. Locally,
with no publisher running, use `python3 scripts/restore_firmware_channel.py --channel stable`
(or `nightly`) from a full checkout with authenticated `gh` write access.

Recovery reads the current COS index, preserves its version, revisions, timestamps,
and notes, and converts only manifest URLs to the existing GitHub archive or
Stable version Release. It verifies both regions using the scripts from the
original build commit, so historical target capabilities remain valid. It then
restores the channel index and X3/X4 compatibility alias. It never builds, cleans,
rolls back, or overwrites immutable assets. A healthy existing channel is a no-op;
regional disagreement, missing resources, conflicting aliases, or concurrent changes
stop recovery. A draft channel requires inspection before retrying.

Missing previous Nightly indexes still stop normal publication; do not bypass
this check or cleanup's rollback protection. GitHub can be restored using the
workflow above; a missing COS index requires recovery of that regional index first.
No historical releases are migrated as part of this change.


## Unified target contract and Stable promotion

`nightly_targets.py` is the sole technical target definition. Export with
`python3 scripts/export_firmware_targets.py`; CrossMux Web checks in the resulting
`shared/firmware-targets.generated.json`. In the web checkout, run
`node scripts/sync-firmware-targets.mjs <firmware-checkout>` to refresh it, or append
`--check` to check for drift. The snapshot records the source commit and the SHA-256
of the actual target source (including uncommitted edits). Refresh from the final
firmware commit before the production web deployment. Web builds never fetch code
or target configuration from the network.

New schema-v1 manifests include `assetProfile`. C3 Stable and Nightly both use
`c3-ota-v1` (bootloader, partitions, firmware); S3 uses `s3-ota-v1` (also boot_app0).
The C3 installer generates erased otadata as before; it does not borrow boot files
from Stable for a Nightly installation. Manifests without a profile retain their
historical contract, including app-only C3 Nightly. App-only packages cannot perform
an initial install or partition migration. Explicit profiles must match the local
registered target and contain exactly the declared resources.

Web install manifests now cover every registered target. The same target table
selects chip checks and installation policy; USB, backup, partition migration,
OTA commit/readback and reset behavior remain hardware-specific. X3 and X4 are
separate presentation choices for the same firmware target. All known models remain
visible when catalogs fail; channel failures are shown with a retry action, separately
from an unpublished channel. Stable is preferred only when its manifest validates.

An immutable manifest's supported channel declaration may be a subset of the current
registered channels, must include its own channel and contain no duplicates, and must
match its index. This lets any board enter Stable without invalidating old Nightly
builds. Publication still requires the exact target/channel set from its build commit.

To promote a platform: record real-device Nightly acceptance; add the Stable
PlatformIO environment to the target; export and deploy the web contract; then include
it in the next version-tagged Stable build. All Stable targets must use one release
version and source revision, and every target must succeed. No per-board Stable
Release or fallback to an older board build is created.

Release identity is checked across all target pointers. Legacy unprefixed downloads
are generated by the declared compatibility alias map, verified before publishing,
and restored by the same map. Missing alias sources fail publication; they never
fall back to another target.

Roll out the compatible web reader before enabling new profile-bearing packages.
Verify a complete Nightly before applying the pipeline to the next Stable release.
Do not mutate old manifests, overwrite immutable binaries, or remove old releases.
Automated checks do not certify physical X3/X4 migration or S3 reset behavior; record
those checks separately before calling the rollout fully accepted.
