#!/usr/bin/env bash
set -euo pipefail
umask 077

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
idf_python="$(
  find "${repo_root}/.tools/espressif/python_env" \
    -path '*/bin/python' -print | sort | tail -n 1
)"
output="${repo_root}/dist/cardputer_codex_companion-full.bin"

test -x "${idf_python}"
python3 "${repo_root}/tools/product/merge_product_image.py" \
  --build-dir "${repo_root}/firmware/build" \
  --output "${output}" \
  --idf-python "${idf_python}"
printf 'Generic full image: %s\n' "${output}"
