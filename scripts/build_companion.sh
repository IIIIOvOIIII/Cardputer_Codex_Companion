#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
companion_root="${repo_root}/companion"
app_root="${repo_root}/dist/CardputerCompanion.app"

swift build --package-path "${companion_root}" -c release
mkdir -p "${app_root}/Contents/MacOS"
cp "${companion_root}/.build/release/cardputer-companion" \
  "${app_root}/Contents/MacOS/cardputer-companion"
cp "${companion_root}/AppBundle/Info.plist" "${app_root}/Contents/Info.plist"
chmod 0755 "${app_root}/Contents/MacOS/cardputer-companion"

if command -v codesign >/dev/null 2>&1; then
  codesign --force --sign - \
    --entitlements \
    "${companion_root}/AppBundle/CardputerCompanion.entitlements" \
    "${app_root}" >/dev/null
fi

printf 'Companion app: %s\n' "${app_root}"
