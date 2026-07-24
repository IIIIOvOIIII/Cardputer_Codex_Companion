#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
exec "$repo_root/.tools/node-v22.14.0-darwin-arm64/bin/npm" "$@"
