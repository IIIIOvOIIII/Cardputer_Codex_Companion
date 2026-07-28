#!/usr/bin/env bash
set -euo pipefail
umask 077

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
idf_python="$(
  find "${repo_root}/.tools/espressif/python_env" \
    -path '*/bin/python' -print | sort | tail -n 1
)"
factory_output="${repo_root}/dist/cardputer_codex_companion-full.bin"
factory_versioned="${repo_root}/dist/Cardputer-Codex-Companion-1.3.2-factory.bin"
factory_app="${repo_root}/dist/cardputer_codex_companion.bin"
factory_app_versioned="${repo_root}/dist/Cardputer-Codex-Companion-1.3.2-app.bin"
launcher_output="${repo_root}/dist/Cardputer-Codex-Companion-1.3.2l-launcher.bin"

test -x "${idf_python}"
(
  cd "${repo_root}/firmware"
  ../scripts/phase0/idf.sh \
    -B build-launcher \
    -D CARDPUTER_LAUNCHER_BUILD=ON \
    -D SDKCONFIG="${repo_root}/firmware/build-launcher/sdkconfig" \
    -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.launcher.defaults" \
    -D PROJECT_VER=1.3.2l \
    build
)
python3 "${repo_root}/tools/product/merge_product_image.py" \
  --build-dir "${repo_root}/firmware/build" \
  --output "${factory_output}" \
  --idf-python "${idf_python}"
/bin/cp \
  "${repo_root}/firmware/build/cardputer_codex_companion.bin" \
  "${factory_app}"
/bin/cp "${factory_output}" "${factory_versioned}"
/bin/cp "${factory_app}" "${factory_app_versioned}"
python3 "${repo_root}/tools/product/package_launcher_image.py" \
  --build-dir "${repo_root}/firmware/build-launcher" \
  --output "${launcher_output}" \
  --idf-python "${idf_python}" \
  --expected-version "1.3.2l"
/bin/chmod 0600 \
  "${factory_app}" \
  "${factory_versioned}" \
  "${factory_app_versioned}" \
  "${launcher_output}"
printf 'Factory image: %s\n' "${factory_versioned}"
printf 'Launcher image: %s\n' "${launcher_output}"
