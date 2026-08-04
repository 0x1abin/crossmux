#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PUBLISH="${SCRIPT_DIR}/publish-gitee-release.sh"
MOCK_LOG="$(mktemp)"
MOCK_ASSET="$(mktemp)"
export MOCK_LOG
trap 'rm -f "$MOCK_LOG" "$MOCK_ASSET"' EXIT

git() { printf '%s\trefs/tags/1.5.4^{}\n' "$EXPECTED_COMMIT"; }
sleep() { :; }
curl() {
  printf '%s\n' "$*" >>"$MOCK_LOG"
  case "$*" in
    *'/releases/tags/'*) printf '{}\n' ;;
    *'/attach_files'*) printf '{"browser_download_url":"mock"}\n' ;;
    *) printf '{"id":1}\n' ;;
  esac
}
export -f git sleep curl

set +e
env -u GITEE_TOKEN bash "$PUBLISH" repo stable name false commit "$MOCK_ASSET" "$MOCK_ASSET"
missing_token_rc=$?
set -e
if [ "$missing_token_rc" -ne 1 ]; then
  echo "missing GITEE_TOKEN returned ${missing_token_rc}, expected 1" >&2
  exit 1
fi

export GITEE_TOKEN=test EXPECTED_COMMIT=commit
bash "$PUBLISH" repo 1.5.4 name false "$EXPECTED_COMMIT" "$MOCK_ASSET" "$MOCK_ASSET"
! grep -q target_commitish "$MOCK_LOG"

: >"$MOCK_LOG"
bash "$PUBLISH" repo nightly name false "$EXPECTED_COMMIT" "$MOCK_ASSET" "$MOCK_ASSET"
grep -q 'target_commitish=commit' "$MOCK_LOG"
