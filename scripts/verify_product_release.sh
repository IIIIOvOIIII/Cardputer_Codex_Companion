#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

scripts/build_audio_driver.sh
PYTHONPATH=. uv run pytest -q
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_audio_vectors.py \
  tools/product/tests/test_audio_driver_bundle.py \
  tools/product/tests/test_audio_driver_installer.py \
  tools/product/tests/test_launch_agent_installer.py \
  tools/product/tests/test_mac_installer.py \
  tools/product/tests/test_audio_release.py

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
swift run --package-path companion -c release product-audio-tests
swift run --package-path companion -c release product-gatt-tests
swift run --package-path companion -c release product-configuration-tests
scripts/test_audio_ring.sh
scripts/build_audio_driver.sh --test
companion/.build/release/cardputer-companion --version
companion/.build/release/cardputer-companion doctor

scripts/package_product_firmware.sh
scripts/build_companion.sh
scripts/package_mac_installer.sh
scripts/package_windows_agent.sh

test -f dist/cardputer_codex_companion-full.bin
test -x dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion
test -x dist/CardputerCompanion-mac-installer/install.sh
test -f \
  dist/CardputerCompanion-mac-installer/installer/mac_installer.py
test -f \
  dist/CardputerCompanion-mac-installer/installer/install_companion_launch_agent.py
test -x \
  dist/CardputerCompanion-mac-installer/CardputerCompanion.app/Contents/MacOS/cardputer-companion
test -x \
  dist/CardputerCompanion.app/Contents/Resources/install_audio_driver.sh
test -f \
  dist/CardputerCompanion.app/Contents/Resources/CardputerCodexMicrophone.driver/Contents/Info.plist
test -x \
  dist/CardputerCompanion.app/Contents/Resources/CardputerAudioBridge
test -f \
  dist/CardputerCompanion.app/Contents/Resources/com.lynx.cardputer-audio-bridge.plist
test -f dist/CardputerCompanion-1.2.0-windows-amd64.zip
test -f dist/CardputerCompanion-1.2.0-windows-arm64.zip
test -f dist/CardputerCompanion-1.2.0-windows-x64-setup.exe
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_audio_driver_bundle.py \
  tools/product/tests/test_audio_driver_installer.py \
  tools/product/tests/test_launch_agent_installer.py \
  tools/product/tests/test_mac_installer.py \
  tools/product/tests/test_windows_agent_packaging.py
codesign --verify --strict \
  dist/CardputerCompanion.app/Contents/Resources/CardputerCodexMicrophone.driver
codesign --verify --deep --strict dist/CardputerCompanion.app
codesign --verify --deep --strict \
  dist/CardputerCompanion-mac-installer/CardputerCompanion.app
python3 tools/product/audit_public_release.py \
  --repo "${repo_root}" \
  --artifacts "${repo_root}/dist"

tracked_generated="$(
  git ls-files |
    grep -E '^(build|dist)/|wifi_cfg\.bin$' |
    grep -Ev '^dist/[0-9]+\.[0-9]+\.[0-9]+-SHA256SUMS$' ||
    true
)"
if [[ -n "${tracked_generated}" ]]; then
  echo "private or generated artifacts are tracked" >&2
  exit 1
fi
if git ls-files | grep -E -i '\.(wav|aiff|aif|caf|pcm|adpcm|m4a|mp3)$' \
  >/dev/null; then
  echo "audio content artifact is tracked" >&2
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
  dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion \
  dist/CardputerCompanion-mac-installer/install.sh
