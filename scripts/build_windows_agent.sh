#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
agent_root="${repo_root}/windows-agent"
version="${WINDOWS_AGENT_VERSION:-1.2.3}"
source_epoch="${SOURCE_DATE_EPOCH:-1577836800}"

if [[ ! "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "invalid Windows Agent version" >&2
  exit 64
fi
if [[ ! "${source_epoch}" =~ ^[0-9]+$ ]]; then
  echo "invalid SOURCE_DATE_EPOCH" >&2
  exit 64
fi

cd "${agent_root}"
go test ./...

for architecture in amd64 arm64; do
  output="${repo_root}/build/windows/${version}/${architecture}"
  mkdir -p "${output}"
  SOURCE_DATE_EPOCH="${source_epoch}" \
    GOOS=windows GOARCH="${architecture}" CGO_ENABLED=0 \
    go build \
      -trimpath \
      -buildvcs=false \
      -ldflags="-s -w -X main.version=${version}" \
      -o "${output}/cardputer-agent.exe" \
      ./cmd/cardputer-agent
done

echo "Windows Agent binaries: ${repo_root}/build/windows/${version}"
