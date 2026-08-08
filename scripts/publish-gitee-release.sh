#!/usr/bin/env bash
#
# Mirror a just-published GitHub release to Gitee so mainland-China users (and
# CN device OTA) download firmware from Gitee's CN-resident foruda.gitee.com CDN
# instead of the slow/unstable GitHub release CDN. The crossmux-web app's EdgeOne
# (China) deployment reads release metadata + binaries from this Gitee repo when
# FIRMWARE_MIRROR=gitee (see crosspoint-web server/handlers.ts).
#
# This publishes the SAME binaries GitHub Actions just built — it does NOT
# rebuild on Gitee — so the firmware is byte-identical across both hosts.
#
# The stable-release workflow treats mirror failures as errors; nightly remains
# best-effort at the workflow level.
#
# Usage:
#   GITEE_TOKEN=... scripts/publish-gitee-release.sh \
#     <gitee_repo> <tag> <name> <prerelease:true|false> <target_commitish> \
#     <body_file> <asset1> [asset2 ...]
#
# Notes / first-run verification:
#   - GITEE_TOKEN needs the `projects` scope (Gitee → 设置 → 私人令牌).
#   - For the rolling `nightly` tag we delete the existing Gitee release and
#     recreate it; the freshly-uploaded assets are what the web app/device read,
#     so a temporarily-stale `nightly` git tag on Gitee (until the repo mirror
#     re-syncs it) does not affect firmware downloads.
#   - Gitee single-attachment cap is 100 MB and per-repo total is 1 GB; firmware
#     is ~6 MB/asset, so prune old releases if you ever approach the quota.

set -uo pipefail

if [ "$#" -lt 7 ]; then
  echo "::error::usage: publish-gitee-release.sh <repo> <tag> <name> <prerelease> <commitish> <body_file> <asset...>"
  exit 2
fi

REPO="$1"; TAG="$2"; NAME="$3"; PRERELEASE="$4"; COMMITISH="$5"; BODY_FILE="$6"; shift 6
ASSETS=("$@")
API="https://gitee.com/api/v5/repos/${REPO}"

if [ -z "${GITEE_TOKEN:-}" ]; then
  echo "::error::GITEE_TOKEN not set"
  exit 1
fi

if [ "$TAG" != "nightly" ]; then
  echo "Triggering Gitee pull mirror for ${TAG}"
  if ! curl -fsS -X POST "${API}/remote_mirror/pull?access_token=${GITEE_TOKEN}" -o /dev/null; then
    echo "::warning::Gitee pull mirror trigger failed; waiting in case a sync is already running"
  fi
  gitee_git="https://gitee.com/${REPO}.git"
  for attempt in $(seq 1 30); do
    synced_commit="$(git ls-remote "$gitee_git" "refs/tags/${TAG}^{}" 2>/dev/null | cut -f1)"
    if [ -z "$synced_commit" ]; then
      synced_commit="$(git ls-remote "$gitee_git" "refs/tags/${TAG}" 2>/dev/null | cut -f1)"
    fi
    if [ "$synced_commit" = "$COMMITISH" ]; then
      echo "Gitee tag ${TAG} synced to ${COMMITISH}"
      break
    fi
    if [ "$attempt" -eq 30 ]; then
      echo "::error::Gitee tag ${TAG} did not sync to ${COMMITISH}"
      exit 1
    fi
    echo "Waiting for Gitee tag ${TAG} (${attempt}/30)"
    sleep 10
  done
fi

# Roll a pre-existing release for this tag (e.g. nightly) forward: delete it so
# we can recreate cleanly with the new commit's assets.
existing_id="$(curl -fsS "${API}/releases/tags/${TAG}?access_token=${GITEE_TOKEN}" 2>/dev/null \
  | jq -r '.id // empty' 2>/dev/null || true)"
if [ -n "$existing_id" ]; then
  echo "Deleting existing Gitee release ${existing_id} for tag ${TAG}"
  curl -sS -X DELETE "${API}/releases/${existing_id}?access_token=${GITEE_TOKEN}" \
    -o /dev/null -w "  delete: HTTP %{http_code}\n" || true
fi

# Stable releases reuse the mirror-synced tag. Nightly keeps its rolling-tag
# behavior by supplying target_commitish.
echo "Creating Gitee release for tag ${TAG}"
create_args=(-sS -X POST "${API}/releases"
  --data-urlencode "access_token=${GITEE_TOKEN}"
  --data-urlencode "tag_name=${TAG}"
  --data-urlencode "name=${NAME}"
  --data-urlencode "body@${BODY_FILE}"
  --data-urlencode "prerelease=${PRERELEASE}")
if [ "$TAG" = "nightly" ]; then
  create_args+=(--data-urlencode "target_commitish=${COMMITISH}")
fi
create_resp="$(curl "${create_args[@]}")"

release_id="$(echo "$create_resp" | jq -r '.id // empty' 2>/dev/null || true)"
if [ -z "$release_id" ]; then
  echo "::error::Gitee release create failed: ${create_resp}"
  exit 1
fi
echo "Created Gitee release id=${release_id}"

rc=0
for f in "${ASSETS[@]}"; do
  if [ ! -f "$f" ]; then
    echo "::error::asset not found: ${f}"
    rc=1
    continue
  fi
  echo "Uploading $(basename "$f") ($(du -h "$f" | cut -f1))"
  up="$(curl -sS -X POST "${API}/releases/${release_id}/attach_files" \
    -F "access_token=${GITEE_TOKEN}" \
    -F "file=@${f}")"
  if echo "$up" | jq -e '.browser_download_url' >/dev/null 2>&1; then
    echo "  ok: $(echo "$up" | jq -r '.browser_download_url')"
  else
    echo "::error::upload failed for ${f}: ${up}"
    rc=1
  fi
done

exit $rc
