#!/usr/bin/env bash
set -euo pipefail
umask 077

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
idf_python="$(
  find "${repo_root}/.tools/espressif/python_env" \
    -path '*/bin/python' -print | sort | tail -n 1
)"
nvs_generator="${repo_root}/.tools/esp-idf/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
vault_helper="/Users/nicholasliao/clawd/DevOps_Practice/skills/lynx-vault/scripts/issue_service_account_token.sh"
private_build="${repo_root}/build/private"
private_dist="${repo_root}/dist/private"
wifi_nvs="${private_build}/wifi_cfg.bin"
private_image="${private_dist}/cardputer_codex_companion-private-full.bin"
generic_image="${repo_root}/dist/cardputer_codex_companion-full.bin"

test -x "${idf_python}"
test -f "${nvs_generator}"
test -x "${vault_helper}"
test -f "${repo_root}/firmware/build/cardputer_codex_companion.bin"

vault_token=""
cardputer_wifi_ssid=""
cardputer_wifi_password=""
cleanup() {
  unset vault_token cardputer_wifi_ssid cardputer_wifi_password
}
trap cleanup EXIT

curl -fsSk https://vault.esxi/api/v1/health >/dev/null
session_status="$(curl -fsSk https://vault.esxi/api/v1/sessions/status)"
printf '%s' "${session_status}" | python3 -c '
import json, sys
payload = json.load(sys.stdin)
if payload.get("sealed") is True:
    raise SystemExit("vault is sealed")
' >/dev/null
unset session_status

vault_token="$("${vault_helper}")"
test -n "${vault_token}"

fetch_scalar() {
  local ref="$1"
  curl -fsSk "https://vault.esxi/api/v1/secrets/${ref}" \
    -H "Authorization: Bearer ${vault_token}" |
    python3 -c '
import json, sys
payload = json.load(sys.stdin)
value = payload.get("value")
if not isinstance(value, str) or not value:
    raise SystemExit("vault scalar is missing or empty")
sys.stdout.write(value)
'
}

cardputer_wifi_ssid="$(fetch_scalar shared.wifi.ssid)"
cardputer_wifi_password="$(fetch_scalar shared.wifi.password)"
export CARDPUTER_WIFI_SSID="${cardputer_wifi_ssid}"
export CARDPUTER_WIFI_PASSWORD="${cardputer_wifi_password}"

mkdir -p "${private_build}" "${private_dist}"
python3 "${repo_root}/tools/product/generate_wifi_nvs.py" \
  --output "${wifi_nvs}" \
  --generator "${nvs_generator}" \
  --idf-python "${idf_python}"
python3 "${repo_root}/tools/product/merge_product_image.py" \
  --build-dir "${repo_root}/firmware/build" \
  --output "${private_image}" \
  --idf-python "${idf_python}" \
  --wifi-nvs "${wifi_nvs}"

CARDPUTER_REPO_ROOT="${repo_root}" \
CARDPUTER_WIFI_NVS="${wifi_nvs}" \
CARDPUTER_PRIVATE_IMAGE="${private_image}" \
CARDPUTER_GENERIC_IMAGE="${generic_image}" \
python3 -c '
import os, pathlib, subprocess
root = pathlib.Path(os.environ["CARDPUTER_REPO_ROOT"])
wifi = pathlib.Path(os.environ["CARDPUTER_WIFI_NVS"]).read_bytes()
private = pathlib.Path(os.environ["CARDPUTER_PRIVATE_IMAGE"]).read_bytes()
ssid = os.environ["CARDPUTER_WIFI_SSID"].encode()
password = os.environ["CARDPUTER_WIFI_PASSWORD"].encode()
assert ssid in wifi and password in wifi
assert private[0x12000:0x18000] == wifi
generic_path = pathlib.Path(os.environ["CARDPUTER_GENERIC_IMAGE"])
if generic_path.exists():
    generic = generic_path.read_bytes()
    assert ssid not in generic and password not in generic
tracked = subprocess.check_output(
    [
        "git", "-C", str(root), "ls-files",
        "--cached", "--others", "--exclude-standard",
    ],
    text=True,
).splitlines()
for relative in tracked:
    path = root / relative
    if not path.is_file():
        continue
    data = path.read_bytes()
    assert ssid not in data and password not in data
'

unset CARDPUTER_WIFI_SSID CARDPUTER_WIFI_PASSWORD
printf 'Private Wi-Fi NVS: %s\n' "${wifi_nvs}"
printf 'Private full image: %s\n' "${private_image}"
