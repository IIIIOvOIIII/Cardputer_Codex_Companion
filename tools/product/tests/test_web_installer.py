import json
import os
import stat
import subprocess
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PACKAGER = ROOT / "tools/product/package_web_installer.py"
INSTALLER = ROOT / "web-installer"
VERSION = "1.3.0"
FACTORY_ASSET = (
    "https://github.com/IIIIOvOIIII/Cardputer_Codex_Companion/"
    "releases/download/v1.3.0/"
    "Cardputer-Codex-Companion-1.3.0-factory.bin"
)


def test_manifest_installs_factory_image_at_flash_offset_zero() -> None:
    manifest = json.loads((INSTALLER / "manifest.json").read_text())

    assert manifest == {
        "name": "Cardputer Codex Companion",
        "version": VERSION,
        "new_install_prompt_erase": False,
        "new_install_improv_wait_time": 0,
        "builds": [
            {
                "chipFamily": "ESP32-S3",
                "parts": [{"path": FACTORY_ASSET, "offset": 0}],
            }
        ],
    }


def test_installer_page_pins_web_serial_component_and_warns_about_reset() -> None:
    page = (INSTALLER / "index.html").read_text()

    assert (
        "https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"
        in page
    )
    assert '<esp-web-install-button manifest="manifest.json">' in page
    assert "Install Factory Firmware 1.3.0" in page
    assert "desktop Chrome or Edge" in page
    assert "Web Serial" in page
    assert "HTTPS" in page
    for reset_item in ("Wi-Fi", "PIN", "profiles", "pets", "BLE pairing"):
        assert reset_item in page
    assert "Cardputer-Codex-Companion-1.3.0l-launcher.bin" in page


def test_web_installer_package_is_reproducible_and_minimal(tmp_path) -> None:
    first = tmp_path / "first.zip"
    second = tmp_path / "second.zip"
    environment = {**os.environ, "SOURCE_DATE_EPOCH": "1704067201"}

    for output in (first, second):
        subprocess.run(
            [
                "python3",
                str(PACKAGER),
                "--source",
                str(INSTALLER),
                "--output",
                str(output),
            ],
            check=True,
            env=environment,
        )

    assert first.read_bytes() == second.read_bytes()
    with zipfile.ZipFile(first) as archive:
        assert archive.namelist() == ["index.html", "manifest.json"]
        for entry in archive.infolist():
            mode = stat.S_IMODE(entry.external_attr >> 16)
            assert mode == 0o644
            assert entry.date_time == (2024, 1, 1, 0, 0, 0)

