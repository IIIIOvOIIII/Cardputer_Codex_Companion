import hashlib
import json
import subprocess
import sys
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from tools.phase0.validate_concurrency_report import (
    EvidenceClock,
    forbidden_verdict_fields,
    same_run_errors,
    validate_concurrency_report,
    validate_continuous_window,
    validate_hid_measurement,
    validate_report,
    validate_resource_measurements,
)


def _firmware_schema() -> dict:
    return json.loads(
        Path("protocol/phase0/firmware-concurrency-report.schema.json").read_text(
            encoding="utf-8"
        )
    )


def _validate_schema(value: dict) -> list[str]:
    validator = Draft202012Validator(_firmware_schema())
    return [error.message for error in sorted(validator.iter_errors(value), key=str)]


def _artifact_entry(path: Path, data: bytes) -> dict:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return {
        "path": str(path.relative_to(path.parent.parent)),
        "sha256": hashlib.sha256(data).hexdigest(),
        "byte_length": len(data),
        "first_runner_receipt_ns": 1,
        "last_runner_receipt_ns": 3,
    }


def _clock(
    *,
    producer: str,
    run_id: str,
    boot_id: str,
    digest: str,
    first_ns: int,
    last_ns: int,
) -> dict:
    return {
        "producer": producer,
        "run_id": run_id,
        "boot_id": boot_id,
        "app_elf_sha256": digest,
        "firmware_image_sha256": "55" * 32,
        "device_id_sha256": "66" * 32,
        "first_ns": first_ns,
        "last_ns": last_ns,
    }


def _good_report(tmp_path: Path, capture_complete: bool = True) -> dict:
    run_id = "22" * 16
    boot_id = "33" * 16
    elf = "44" * 32

    report = {
        "schema_version": 1,
        "capture_complete": capture_complete,
        "run": {
            "run_id": run_id,
            "boot_id": boot_id,
            "app_elf_sha256": elf,
            "firmware_image_sha256": "55" * 32,
            "device_id_sha256": "66" * 32,
            "continuous_capture": True,
            "git_commit": "d" * 40,
            "git_tree_clean": True,
            "toolchain_manifest_sha256": "ee" * 32,
            "started_at": "2026-07-24T10:00:00Z",
            "ended_at": "2026-07-24T10:30:00Z",
            "duration_seconds": 1800,
        },
        "hardware": {
            "model": "Cardputer",
            "chip": "ESP32-S3",
            "flash_bytes": 8_388_608,
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
            "attack_matrix": [
                {
                    "name": "invalid pairing code",
                    "attempted": 10,
                    "accepted": 0,
                    "rejected": 10,
                    "http_status_code": 403,
                    "reason": "rejected",
                    "allocation_failures_before": 0,
                    "allocation_failures_after": 0,
                    "heap_delta_bytes": 0,
                }
            ],
        },
        "wss": {
            "active": True,
            "handshake_failures": 0,
            "authenticated_seconds": 0,
        },
        "resources": {
            "steady": {
                "free_internal_heap_min": 65_536,
                "largest_internal_block_min": 32_768,
            },
            "transient": {
                "free_internal_heap_min": 40_960,
            },
            "attack": {
                "free_internal_heap_min": 40_960,
            },
            "allocation_failures": 0,
            "metrics_encode_failures": 0,
            "task_stack_samples": [
                {
                    "name": "scanner",
                    "configured_bytes": 4_096,
                    "minimum_free_bytes": 1_024,
                },
                {
                    "name": "hid_sender",
                    "configured_bytes": 4_096,
                    "minimum_free_bytes": 1_024,
                },
                {
                    "name": "nimble",
                    "configured_bytes": 4_096,
                    "minimum_free_bytes": 1_024,
                },
                {
                    "name": "https",
                    "configured_bytes": 4_096,
                    "minimum_free_bytes": 1_024,
                },
                {
                    "name": "wss",
                    "configured_bytes": 4_096,
                    "minimum_free_bytes": 1_024,
                },
                {
                    "name": "display",
                    "configured_bytes": 4_096,
                    "minimum_free_bytes": 1_024,
                },
                {
                    "name": "metrics",
                    "configured_bytes": 4_096,
                    "minimum_free_bytes": 1_024,
                },
            ],
            "https_occupancy": {
                "established": 4,
                "pending_handshakes": 1,
            },
            "transient_burst": {
                "window_us": 5_000_000,
                "wss_frames": 100,
                "wss_bytes": 100 * 16_384,
                "import_bytes": 131_072,
                "session_items": 20,
                "approval_fragments": 4,
                "approval_bytes": 65_536,
            },
        },
        "hid": {
            "generated": 10_000,
            "queued": 10_000,
            "queue_failures": 0,
            "overflow_samples": 0,
            "p95_upper_bound_us": 20_000,
            "release_all_observed": True,
        },
        "evidence_clocks": [
            _clock(
                producer="firmware",
                run_id=run_id,
                boot_id=boot_id,
                digest=elf,
                first_ns=0,
                last_ns=120_000_000_000,
            ),
            _clock(
                producer="attacker",
                run_id=run_id,
                boot_id=boot_id,
                digest=elf,
                first_ns=30_000_000_000,
                last_ns=150_000_000_000,
            ),
            _clock(
                producer="macos_companion",
                run_id=run_id,
                boot_id=boot_id,
                digest=elf,
                first_ns=60_000_000_000,
                last_ns=180_000_000_000,
            ),
        ],
        "artifacts": {
            "raw_serial_log": _artifact_entry(
                tmp_path / "raw" / "raw_serial.log", b"serial\n"
            ),
            "raw_companion_log": _artifact_entry(
                tmp_path / "raw" / "raw_companion.log", b"companion\n"
            ),
            "raw_hid_log": _artifact_entry(tmp_path / "raw" / "raw_hid.log", b"hid\n"),
            "raw_resource_log": _artifact_entry(
                tmp_path / "raw" / "raw_resource.log", b"resource\n"
            ),
            "raw_attacker_log": _artifact_entry(
                tmp_path / "raw" / "raw_attacker.log", b"attacker\n"
            ),
            "flash_backup": _artifact_entry(
                tmp_path / "raw" / "flash_backup.bin", b"backup\n"
            ),
            "report_sha256": "ff" * 32,
        },
        "blockers": [],
        "consistency_errors": [],
    }

    if not capture_complete:
        for key in [
            "hardware",
            "services",
            "ble_identity",
            "https",
            "web_security",
            "wss",
            "resources",
            "hid",
            "evidence_clocks",
            "artifacts",
        ]:
            report.pop(key)

        report["run"].pop("git_commit", None)
        report["run"].pop("git_tree_clean", None)
        report["run"].pop("toolchain_manifest_sha256", None)
        report["run"].pop("started_at", None)
        report["run"].pop("ended_at", None)
        report["run"].pop("duration_seconds", None)
        report["run"].pop("continuous_capture", None)
        report["run"].pop("boot_id", None)
        report["run"].pop("app_elf_sha256", None)
        report["run"].pop("firmware_image_sha256", None)
        report["run"].pop("device_id_sha256", None)
        report["blockers"].append("preflight blocker")

    return report


def _write_report(tmp_path: Path, report: dict) -> Path:
    path = tmp_path / "report.json"
    path.write_text(json.dumps(report), encoding="utf-8")
    return path


def _run_validator(report_path: Path) -> tuple[int, str]:
    completed = subprocess.run(
        [sys.executable, "tools/phase0/validate_concurrency_report.py", str(report_path)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    return completed.returncode, completed.stdout


def test_different_boot_cannot_be_merged() -> None:
    events = [
        {
            "producer": "firmware",
            "run_id": "run-a",
            "boot_id": "boot-a",
            "app_elf_sha256": "22" * 32,
            "firmware_image_sha256": "33" * 32,
            "device_id_sha256": "11" * 32,
        },
        {
            "producer": "macos_companion",
            "run_id": "run-a",
            "boot_id": "boot-b",
            "app_elf_sha256": "22" * 32,
            "firmware_image_sha256": "33" * 32,
            "device_id_sha256": "11" * 32,
        },
    ]
    assert same_run_errors(events) == ["boot_id differs across evidence"]


def test_firmware_cannot_claim_companion_replay_rejection() -> None:
    candidate = {
        "producer": "firmware",
        "run_id": "run-a",
        "boot_id": "boot-a",
        "app_elf_sha256": "22" * 32,
        "firmware_image_sha256": "33" * 32,
        "device_id_sha256": "11" * 32,
        "kind": "gatt_replay_result",
    }
    assert same_run_errors([candidate]) == [
        "gatt_replay_result must be produced by macos_companion"
    ]


def test_forbidden_verdict_fields_nested_path() -> None:
    report = {
        "capture_complete": False,
        "run": {
            "run_id": "22" * 16,
            "boot_id": "33" * 16,
            "app_elf_sha256": "44" * 32,
            "firmware_image_sha256": "55" * 32,
            "device_id_sha256": "66" * 32,
        },
        "blockers": ["blocked"],
        "consistency_errors": [],
        "nested": {"reported_status": "PASS"},
    }
    assert forbidden_verdict_fields(report) == ["nested.reported_status"]


def test_exactly_sixty_second_overlap_is_acceptable() -> None:
    records = [
        EvidenceClock(
            producer="firmware",
            run_id="11" * 16,
            boot_id="22" * 16,
            app_elf_sha256="33" * 32,
            firmware_image_sha256="44" * 32,
            device_id_sha256="55" * 32,
            first_ns=0,
            last_ns=120_000_000_000,
        ),
        EvidenceClock(
            producer="macos_companion",
            run_id="11" * 16,
            boot_id="22" * 16,
            app_elf_sha256="33" * 32,
            firmware_image_sha256="44" * 32,
            device_id_sha256="55" * 32,
            first_ns=30_000_000_000,
            last_ns=150_000_000_000,
        ),
        EvidenceClock(
            producer="attacker",
            run_id="11" * 16,
            boot_id="22" * 16,
            app_elf_sha256="33" * 32,
            firmware_image_sha256="44" * 32,
            device_id_sha256="55" * 32,
            first_ns=60_000_000_000,
            last_ns=180_000_000_000,
        ),
    ]
    assert validate_continuous_window(records) == []


def test_digest_and_boot_mismatch_fails_window() -> None:
    records = [
        EvidenceClock(
            producer="firmware",
            run_id="11" * 16,
            boot_id="22" * 16,
            app_elf_sha256="33" * 32,
            firmware_image_sha256="44" * 32,
            device_id_sha256="55" * 32,
            first_ns=0,
            last_ns=120_000_000_000,
        ),
        EvidenceClock(
            producer="attacker",
            run_id="11" * 16,
            boot_id="22" * 16,
            app_elf_sha256="66" * 32,
            firmware_image_sha256="44" * 32,
            device_id_sha256="55" * 32,
            first_ns=0,
            last_ns=120_000_000_000,
        ),
        EvidenceClock(
            producer="macos_companion",
            run_id="11" * 16,
            boot_id="33" * 16,
            app_elf_sha256="33" * 32,
            firmware_image_sha256="44" * 32,
            device_id_sha256="55" * 32,
            first_ns=0,
            last_ns=120_000_000_000,
        ),
    ]
    assert validate_continuous_window(records) == [
        "boot_id differs across evidence",
        "app_elf_sha256 differs across evidence",
    ]


def test_queue_loss_is_reported_despite_low_p95() -> None:
    assert validate_hid_measurement(
        {
            "generated": 10_000,
            "queued": 9_999,
            "queue_failures": 1,
            "overflow_samples": 0,
            "p95_upper_bound_us": 1_000,
            "release_all_observed": True,
        }
    ) == [
        "generated must equal queued",
        "queue_failures must equal zero",
    ]


def test_resource_thresholds_are_inclusive() -> None:
    resources = {
        "steady": {
            "free_internal_heap_min": 65_536,
            "largest_internal_block_min": 32_768,
        },
        "transient": {"free_internal_heap_min": 40_960},
        "attack": {"free_internal_heap_min": 40_960},
        "allocation_failures": 0,
        "metrics_encode_failures": 0,
        "task_stack_samples": [
            {"name": "scanner", "configured_bytes": 5_120, "minimum_free_bytes": 1_024},
            {"name": "hid_sender", "configured_bytes": 6_000, "minimum_free_bytes": 1_200},
            {"name": "nimble", "configured_bytes": 10_000, "minimum_free_bytes": 2_000},
            {"name": "https", "configured_bytes": 8_192, "minimum_free_bytes": 1_700},
            {"name": "wss", "configured_bytes": 2_048, "minimum_free_bytes": 1_024},
            {"name": "display", "configured_bytes": 32_768, "minimum_free_bytes": 6_553},
            {"name": "metrics", "configured_bytes": 1_024, "minimum_free_bytes": 1_024},
        ],
        "https_occupancy": {
            "established": 4,
            "pending_handshakes": 1,
        },
        "transient_burst": {
            "window_us": 5_000_000,
            "wss_frames": 100,
            "wss_bytes": 100 * 16_384,
            "import_bytes": 131_072,
            "session_items": 20,
            "approval_fragments": 4,
            "approval_bytes": 65_536,
        },
    }
    assert validate_resource_measurements(resources) == []

    below_boundary = dict(resources)
    below_boundary["attack"] = {"free_internal_heap_min": 40_959}
    assert "attack_free_internal_heap_min must be at least 40960" in validate_resource_measurements(
        below_boundary
    )


def test_schema_accepts_capture_incomplete_preflight_report(tmp_path: Path) -> None:
    report = _good_report(tmp_path, capture_complete=False)
    assert _validate_schema(report) == []


def test_complete_report_requires_full_identity_and_exact_duration(
    tmp_path: Path,
) -> None:
    report = _good_report(tmp_path)
    report["run"]["duration_seconds"] = 1799
    del report["run"]["boot_id"]
    schema_errors = _validate_schema(report)
    assert any("1800 was expected" in error for error in schema_errors)
    assert any("'boot_id' is a required property" in error for error in schema_errors)
    assert "run.duration_seconds must equal 1800" in validate_report(
        report, tmp_path / "report.json"
    )


def test_evidence_producers_and_task_names_are_fixed(tmp_path: Path) -> None:
    report = _good_report(tmp_path)
    report["evidence_clocks"][1]["producer"] = "firmware"
    report["resources"]["task_stack_samples"][0]["name"] = "other"
    errors = validate_report(report, tmp_path / "report.json")
    assert "evidence producers must equal attacker, firmware, macos_companion" in errors
    assert "evidence producers must be unique" in errors
    assert (
        "task_stack_samples names must equal display, hid_sender, https, "
        "metrics, nimble, scanner, wss"
    ) in errors


def test_schema_rejects_status_field_in_attack_matrix(tmp_path: Path) -> None:
    report = _good_report(tmp_path)
    report["web_security"]["attack_matrix"][0]["status"] = "PASS"
    assert any("status" in msg for msg in _validate_schema(report))


def test_artifact_validation_flags_length_hash_path_and_traversal(tmp_path: Path) -> None:
    report = _good_report(tmp_path)
    # Length mismatch
    report["artifacts"]["raw_serial_log"]["byte_length"] = 9999
    path = _write_report(tmp_path, report)
    _, stdout = _run_validator(path)
    assert "artifacts.raw_serial_log.byte_length must be 9999" in stdout

    # Hash mismatch
    report = _good_report(tmp_path)
    report["artifacts"]["raw_hid_log"]["sha256"] = "00" * 32
    path = _write_report(tmp_path, report)
    _, stdout = _run_validator(path)
    assert "artifacts.raw_hid_log.sha256 mismatch" in stdout

    # Path traversal
    report = _good_report(tmp_path)
    report["artifacts"]["raw_resource_log"] = {
        **report["artifacts"]["raw_resource_log"],
        "path": "../outside.log",
    }
    path = _write_report(tmp_path, report)
    _, stdout = _run_validator(path)
    assert "artifacts.raw_resource_log.path must stay within the report directory" in stdout


@pytest.mark.parametrize(
    "marker",
    [
        b"00000000",
        b"cp_admin=admin",
        b"X-CSRF-Token: abc",
        b"-----BEGIN PRIVATE KEY-----",
        b"wifi_credential",
        b"exporter bytes",
        b"request body",
        b"text payload",
    ],
)
def test_artifact_validation_redaction_markers(tmp_path: Path, marker: bytes) -> None:
    report = _good_report(tmp_path)
    artifact_path = tmp_path / "raw" / "raw_attacker.log"
    artifact_path.parent.mkdir(parents=True, exist_ok=True)
    artifact_path.write_bytes(marker)
    report["artifacts"]["raw_attacker_log"] = _artifact_entry(artifact_path, marker)

    _, stdout = _run_validator(_write_report(tmp_path, report))

    if marker == b"00000000":
        assert "must not contain an 8-digit pairing code" in stdout
    elif marker.lower().startswith(b"x-csrf"):
        assert "redacted secret marker x-csrf-token" in stdout
    elif b"private key" in marker.lower():
        assert "contains a PEM private key" in stdout
    else:
        assert "contains redacted secret marker" in stdout


def test_nested_verdict_rejection(tmp_path: Path) -> None:
    report = {
        "capture_complete": False,
        "run": {
            "run_id": "22" * 16,
            "boot_id": "33" * 16,
            "app_elf_sha256": "44" * 32,
            "firmware_image_sha256": "55" * 32,
            "device_id_sha256": "66" * 32,
        },
        "blockers": ["blocked"],
        "consistency_errors": [],
        "nested": {"overall_status": "FAIL"},
    }
    errors = validate_report(report, tmp_path / "report.json")
    assert "forbidden verdict field: nested.overall_status" in errors


def test_cli_success_for_complete_report(tmp_path: Path) -> None:
    report = _good_report(tmp_path)
    path = _write_report(tmp_path, report)
    code, stdout = _run_validator(path)

    assert code == 0
    assert "capture_complete=true" in stdout
    assert "measurement_count=9" in stdout
    assert "artifact_count=6" in stdout


def test_cli_nonzero_when_incomplete(tmp_path: Path) -> None:
    report = _good_report(tmp_path, capture_complete=False)
    path = _write_report(tmp_path, report)
    code, stdout = _run_validator(path)

    assert code != 0
    assert "capture_complete=false" in stdout


def test_cli_nonzero_when_consistency_errors_exist(tmp_path: Path) -> None:
    report = _good_report(tmp_path)
    report["hid"]["queue_failures"] = 1
    report["consistency_errors"].append("queue failures observed")

    code, stdout = _run_validator(_write_report(tmp_path, report))

    assert code != 0
    assert "capture_complete=true" in stdout
    assert "queue_failures must equal zero" in stdout
    assert "error=" in stdout


def test_complete_report_without_threshold_failures_fails_validate_report(tmp_path: Path) -> None:
    report = _good_report(tmp_path)
    report["resources"]["transient"]["free_internal_heap_min"] = 40_959

    assert validate_report(report, _write_report(tmp_path, report)) == [
        "transient_free_internal_heap_min must be at least 40960"
    ]
