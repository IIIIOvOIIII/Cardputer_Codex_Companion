#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
lock_path="$repo_root/toolchain.lock.json"

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 not found in PATH" >&2
  exit 1
fi
if ! command -v uv >/dev/null 2>&1; then
  echo "uv not found in PATH" >&2
  exit 1
fi

read -r esp_idf_tag esp_idf_commit node_version node_archive node_sha node_python < <(
  python3 - "$lock_path" <<'PY'
import json
import sys

lock_path = sys.argv[1]
with open(lock_path, "r", encoding="utf-8") as f:
    data = json.load(f)
print(
    data["esp_idf"]["tag"],
    data["esp_idf"]["commit"],
    data["node"]["version"],
    data["node"]["archive"],
    data["node"]["sha256"],
    data["python"]["version"],
    sep=" "
)
PY
)

esp_idf_dir="$repo_root/.tools/esp-idf"
espressif_dir="$repo_root/.tools/espressif"
tools_dir="$repo_root/.tools"
node_archive_path="$tools_dir/$node_archive"
node_unpack_dir="${node_archive%.tar.gz}"
python_dir="$tools_dir/uv-python"
python_path="$python_dir/bin/python3"

mkdir -p "$tools_dir"

if [ ! -d "$esp_idf_dir/.git" ]; then
  git clone --recursive https://github.com/espressif/esp-idf.git "$esp_idf_dir"
fi

cd "$esp_idf_dir"
git fetch --all --tags --prune
git checkout "$esp_idf_tag"
git submodule update --init --recursive
actual_head="$(git rev-parse HEAD)"
if [ "$actual_head" != "$esp_idf_commit" ]; then
  echo "esp-idf HEAD mismatch: expected $esp_idf_commit, got $actual_head" >&2
  exit 1
fi

if [ ! -x "$python_path" ]; then
  uv venv "$python_dir" --python "$node_python"
fi

if ! python_version="$("$python_path" --version 2>/dev/null | sed 's/^Python //')"; then
  echo "uv-python path invalid: $python_path" >&2
  exit 1
fi
if [ "$python_version" != "$node_python" ]; then
  echo "uv-python version mismatch: expected $node_python, got ${python_version:-unavailable} at $python_path" >&2
  exit 1
fi

export IDF_TOOLS_PATH="$espressif_dir"
export PYTHON="$python_path"
export PATH="$python_dir/bin:$PATH"
"$esp_idf_dir/install.sh" esp32s3

if [ -d "$tools_dir/$node_unpack_dir" ]; then
  rm -rf "$tools_dir/$node_unpack_dir"
fi
if [ ! -f "$node_archive_path" ]; then
  curl -fsSL "https://nodejs.org/dist/v${node_version}/${node_archive}" -o "$node_archive_path"
fi
actual_sha="$(shasum -a 256 "$node_archive_path" | awk '{print $1}')"
if [ "$actual_sha" != "$node_sha" ]; then
  echo "node archive sha256 mismatch: expected $node_sha, got $actual_sha" >&2
  exit 1
fi
tar -xzf "$node_archive_path" -C "$tools_dir"
