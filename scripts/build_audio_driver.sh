#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
driver_source="$repo_root/companion/AudioDriver"
bridge_source="$repo_root/companion/Sources/CAudioBridge"
helper_source="$repo_root/companion/AudioHelper"
bundle="$repo_root/dist/CardputerCodexMicrophone.driver"
executable="$bundle/Contents/MacOS/CardputerCodexMicrophone"
bridge_helper="$repo_root/dist/CardputerAudioBridge"
launchd_plist="$repo_root/dist/com.lynx.cardputer-audio-bridge.plist"
run_tests=false

if [[ "${1:-}" == "--test" ]]; then
  run_tests=true
elif [[ $# -ne 0 ]]; then
  echo "usage: $0 [--test]" >&2
  exit 64
fi

common_flags=(
  -std=c17
  -mmacosx-version-min=14.0
  -Wall
  -Wextra
  -Werror
  -fno-common
  -I"$driver_source"
  -I"$bridge_source/include"
)

if [[ "$run_tests" == true ]]; then
  test_dir="$(mktemp -d "${TMPDIR:-/tmp}/cardputer-audio-driver.XXXXXX")"
  trap 'rm -rf "$test_dir"' EXIT
  xcrun --sdk macosx clang \
    "${common_flags[@]}" \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    "$bridge_source/CardputerAudioRing.c" \
    "$driver_source/CardputerAudioDevice.c" \
    "$driver_source/Tests/CardputerAudioDeviceTests.c" \
    -o "$test_dir/cardputer-audio-device-tests"
  "$test_dir/cardputer-audio-device-tests"
  echo "CardputerAudioDevice tests passed"
  xcrun --sdk macosx clang \
    "${common_flags[@]}" \
    -fblocks \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    "$bridge_source/CardputerAudioRing.c" \
    "$driver_source/CardputerAudioDevice.c" \
    "$driver_source/CardputerAudioIPC.c" \
    "$driver_source/Tests/CardputerAudioIPCTests.c" \
    -framework CoreFoundation \
    -framework Security \
    -o "$test_dir/cardputer-audio-ipc-tests"
  "$test_dir/cardputer-audio-ipc-tests"
  echo "CardputerAudioIPC tests passed"
fi

mkdir -p "$bundle/Contents/MacOS"
cp "$driver_source/Info.plist" "$bundle/Contents/Info.plist"
xcrun --sdk macosx clang \
  "${common_flags[@]}" \
  -fvisibility=hidden \
  -fblocks \
  -DCARDPUTER_AUDIO_DEVELOPMENT=1 \
  -DCARDPUTER_AUDIO_CONSOLE_UID="$(id -u)" \
  -bundle \
  "$bridge_source/CardputerAudioRing.c" \
  "$bridge_source/CardputerAudioXPCClient.c" \
  "$driver_source/CardputerAudioDevice.c" \
  "$driver_source/CardputerAudioDriver.c" \
  -framework CoreAudio \
  -framework CoreFoundation \
  -o "$executable"
codesign --force --sign - "$bundle"
codesign --verify --strict "$bundle"

xcrun --sdk macosx clang \
  "${common_flags[@]}" \
  -fblocks \
  -DCARDPUTER_AUDIO_DEVELOPMENT=1 \
  -DCARDPUTER_AUDIO_CONSOLE_UID="$(id -u)" \
  "$bridge_source/CardputerAudioRing.c" \
  "$driver_source/CardputerAudioIPC.c" \
  "$helper_source/CardputerAudioBridgeMain.c" \
  -framework CoreFoundation \
  -framework Security \
  -o "$bridge_helper"
codesign --force --sign - "$bridge_helper"
codesign --verify --strict "$bridge_helper"
cp \
  "$helper_source/com.lynx.cardputer-audio-bridge.plist" \
  "$launchd_plist"
echo "Audio driver: $bundle"
echo "Audio bridge: $bridge_helper"
