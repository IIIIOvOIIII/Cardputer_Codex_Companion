#!/usr/bin/env bash
set -euo pipefail

readonly driver_name="CardputerCodexMicrophone.driver"
readonly driver_id="com.lynx.cardputer-codex-microphone.driver"
readonly bridge_name="com.lynx.cardputer-audio-bridge"
readonly bridge_service="com.lynx.cardputer-codex-microphone.ipc"
readonly operation="${1:-}"
readonly source_driver="${2:-}"
readonly source_bridge="${3:-}"
readonly source_launchd="${4:-}"
readonly test_root="${CARDPUTER_AUDIO_INSTALL_TEST_ROOT:-}"
readonly driver_root="${test_root}/Library/Audio/Plug-Ins/HAL"
readonly helper_root="${test_root}/Library/PrivilegedHelperTools"
readonly launchd_root="${test_root}/Library/LaunchDaemons"
readonly driver_target="${driver_root}/${driver_name}"
readonly bridge_target="${helper_root}/${bridge_name}"
readonly launchd_target="${launchd_root}/${bridge_name}.plist"

usage() {
  echo \
    "usage: install_audio_driver.sh install DRIVER BRIDGE LAUNCHD_PLIST | uninstall" \
    >&2
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
if [[ "$operation" == "install" && $# -ne 4 ]]; then
  usage
fi

validate_signature() {
  local target="$1"
  if [[ "${CARDPUTER_AUDIO_SKIP_SIGNATURE_CHECK:-0}" != "1" ]]; then
    /usr/bin/codesign --verify --strict "$target"
  elif [[ -z "$test_root" ]]; then
    echo "signature bypass is test-only" >&2
    exit 78
  fi
}

validate_driver() {
  local bundle="$1"
  local info="${bundle}/Contents/Info.plist"
  local executable="${bundle}/Contents/MacOS/CardputerCodexMicrophone"
  [[ "$(basename "$bundle")" == "$driver_name" ]]
  [[ -f "$info" && -f "$executable" ]]
  [[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$info")" == \
      "$driver_id" ]]
  [[ -n "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$info")" ]]
  validate_signature "$bundle"
}

validate_bridge() {
  local executable="$1"
  [[ -f "$executable" && -x "$executable" ]]
  validate_signature "$executable"
}

validate_launchd() {
  local plist="$1"
  [[ -f "$plist" ]]
  [[ "$(/usr/libexec/PlistBuddy -c 'Print :Label' "$plist")" == \
      "$bridge_name" ]]
  [[ "$(/usr/libexec/PlistBuddy -c 'Print :ProgramArguments:0' "$plist")" == \
      "/Library/PrivilegedHelperTools/${bridge_name}" ]]
  [[ "$(/usr/libexec/PlistBuddy \
      -c "Print :MachServices:${bridge_service}" "$plist")" == "true" ]]
}

stop_bridge() {
  if [[ -z "$test_root" ]]; then
    /bin/launchctl bootout system "$launchd_target" 2>/dev/null || true
  fi
}

/bin/mkdir -p "$driver_root" "$helper_root" "$launchd_root"

if [[ "$operation" == "uninstall" ]]; then
  stop_bridge
  if [[ -L "$driver_target" || -d "$driver_target" ]]; then
    /bin/rm -rf "$driver_target"
  fi
  /bin/rm -f "$bridge_target" "$launchd_target"
  echo "Audio driver and bridge removed."
  exit 0
fi

validate_driver "$source_driver"
validate_bridge "$source_bridge"
validate_launchd "$source_launchd"

readonly stage_driver="$(
  /usr/bin/mktemp -d "${driver_root}/.${driver_name}.stage.XXXXXX"
)"
readonly staged_driver="${stage_driver}/${driver_name}"
readonly stage_helper="$(
  /usr/bin/mktemp -d "${helper_root}/.${bridge_name}.stage.XXXXXX"
)"
readonly staged_bridge="${stage_helper}/${bridge_name}"
readonly staged_launchd="${stage_helper}/${bridge_name}.plist"
readonly driver_backup="${driver_root}/.${driver_name}.backup.$$"
readonly bridge_backup="${helper_root}/.${bridge_name}.backup.$$"
readonly launchd_backup="${launchd_root}/.${bridge_name}.backup.$$"
install_complete=false

cleanup() {
  /bin/rm -rf "$stage_driver" "$stage_helper"
  if [[ "$install_complete" == true ]]; then
    /bin/rm -rf "$driver_backup"
    /bin/rm -f "$bridge_backup" "$launchd_backup"
    return
  fi
  if [[ -L "$driver_target" || -d "$driver_target" ]]; then
    /bin/rm -rf "$driver_target"
  fi
  /bin/rm -f "$bridge_target" "$launchd_target"
  if [[ -e "$driver_backup" || -L "$driver_backup" ]]; then
    /bin/mv "$driver_backup" "$driver_target"
  fi
  if [[ -e "$bridge_backup" ]]; then
    /bin/mv "$bridge_backup" "$bridge_target"
  fi
  if [[ -e "$launchd_backup" ]]; then
    /bin/mv "$launchd_backup" "$launchd_target"
  fi
}
trap cleanup EXIT

/usr/bin/ditto "$source_driver" "$staged_driver"
/bin/cp "$source_bridge" "$staged_bridge"
/bin/cp "$source_launchd" "$staged_launchd"
/bin/chmod 0755 "$staged_bridge"
/bin/chmod 0644 "$staged_launchd"
validate_driver "$staged_driver"
validate_bridge "$staged_bridge"
validate_launchd "$staged_launchd"

stop_bridge
if [[ -e "$driver_target" || -L "$driver_target" ]]; then
  /bin/mv "$driver_target" "$driver_backup"
fi
if [[ -e "$bridge_target" ]]; then
  /bin/mv "$bridge_target" "$bridge_backup"
fi
if [[ -e "$launchd_target" ]]; then
  /bin/mv "$launchd_target" "$launchd_backup"
fi

/bin/mv "$staged_driver" "$driver_target"
/bin/mv "$staged_bridge" "$bridge_target"
/bin/mv "$staged_launchd" "$launchd_target"
if [[ -z "$test_root" ]]; then
  /usr/sbin/chown -R root:wheel "$driver_target"
  /usr/sbin/chown root:wheel "$bridge_target" "$launchd_target"
fi
/bin/chmod -R go-w "$driver_target"
/bin/chmod 0755 "$bridge_target"
/bin/chmod 0644 "$launchd_target"

if [[ -z "$test_root" ]]; then
  /bin/launchctl bootstrap system "$launchd_target"
  /bin/launchctl kickstart -k "system/${bridge_name}"
fi
install_complete=true
echo "Audio driver installed: $driver_target"
echo "Audio bridge installed: $bridge_target"
echo "Restart Core Audio or restart this Mac before running doctor audio."
