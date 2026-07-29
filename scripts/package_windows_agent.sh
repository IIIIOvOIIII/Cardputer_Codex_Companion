#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="${WINDOWS_AGENT_VERSION:-1.3.5}"
source_epoch="${SOURCE_DATE_EPOCH:-1577836800}"
archives_only=false

if [[ "${1:-}" == "--archives-only" ]]; then
  archives_only=true
elif [[ $# -ne 0 ]]; then
  echo "usage: package_windows_agent.sh [--archives-only]" >&2
  exit 64
fi

if [[ "${archives_only}" == false ]]; then
  WINDOWS_AGENT_VERSION="${version}" SOURCE_DATE_EPOCH="${source_epoch}" \
    "${repo_root}/scripts/build_windows_agent.sh"
fi

python3 "${repo_root}/tools/product/package_windows_agent.py" \
  --root "${repo_root}" \
  --version "${version}" \
  --source-date-epoch "${source_epoch}"

if [[ "${archives_only}" == true ]]; then
  exit 0
fi

makensis \
  -DVERSION="${version}" \
  -DAGENT_EXE="${repo_root}/build/windows/${version}/amd64/cardputer-agent.exe" \
  -DOUTPUT="${repo_root}/dist/CardputerCompanion-${version}-windows-x64-setup.exe" \
  "${repo_root}/windows-agent/installer/CardputerCompanion.nsi"

echo "Windows Agent packages: ${repo_root}/dist"
