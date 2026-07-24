import json
import sys
from pathlib import Path

from jsonschema import Draft202012Validator

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from tools.phase0.validate_concurrency_report import (
    forbidden_verdict_fields,
    same_run_errors,
)


def event(producer: str, run_id: str, boot_id: str, digest: str) -> dict:
    return {
        "producer": producer,
        "run_id": run_id,
        "boot_id": boot_id,
        "app_elf_sha256": digest,
        "firmware_image_sha256": "33" * 32,
        "device_id_sha256": "11" * 32,
        "observed_at_ns": 100,
    }


def _firmware_schema() -> dict:
    return json.loads(
        Path("protocol/phase0/firmware-concurrency-report.schema.json").read_text(
            encoding="utf-8"
        )
    )


def _companion_schema() -> dict:
    return json.loads(
        Path("protocol/phase0/companion-probe-event.schema.json").read_text(
            encoding="utf-8"
        )
    )


def _validate(schema: dict, value: dict) -> list[str]:
    validator = Draft202012Validator(schema)
    return [error.message for error in sorted(validator.iter_errors(value), key=str)]


def _good_report() -> dict:
    return {
        "schema_version": 1,
        "capture_complete": True,
        "run": {
            "run_id": "22" * 16,
            "boot_id": "33" * 16,
            "app_elf_sha256": "44" * 32,
            "firmware_image_sha256": "55" * 32,
            "device_id_sha256": "66" * 32,
            "git_commit": "d" * 40,
            "git_tree_clean": True,
            "toolchain_manifest_sha256": "ee" * 32,
            "started_at": "2026-07-24T10:00:00Z",
            "ended_at": "2026-07-24T10:10:00Z",
            "duration_seconds": 600,
            "continuous_capture": True,
        },
        "hardware": {
            "model": "Cardputer",
            "chip": "ESP32-S3",
            "flash_bytes": 8388608,
            "psram_bytes": 0,
        },
        "services": {
            "ble_hid": True,
            "encrypted_gatt": True,
            "wifi": True,
            "https": True,
            "wss_authenticated": True,
            "all_live": True,
        },
        "ble_identity": {
            "hid_serial_base32": "CEIRCEIRCEIRCEIRCEIRCEIRCE",
            "gatt_device_id_hex": "11" * 16,
            "gatt_link_encrypted": True,
            "gatt_link_authenticated": True,
            "bonded": True,
            "same_device": True,
        },
        "https": {
            "running": True,
            "requests_served": 0,
            "bad_requests": 0,
            "connection_errors": 0,
        },
        "web_security": {
            "challenge_accepted": True,
            "gatt_replay_rejects": 0,
        },
        "wss": {
            "active": True,
            "handshake_failures": 0,
            "authenticated_seconds": 0,
        },
        "resources": {
            "heap_free_bytes": 10000,
            "largest_block_bytes": 4000,
            "max_stack_bytes": 1024,
        },
        "hid": {
            "generated": 12000,
            "queued": 12000,
            "queue_failures": 0,
            "buckets": [{"upper_us": 100, "count": 12000}],
        },
        "artifacts": {
            "raw_log_sha256": "77" * 32,
            "raw_hid_log_sha256": "88" * 32,
            "raw_resource_log_sha256": "99" * 32,
            "report_sha256": "aa" * 32,
        },
        "blockers": [],
        "consistency_errors": [],
    }


def _good_event() -> dict:
    return {
        "producer": "macos_companion",
        "kind": "ready",
        "run_id": "22" * 16,
        "boot_id": "33" * 16,
        "app_elf_sha256": "44" * 32,
        "firmware_image_sha256": "55" * 32,
        "device_id_sha256": "66" * 32,
        "producer_monotonic_ns": 1,
        "observed_at_ns": 2,
    }


def test_different_boot_cannot_be_merged() -> None:
    events = [
        event("firmware", "run-a", "boot-a", "22" * 32),
        event("macos_companion", "run-a", "boot-b", "22" * 32),
    ]
    assert same_run_errors(events) == ["boot_id differs across evidence"]


def test_firmware_cannot_claim_companion_replay_rejection() -> None:
    candidate = event("firmware", "run-a", "boot-a", "22" * 32)
    candidate["kind"] = "gatt_replay_result"
    assert same_run_errors([candidate]) == [
        "gatt_replay_result must be produced by macos_companion",
    ]


def test_child_report_cannot_claim_gate_status() -> None:
    report = {
        "capture_complete": True,
        "status": "PASS",
        "nested": {"reported_status": "PASS"},
    }
    assert forbidden_verdict_fields(report) == [
        "nested.reported_status",
        "status",
    ]


def test_same_run_validation_fails_when_identity_missing() -> None:
    events = [
        {"producer": "firmware", "run_id": "run-a", "observed_at_ns": 1},
        {
            "producer": "macos_companion",
            "run_id": "run-a",
            "boot_id": "boot",
            "observed_at_ns": 2,
        },
    ]
    assert "boot_id missing in evidence" in same_run_errors(events)
    assert "app_elf_sha256 missing in evidence" in same_run_errors(events)


def test_firmware_schema_rejects_status_fields() -> None:
    data = _good_report()
    data["status"] = "PASS"
    assert _validate(_firmware_schema(), data)


def test_firmware_schema_rejects_additional_fields() -> None:
    data = _good_report()
    data["hardware"]["unexpected"] = 1
    assert _validate(_firmware_schema(), data)


def test_firmware_schema_rejects_missing_required_identity() -> None:
    data = _good_report()
    del data["hardware"]
    assert _validate(_firmware_schema(), data)


def test_companion_schema_forbids_unexpected_raw_secret_payloads() -> None:
    data = _good_event()
    data["raw_device_id"] = "11" * 16
    assert _validate(_companion_schema(), data)


def test_companion_schema_requires_known_producer_and_kind() -> None:
    data = _good_event()
    data["producer"] = "firmware"
    data["kind"] = "ready"
    assert _validate(_companion_schema(), data)


def test_companion_schema_rejects_missing_monotonic_time() -> None:
    data = _good_event()
    del data["producer_monotonic_ns"]
    assert _validate(_companion_schema(), data)


def test_valid_fixtures_pass_schema_validation() -> None:
    assert not _validate(_firmware_schema(), _good_report())
    assert not _validate(_companion_schema(), _good_event())
