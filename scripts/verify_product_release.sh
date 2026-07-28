#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"
version="1.3.2"
launcher_version="${version}l"
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1577836800}"

python3 tools/product/verify_public_artifacts.py --dist dist
scripts/build_audio_driver.sh
PYTHONPATH=. uv run pytest -q \
  --ignore=tools/product/tests/test_windows_agent_packaging.py
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
node --test web/tests/device_api.test.js

rm -f firmware/sdkconfig firmware/sdkconfig.old
if [[ -f firmware/build/CMakeCache.txt ]]; then
  (
    cd firmware
    ../scripts/phase0/idf.sh -B build fullclean
  )
fi
(
  cd firmware
  ../scripts/phase0/idf.sh -B build set-target esp32s3
  ../scripts/phase0/idf.sh -B build build
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
swift run --package-path companion -c release product-pet-tests
swift run --package-path companion -c release product-telemetry-tests
swift run --package-path companion -c release product-configuration-tests
scripts/test_audio_ring.sh
scripts/build_audio_driver.sh --test
companion/.build/release/cardputer-companion --version
companion/.build/release/cardputer-companion doctor

if [[ -f firmware/build-launcher/CMakeCache.txt ]]; then
  (
    cd firmware
    ../scripts/phase0/idf.sh -B build-launcher fullclean
  )
fi
scripts/package_product_firmware.sh
scripts/build_companion.sh
scripts/package_mac_installer.sh
scripts/package_windows_agent.sh
python3 tools/product/package_web_installer.py \
  --source web-installer \
  --firmware "dist/Cardputer-Codex-Companion-${version}-factory.bin" \
  --output "dist/CardputerCompanion-${version}-web-installer.zip"

(
  cd windows-agent
  go test ./...
  go test -race ./...
)

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
test -f "dist/Cardputer-Codex-Companion-${version}-factory.bin"
test -f "dist/Cardputer-Codex-Companion-${version}-app.bin"
test -f "dist/Cardputer-Codex-Companion-${launcher_version}-launcher.bin"
test -f "dist/CardputerCompanion-${version}-windows-amd64.zip"
test -f "dist/CardputerCompanion-${version}-windows-arm64.zip"
test -f "dist/CardputerCompanion-${version}-windows-x64-setup.exe"
test -f "dist/CardputerCompanion-${version}-web-installer.zip"
python3 tools/product/verify_public_firmware.py \
  --image dist/cardputer_codex_companion-full.bin \
  --layout firmware/partitions_product.csv
"${idf_python}" -m esptool image_info --version 2 \
  firmware/build/cardputer_codex_companion.bin |
  tee build/product-factory-image-info.txt |
  grep -F "App version: ${version}"
python3 tools/product/verify_launcher_firmware.py \
  --image "dist/Cardputer-Codex-Companion-${launcher_version}-launcher.bin" \
  --app-image firmware/build-launcher/cardputer_codex_companion.bin \
  --idf-python "${idf_python}" \
  --expected-version "${launcher_version}"
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
(
  cd dist
  shasum -a 256 \
    "Cardputer-Codex-Companion-${version}-factory.bin" \
    "Cardputer-Codex-Companion-${version}-app.bin" \
    "Cardputer-Codex-Companion-${launcher_version}-launcher.bin" \
    cardputer_codex_companion.bin \
    cardputer_codex_companion-full.bin \
    CardputerCompanion.app/Contents/MacOS/cardputer-companion \
    CardputerCompanion.app/Contents/Resources/CardputerAudioBridge \
    CardputerCompanion.app/Contents/Resources/CardputerCodexMicrophone.driver/Contents/MacOS/CardputerCodexMicrophone \
    CardputerCompanion-mac-installer/install.sh \
    CardputerCompanion-mac-installer/installer/mac_installer.py \
    "CardputerCompanion-${version}-windows-amd64.zip" \
    "CardputerCompanion-${version}-windows-arm64.zip" \
    "CardputerCompanion-${version}-windows-x64-setup.exe" \
    "CardputerCompanion-${version}-web-installer.zip"
) > "dist/${version}-SHA256SUMS"
(
  cd dist
  shasum -a 256 -c "${version}-SHA256SUMS"
)
python3 tools/product/verify_public_artifacts.py \
  --dist dist \
  --require-complete
python3 tools/product/audit_public_release.py \
  --repo "${repo_root}" \
  --artifacts "${repo_root}/dist"
