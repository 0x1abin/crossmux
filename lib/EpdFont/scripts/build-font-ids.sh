#!/bin/bash
set -euo pipefail
exec "${PYTHON:-python3}" "$(dirname "$0")/build-font-ids.py" "$@"
