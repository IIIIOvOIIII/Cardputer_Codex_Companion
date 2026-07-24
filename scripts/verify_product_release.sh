#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

PYTHONPATH=. uv run pytest -q

cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host -j
ctest --test-dir build/product-host --output-on-failure

cmake -S firmware/test/host -B build/product-host-sanitize \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build/product-host-sanitize -j
ctest --test-dir build/product-host-sanitize --output-on-failure

python3 scripts/build_web_assets.py --check

rm -f firmware/sdkconfig firmware/sdkconfig.old
(
  cd firmware
  ../scripts/phase0/idf.sh set-target esp32s3
  ../scripts/phase0/idf.sh build
)
python3 tools/product/verify_partition_layout.py
idf_python="$(
  find "${repo_root}/.tools/espressif/python_env" \
    -path '*/bin/python' -print | sort | tail -n 1
)"
test -x "${idf_python}"
"${idf_python}" -m esp_idf_size --format json \
  firmware/build/cardputer_codex_companion.map \
  > build/product-firmware-size.json
python3 tools/product/verify_firmware_memory.py \
  build/product-firmware-size.json

swift build --package-path companion -c release
companion/.build/release/cardputer-companion --version
companion/.build/release/cardputer-companion doctor

scripts/package_product_firmware.sh
scripts/package_private_firmware.sh
scripts/build_companion.sh

test "$(stat -f %z build/private/wifi_cfg.bin)" -eq 24576
test -f dist/cardputer_codex_companion-full.bin
test -f dist/private/cardputer_codex_companion-private-full.bin
test -x dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion

if git ls-files | grep -E '^(build|dist)/|wifi_cfg\.bin$' >/dev/null; then
  echo "private or generated artifacts are tracked" >&2
  exit 1
fi
if git grep -n -I -E 'PHASE 0 / NOT FOR RELEASE|NSPasteboard|Command-V' \
  -- firmware/main companion/Sources >/dev/null; then
  echo "forbidden product implementation marker found" >&2
  exit 1
fi

git diff --check
shasum -a 256 \
  firmware/build/cardputer_codex_companion.bin \
  dist/cardputer_codex_companion-full.bin \
  build/private/wifi_cfg.bin \
  dist/private/cardputer_codex_companion-private-full.bin \
  dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion
