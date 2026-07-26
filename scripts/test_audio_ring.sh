#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/cardputer-audio-ring.XXXXXX")"
trap 'rm -rf "$build_dir"' EXIT

xcrun --sdk macosx clang \
  -std=c17 \
  -Wall -Wextra -Werror \
  -fsanitize=address,undefined \
  -fno-omit-frame-pointer \
  -pthread \
  -I"$repo_root/companion/Sources/CAudioBridge/include" \
  "$repo_root/companion/Sources/CAudioBridge/CardputerAudioRing.c" \
  "$repo_root/companion/Tests/CAudioBridgeTests/CardputerAudioRingTests.c" \
  -o "$build_dir/cardputer-audio-ring-tests"

"$build_dir/cardputer-audio-ring-tests"
echo "CardputerAudioRing tests passed"
