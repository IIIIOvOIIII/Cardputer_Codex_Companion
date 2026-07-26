#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
companion_root="${repo_root}/companion"
app_root="${repo_root}/dist/CardputerCompanion.app"
resources_root="${app_root}/Contents/Resources"

scripts/build_audio_driver.sh
swift build --package-path "${companion_root}" -c release
mkdir -p "${app_root}/Contents/MacOS"
mkdir -p "${resources_root}"
cp "${companion_root}/.build/release/cardputer-companion" \
  "${app_root}/Contents/MacOS/cardputer-companion"
cp "${companion_root}/AppBundle/Info.plist" "${app_root}/Contents/Info.plist"
rm -rf "${resources_root}/CardputerCodexMicrophone.driver"
cp -R "${repo_root}/dist/CardputerCodexMicrophone.driver" \
  "${resources_root}/CardputerCodexMicrophone.driver"
cp "${repo_root}/scripts/install_audio_driver.sh" \
  "${resources_root}/install_audio_driver.sh"
cp "${repo_root}/dist/CardputerAudioBridge" \
  "${resources_root}/CardputerAudioBridge"
cp "${repo_root}/dist/com.lynx.cardputer-audio-bridge.plist" \
  "${resources_root}/com.lynx.cardputer-audio-bridge.plist"
chmod 0755 "${app_root}/Contents/MacOS/cardputer-companion"
chmod 0755 "${resources_root}/install_audio_driver.sh"
chmod 0755 "${resources_root}/CardputerAudioBridge"

if command -v codesign >/dev/null 2>&1; then
  codesign --force --sign - \
    --entitlements \
    "${companion_root}/AppBundle/CardputerCompanion.entitlements" \
    "${app_root}" >/dev/null
fi

printf 'Companion app: %s\n' "${app_root}"
