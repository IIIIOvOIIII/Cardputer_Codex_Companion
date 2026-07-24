import sys
import io
from pathlib import Path
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from scripts.phase0.capture_hardware_manifest import (
    _coerce_physical_verification,
    _hash_usb_serial,
    _read_json_event,
    capture_from_event,
)
from tools.phase0.validate_hardware_manifest import validate_manifest


def _hardware_runtime() -> dict:
    return {
        "chip_model": "ESP32-S3",
        "chip_revision": 1,
        "flash_jedec_id": "c84017",
        "flash_bytes": 8_388_608,
        "psram_bytes": 0,
    }


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


def test_read_json_event_rejects_non_explicit_payload() -> None:
    payload = "{\"chip_model\": \"ESP32-S3\", \"chip_revision\": 1, \"flash_jedec_id\": \"c84017\", \"flash_bytes\": 8388608, \"psram_bytes\": 0}"
    with pytest.raises(ValueError, match="expected exactly one hardware_runtime event"):
        _read_json_event(None, io.StringIO(payload))


def test_read_json_event_rejects_zero_events() -> None:
    payload = "{\"event\": \"heartbeat\", \"status\": \"ok\"}"
    with pytest.raises(ValueError, match="expected exactly one hardware_runtime event"):
        _read_json_event(None, io.StringIO(payload))


def test_read_json_event_rejects_multiple_runtime_events() -> None:
    payload = (
        '{"type": "hardware_runtime", "chip_model": "ESP32-S3", "chip_revision": 1, '
        '"flash_jedec_id": "c84017", "flash_bytes": 8388608, "psram_bytes": 0}\n'
        '{"hardware_runtime": {"chip_model": "ESP32-S3", "chip_revision": 1, '
        '"flash_jedec_id": "c84017", "flash_bytes": 8388608, "psram_bytes": 0}}'
    )
    with pytest.raises(ValueError, match="expected exactly one hardware_runtime event, found 2"):
        _read_json_event(None, io.StringIO(payload))


def test_capture_from_event_requires_explicit_physical_verification() -> None:
    runtime = _hardware_runtime()
    with pytest.raises(ValueError, match="keyboard matrix source must be explicitly confirmed"):
        capture_from_event(
            runtime,
            product_revision="1.2",
            pcb_revision="K132",
            usb_serial_sha256=_hash_usb_serial("ABCD1234"),
            physically_verified=False,
        )


def test_hash_usb_serial_and_capture_output_does_not_include_raw() -> None:
    raw_serial = "USB-SN-ABCD"
    hashed = _hash_usb_serial(raw_serial)
    runtime = _hardware_runtime()
    manifest = capture_from_event(
        runtime,
        product_revision="1.2",
        pcb_revision="K132",
        usb_serial_sha256=hashed,
        physically_verified=True,
    )
    encoded = io.StringIO()
    import json

    encoded.write(json.dumps(manifest))
    serial_dump = encoded.getvalue()
    assert manifest["usb_serial_sha256"] == hashed
    assert raw_serial not in serial_dump


def test_coerce_physical_verification_requires_true() -> None:
    assert _coerce_physical_verification("yes") is True
    assert _coerce_physical_verification("no") is False
    assert _coerce_physical_verification("1") is True
    assert _coerce_physical_verification("0") is False
    assert _coerce_physical_verification(None) is False
