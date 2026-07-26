#!/usr/bin/env bash
set -euo pipefail

readonly bundle_name="CardputerCodexMicrophone.driver"
readonly bundle_id="com.lynx.cardputer-codex-microphone.driver"
readonly operation="${1:-}"
readonly source_bundle="${2:-}"
readonly test_root="${CARDPUTER_AUDIO_INSTALL_TEST_ROOT:-}"
readonly hal_root="${test_root}/Library/Audio/Plug-Ins/HAL"
readonly target="${hal_root}/${bundle_name}"

usage() {
  echo "usage: install_audio_driver.sh install BUNDLED_DRIVER | uninstall" >&2
  exit 64
}

if [[ "$operation" != "install" && "$operation" != "uninstall" ]]; then
  usage
fi
if [[ -z "$test_root" && "$EUID" -ne 0 ]]; then
  echo "audio driver mutation requires sudo" >&2
  exit 77
fi
if [[ "$operation" == "uninstall" && $# -ne 1 ]]; then
  usage
fi
if [[ "$operation" == "install" && $# -ne 2 ]]; then
  usage
fi

validate_bundle() {
  local bundle="$1"
  local info="${bundle}/Contents/Info.plist"
  local executable="${bundle}/Contents/MacOS/CardputerCodexMicrophone"
  [[ "$(basename "$bundle")" == "$bundle_name" ]]
  [[ -f "$info" && -f "$executable" ]]
  [[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$info")" == \
      "$bundle_id" ]]
  [[ -n "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$info")" ]]
  if [[ "${CARDPUTER_AUDIO_SKIP_SIGNATURE_CHECK:-0}" != "1" ]]; then
    /usr/bin/codesign --verify --strict "$bundle"
  elif [[ -z "$test_root" ]]; then
    echo "signature bypass is test-only" >&2
    exit 78
  fi
}

/bin/mkdir -p "$hal_root"

if [[ "$operation" == "uninstall" ]]; then
  if [[ -L "$target" || -d "$target" ]]; then
    /bin/rm -rf "$target"
  fi
  echo "Audio driver removed: $target"
  exit 0
fi

validate_bundle "$source_bundle"
readonly stage="$(
  /usr/bin/mktemp -d \
    "${hal_root}/.CardputerCodexMicrophone.stage.XXXXXX"
)"
readonly staged_bundle="${stage}/${bundle_name}"
readonly backup="${hal_root}/.CardputerCodexMicrophone.backup.$$"
cleanup() {
  /bin/rm -rf "$stage"
}
trap cleanup EXIT

/usr/bin/ditto "$source_bundle" "$staged_bundle"
validate_bundle "$staged_bundle"
if [[ -e "$target" || -L "$target" ]]; then
  /bin/mv "$target" "$backup"
fi
if ! /bin/mv "$staged_bundle" "$target"; then
  if [[ -e "$backup" || -L "$backup" ]]; then
    /bin/mv "$backup" "$target"
  fi
  exit 1
fi
/bin/rm -rf "$backup"
/usr/sbin/chown -R root:wheel "$target" 2>/dev/null || true
/bin/chmod -R go-w "$target"
echo "Audio driver installed: $target"
echo "Restart Core Audio or restart this Mac before running doctor audio."

