import hashlib
import json
import os
import stat
import subprocess
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PACKAGER = ROOT / "tools/product/package_web_installer.py"
STAGER = ROOT / "tools/product/stage_web_installer.py"
INSTALLER = ROOT / "web-installer"
RELEASE_MANIFEST = ROOT / "release/product-release.json"
VERSION = "1.3.1"
FACTORY_ASSET = "Cardputer-Codex-Companion-1.3.1-factory.bin"
FACTORY_SHA256 = (
    "d160bd57953650f516eb727bf087895ded595570288a569844078471f90b0a7c"
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
    assert "Install Factory Firmware 1.3.1" in page
    assert "desktop Chrome or Edge" in page
    assert "Web Serial" in page
    assert "HTTPS" in page
    for reset_item in ("Wi-Fi", "PIN", "profiles", "pets", "BLE pairing"):
        assert reset_item in page
    assert "Cardputer-Codex-Companion-1.3.1l-launcher.bin" in page


def test_release_manifest_pins_factory_digest_used_for_page_stage() -> None:
    release = json.loads(RELEASE_MANIFEST.read_text())

    assert release["sha256"]["firmware_factory"] == FACTORY_SHA256


def test_page_stage_includes_verified_same_origin_factory(tmp_path) -> None:
    firmware = tmp_path / FACTORY_ASSET
    firmware.write_bytes(b"generic factory firmware")
    expected_sha256 = hashlib.sha256(firmware.read_bytes()).hexdigest()
    output = tmp_path / "site"

    subprocess.run(
        [
            "python3",
            str(STAGER),
            "--source",
            str(INSTALLER),
            "--firmware",
            str(firmware),
            "--expected-sha256",
            expected_sha256,
            "--output",
            str(output),
        ],
        check=True,
    )

    assert sorted(path.name for path in output.iterdir()) == [
        FACTORY_ASSET,
        "index.html",
        "manifest.json",
    ]
    assert (output / FACTORY_ASSET).read_bytes() == firmware.read_bytes()


def test_page_stage_rejects_factory_with_wrong_digest(tmp_path) -> None:
    firmware = tmp_path / FACTORY_ASSET
    firmware.write_bytes(b"unexpected firmware")
    output = tmp_path / "site"

    result = subprocess.run(
        [
            "python3",
            str(STAGER),
            "--source",
            str(INSTALLER),
            "--firmware",
            str(firmware),
            "--expected-sha256",
            "0" * 64,
            "--output",
            str(output),
        ],
        capture_output=True,
        text=True,
    )

    assert result.returncode != 0
    assert "firmware sha256 mismatch" in result.stderr
    assert not output.exists()


def test_web_installer_package_is_reproducible_and_minimal(tmp_path) -> None:
    first = tmp_path / "first.zip"
    second = tmp_path / "second.zip"
    firmware = tmp_path / FACTORY_ASSET
    firmware.write_bytes(b"generic factory firmware")
    environment = {**os.environ, "SOURCE_DATE_EPOCH": "1704067201"}

    for output in (first, second):
        subprocess.run(
            [
                "python3",
                str(PACKAGER),
                "--source",
                str(INSTALLER),
                "--firmware",
                str(firmware),
                "--output",
                str(output),
            ],
            check=True,
            env=environment,
        )

    assert first.read_bytes() == second.read_bytes()
    with zipfile.ZipFile(first) as archive:
        assert archive.namelist() == [
            "index.html",
            "manifest.json",
            FACTORY_ASSET,
        ]
        assert archive.read(FACTORY_ASSET) == firmware.read_bytes()
        for entry in archive.infolist():
            mode = stat.S_IMODE(entry.external_attr >> 16)
            assert mode == 0o644
            assert entry.date_time == (2024, 1, 1, 0, 0, 0)
