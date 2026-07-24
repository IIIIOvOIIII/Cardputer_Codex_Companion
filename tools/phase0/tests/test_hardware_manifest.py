import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from tools.phase0.validate_hardware_manifest import validate_manifest


def valid_manifest() -> dict:
    return {
        "manifest_version": 1,
        "model": "M5Stack Cardputer",
        "product_revision": "1.2",
        "pcb_revision": "K132",
        "chip_model": "ESP32-S3",
        "chip_revision": 1,
        "flash_jedec_id": "c84017",
        "flash_bytes": 8388608,
        "psram_bytes": 0,
        "usb_serial_sha256": "1a" * 32,
        "keyboard_matrix_source": {
            "repository": "m5stack/M5Cardputer",
            "commit": "2d4fa6646e4e5b47e0af96214b003aa7b15b8d81",
            "outputs": [8, 9, 11],
            "inputs": [13, 15, 3, 4, 5, 6, 7],
            "physically_verified": True,
        },
        "captured_at": "2026-07-24T08:00:00Z",
    }


def test_exact_target_is_accepted() -> None:
    assert validate_manifest(valid_manifest()) == []

def test_psram_or_wrong_flash_is_rejected() -> None:
    manifest = valid_manifest()
    manifest["psram_bytes"] = 2_097_152
    manifest["flash_bytes"] = 4_194_304
    assert validate_manifest(manifest) == [
        "flash_bytes must equal 8388608",
        "psram_bytes must equal 0",
    ]
