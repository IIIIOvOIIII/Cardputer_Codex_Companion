#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
export IDF_TOOLS_PATH="$repo_root/.tools/espressif"

. "$repo_root/.tools/esp-idf/export.sh" >/dev/null
exec idf.py "$@"
