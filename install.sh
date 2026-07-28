#!/usr/bin/env bash
set -euo pipefail

readonly install_root="$(
  cd "$(dirname "${BASH_SOURCE[0]}")" && pwd
)"

if [[ "$(/usr/bin/uname -s)" != "Darwin" &&
      -z "${CARDPUTER_MAC_INSTALL_TEST_ROOT:-}" ]]; then
  echo "Cardputer Companion installer requires macOS" >&2
  exit 1
fi

if [[ -f "${install_root}/installer/mac_installer.py" ]]; then
  exec /usr/bin/python3 \
    "${install_root}/installer/mac_installer.py" "$@"
fi

if [[ ! -f "${install_root}/scripts/mac_installer.py" ]]; then
  echo "Cardputer Companion installer layout is invalid" >&2
  exit 1
fi

source_bundle_is_current() {
  /usr/bin/python3 - \
    "${install_root}/companion/AppBundle/Info.plist" \
    "${install_root}/dist/CardputerCompanion.app/Contents/Info.plist" \
    "${install_root}/dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion" \
    <<'PY'
import plistlib
import subprocess
import sys
from pathlib import Path

source_info, built_info, executable = map(Path, sys.argv[1:])
try:
    source = plistlib.loads(source_info.read_bytes())
    built = plistlib.loads(built_info.read_bytes())
    version = source["CFBundleShortVersionString"]
    if (
        not isinstance(version, str)
        or built.get("CFBundleShortVersionString") != version
        or built.get("CFBundleIdentifier") != source.get("CFBundleIdentifier")
        or not executable.is_file()
    ):
        raise ValueError
    result = subprocess.run(
        [str(executable), "--version"],
        capture_output=True,
        check=False,
        text=True,
        timeout=5,
    )
    if (
        result.returncode != 0
        or result.stdout.strip() != f"cardputer-companion {version}"
    ):
        raise ValueError
except (KeyError, OSError, plistlib.InvalidFileException,
        subprocess.SubprocessError, ValueError):
    raise SystemExit(1)
PY
}

if [[ "${1:-}" == "install" ]] && ! source_bundle_is_current; then
  "${install_root}/scripts/build_companion.sh"
fi

exec /usr/bin/python3 \
  "${install_root}/scripts/mac_installer.py" "$@"
