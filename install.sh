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

if [[ "${1:-}" == "install" &&
      ! -d "${install_root}/dist/CardputerCompanion.app" ]]; then
  "${install_root}/scripts/build_companion.sh"
fi

exec /usr/bin/python3 \
  "${install_root}/scripts/mac_installer.py" "$@"
