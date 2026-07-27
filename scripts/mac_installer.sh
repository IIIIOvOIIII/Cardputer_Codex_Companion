#!/usr/bin/env bash
set -euo pipefail

readonly script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
installer="${script_dir}/mac_installer.py"
if [[ ! -f "$installer" ]]; then
  installer="${script_dir}/installer/mac_installer.py"
fi
exec /usr/bin/python3 "$installer" "$@"
