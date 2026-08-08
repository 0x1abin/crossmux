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
  gitee_git="https://gitee.com/${REPO}.git"
  gitee_user="${REPO%%/*}"
  auth_header="$(printf '%s:%s' "$gitee_user" "$GITEE_TOKEN" | base64 | tr -d '\n')"
  git fetch origin "$COMMITISH"
  git -c http.extraHeader="Authorization: Basic ${auth_header}" \
    push --force "$gitee_git" "${COMMITISH}:refs/tags/${TAG}"
  unset auth_header
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

# Gitee requires target_commitish even when the verified tag already exists.
echo "Creating Gitee release for tag ${TAG}"
create_args=(-sS -X POST "${API}/releases"
  --data-urlencode "access_token=${GITEE_TOKEN}"
  --data-urlencode "tag_name=${TAG}"
  --data-urlencode "name=${NAME}"
  --data-urlencode "body@${BODY_FILE}"
  --data-urlencode "prerelease=${PRERELEASE}"
  --data-urlencode "target_commitish=${COMMITISH}")
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
  asset_name="$(basename "$f")"
  echo "Uploading ${asset_name} ($(du -h "$f" | cut -f1))"
  uploaded=false
  for attempt in 1 2 3; do
    up="$(curl -sS --connect-timeout 20 --max-time 600 \
      --speed-limit 1024 --speed-time 120 \
      -X POST "${API}/releases/${release_id}/attach_files" \
      -F "access_token=${GITEE_TOKEN}" \
      -F "file=@${f}")"
    if echo "$up" | jq -e '.browser_download_url' >/dev/null 2>&1; then
      echo "  ok: $(echo "$up" | jq -r '.browser_download_url')"
      uploaded=true
      break
    fi

    # A timed-out response may still leave a completed upload on Gitee. Check
    # before retrying so the release never gains duplicate attachments.
    uploaded_url="$(curl -fsS --connect-timeout 20 --max-time 60 \
      "${API}/releases/tags/${TAG}?access_token=${GITEE_TOKEN}" 2>/dev/null \
      | jq -r --arg name "$asset_name" \
        '.assets[]? | select(.name == $name) | .browser_download_url' \
      | head -n 1 || true)"
    if [ -n "$uploaded_url" ]; then
      echo "  ok after response timeout: ${uploaded_url}"
      uploaded=true
      break
    fi

    echo "  upload attempt ${attempt}/3 failed: ${up}"
    if [ "$attempt" -lt 3 ]; then
      sleep 10
    fi
  done
  if [ "$uploaded" != true ]; then
    echo "::error::upload failed for ${f} after 3 attempts"
    rc=1
  fi
  sleep 2
done

exit $rc
