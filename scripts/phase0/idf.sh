#!/bin/sh
set -eu

REPO_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
IDF_PATH="$REPO_ROOT/.tools/esp-idf"
IDF_TOOLS_PATH="$REPO_ROOT/.tools/espressif"
EXPECTED_COMMIT=735507283d5b2f9fb363a1901172dbd9e847945d

ACTUAL_COMMIT=$(git -C "$IDF_PATH" rev-parse HEAD)
if [ "$ACTUAL_COMMIT" != "$EXPECTED_COMMIT" ]; then
  echo "ESP-IDF commit mismatch" >&2
  exit 2
fi

export IDF_PATH IDF_TOOLS_PATH
. "$IDF_PATH/export.sh" >/dev/null
exec "$IDF_PATH/tools/idf.py" "$@"
