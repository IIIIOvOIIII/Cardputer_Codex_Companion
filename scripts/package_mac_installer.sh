#!/usr/bin/env bash
set -euo pipefail

readonly repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly dist_root="${repo_root}/dist"
readonly output="${dist_root}/CardputerCompanion-mac-installer"
/bin/mkdir -p "$dist_root"
readonly stage_root="$(
  /usr/bin/mktemp -d \
    "${dist_root}/.CardputerCompanion-mac-installer.stage.XXXXXX"
)"
readonly staged="${stage_root}/CardputerCompanion-mac-installer"
readonly backup="${dist_root}/.CardputerCompanion-mac-installer.backup.$$"
install_complete=false

cleanup() {
  /bin/rm -rf "$stage_root"
  if [[ "$install_complete" == true ]]; then
    /bin/rm -rf "$backup"
    return
  fi
  if [[ ! -e "$output" && -e "$backup" ]]; then
    /bin/mv "$backup" "$output"
  fi
}
trap cleanup EXIT

"${repo_root}/scripts/build_companion.sh"
/usr/bin/codesign --verify --deep --strict \
  "${repo_root}/dist/CardputerCompanion.app"

/bin/mkdir -p "${staged}/installer"
/usr/bin/ditto \
  "${repo_root}/dist/CardputerCompanion.app" \
  "${staged}/CardputerCompanion.app"
/bin/cp "${repo_root}/scripts/mac_installer.sh" "${staged}/install.sh"
/bin/cp \
  "${repo_root}/scripts/mac_installer.py" \
  "${staged}/installer/mac_installer.py"
/bin/cp \
  "${repo_root}/scripts/install_companion_launch_agent.py" \
  "${staged}/installer/install_companion_launch_agent.py"
/bin/chmod 0755 \
  "${staged}/install.sh" \
  "${staged}/installer/mac_installer.py" \
  "${staged}/installer/install_companion_launch_agent.py"

/usr/bin/codesign --verify --deep --strict \
  "${staged}/CardputerCompanion.app"

if [[ -e "$backup" ]]; then
  /bin/rm -rf "$backup"
fi
if [[ -e "$output" ]]; then
  /bin/mv "$output" "$backup"
fi
/bin/mv "$staged" "$output"
install_complete=true

printf 'Mac installer: %s\n' "$output"
