"""Validation helpers for firmware concurrency evidence identity and report checks."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator


SCHEMA_PATH = (
    Path(__file__).resolve().parent.parent
    .parent
    / "protocol/phase0/firmware-concurrency-report.schema.json"
)

SCHEMA = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))

PAIRING_CODE_RE = re.compile(rb"\b\d{8}\b")
MARKERS = (
    b"cp_admin=",
    b"x-csrf-token",
    b"wifi_credential",
    b"wifi_password",
    b"wifi_psk",
    b"exporter bytes",
    b"exporter_bytes",
    b"request body",
    b"request_body",
    b"text payload",
    b"text_payload",
)
PRIVATE_KEY_RE = re.compile(
    rb"-----begin (?:rsa |ec |openssh )?private key-----", re.IGNORECASE
)

REQUIRED_EVIDENCE_PRODUCERS = {
    "firmware",
    "attacker",
    "macos_companion",
}

REQUIRED_TASK_NAMES = {
    "scanner",
    "hid_sender",
    "nimble",
    "https",
    "wss",
    "display",
    "metrics",
}


@dataclass(frozen=True)
class EvidenceClock:
    producer: str
    run_id: str
    boot_id: str
    app_elf_sha256: str
    firmware_image_sha256: str
    device_id_sha256: str
    first_ns: int
    last_ns: int


RAW_ARTIFACT_KEYS = (
    "raw_serial_log",
    "raw_companion_log",
    "raw_hid_log",
    "raw_resource_log",
    "raw_attacker_log",
    "flash_backup",
)


# Existing behavior must remain stable.
def same_run_errors(events: list[dict]) -> list[str]:
    errors: list[str] = []

    if not events:
        return ["no evidence events"]

    if any(
        item.get("kind") == "gatt_replay_result" and item.get("producer") != "macos_companion"
        for item in events
    ):
        errors.append(
            "gatt_replay_result must be produced by macos_companion"
        )

    for key in (
        "run_id",
        "boot_id",
        "app_elf_sha256",
        "firmware_image_sha256",
        "device_id_sha256",
    ):
        values: set[object] = set()
        for item in events:
            if key not in item:
                values.add(None)
            else:
                values.add(item[key])

        if any(value is None for value in values):
            errors.append(f"{key} missing in evidence")
            continue
        if len(values) != 1:
            errors.append(f"{key} differs across evidence")

    return errors


def forbidden_verdict_fields(value: object, path: str = "") -> list[str]:
    forbidden = {"status", "reported_status", "overall_status"}
    found: list[str] = []

    if isinstance(value, dict):
        for key, child in value.items():
            child_path = f"{path}.{key}" if path else key
            if key in forbidden:
                found.append(child_path)
            found.extend(forbidden_verdict_fields(child, child_path))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            found.extend(forbidden_verdict_fields(child, f"{path}[{index}]"))

    return sorted(found)


def validate_continuous_window(records: list[EvidenceClock]) -> list[str]:
    errors: list[str] = []

    if not records:
        return ["no evidence clocks"]

    producers = [record.producer for record in records]
    if set(producers) != REQUIRED_EVIDENCE_PRODUCERS:
        errors.append(
            "evidence producers must equal attacker, firmware, macos_companion"
        )
    if len(producers) != len(set(producers)):
        errors.append("evidence producers must be unique")
    if any(record.first_ns > record.last_ns for record in records):
        errors.append("evidence clock receipt times must be in ascending order")

    for key in (
        "run_id",
        "boot_id",
        "app_elf_sha256",
        "firmware_image_sha256",
        "device_id_sha256",
    ):
        if len({getattr(record, key) for record in records}) != 1:
            errors.append(f"{key} differs across evidence")

    overlap_start = max(record.first_ns for record in records)
    overlap_end = min(record.last_ns for record in records)
    if overlap_end - overlap_start < 60_000_000_000:
        errors.append("evidence overlap is shorter than 60 seconds")

    return errors


def validate_hid_measurement(hid: dict[str, Any]) -> list[str]:
    errors: list[str] = []

    required = (
        "generated",
        "queued",
        "queue_failures",
        "overflow_samples",
        "p95_upper_bound_us",
        "release_all_observed",
    )

    for key in required:
        if key not in hid:
            errors.append(f"{key} missing")

    generated = hid.get("generated")
    queued = hid.get("queued")
    queue_failures = hid.get("queue_failures")
    overflow_samples = hid.get("overflow_samples")
    p95 = hid.get("p95_upper_bound_us")
    release_all_observed = hid.get("release_all_observed")

    if isinstance(generated, int) and generated < 10_000:
        errors.append("generated must be at least 10000")
    if isinstance(generated, int) and isinstance(queued, int) and generated != queued:
        errors.append("generated must equal queued")
    if queue_failures != 0:
        errors.append("queue_failures must equal zero")
    if overflow_samples != 0:
        errors.append("overflow_samples must equal zero")
    if isinstance(p95, int) and p95 > 20_000:
        errors.append("p95_upper_bound_us must be at most 20000")
    if release_all_observed is not True:
        errors.append("release_all_observed must be true")

    return errors


def validate_resource_measurements(resources: dict[str, Any]) -> list[str]:
    errors: list[str] = []

    if not isinstance(resources, dict):
        return ["resources must be an object"]

    steady = resources.get("steady")
    transient = resources.get("transient")
    attack = resources.get("attack")

    if not isinstance(steady, dict):
        errors.append("steady must be an object")
        steady = {}
    if not isinstance(transient, dict):
        errors.append("transient must be an object")
        transient = {}
    if not isinstance(attack, dict):
        errors.append("attack must be an object")
        attack = {}

    if steady.get("free_internal_heap_min", -1) < 65_536:
        errors.append("steady_free_internal_heap_min must be at least 65536")
    if steady.get("largest_internal_block_min", -1) < 32_768:
        errors.append("steady_largest_internal_block_min must be at least 32768")

    if transient.get("free_internal_heap_min", -1) < 40_960:
        errors.append("transient_free_internal_heap_min must be at least 40960")
    if attack.get("free_internal_heap_min", -1) < 40_960:
        errors.append("attack_free_internal_heap_min must be at least 40960")

    if resources.get("allocation_failures") != 0:
        errors.append("allocation_failures must equal zero")
    if resources.get("metrics_encode_failures") != 0:
        errors.append("metrics_encode_failures must equal zero")

    task_stack_samples = resources.get("task_stack_samples")
    if not isinstance(task_stack_samples, list):
        errors.append("task_stack_samples must be an array")
    else:
        if len(task_stack_samples) != 7:
            errors.append("task_stack_samples must contain exactly 7 samples")

        task_names = [
            sample.get("name")
            for sample in task_stack_samples
            if isinstance(sample, dict)
        ]
        if set(task_names) != REQUIRED_TASK_NAMES or len(task_names) != len(
            set(task_names)
        ):
            errors.append(
                "task_stack_samples names must equal display, hid_sender, "
                "https, metrics, nimble, scanner, wss"
            )

        for sample in task_stack_samples:
            name = sample.get("name", "<unknown>") if isinstance(sample, dict) else "<unknown>"
            configured_bytes = sample.get("configured_bytes", 0) if isinstance(sample, dict) else 0
            minimum_free_bytes = sample.get("minimum_free_bytes", 0) if isinstance(sample, dict) else 0
            minimum_required = max(configured_bytes // 5, 1024)
            if minimum_free_bytes < minimum_required:
                errors.append(
                    f"{name} minimum_free_bytes must be at least {minimum_required}"
                )

    occupancy = resources.get("https_occupancy")
    if not isinstance(occupancy, dict):
        errors.append("https_occupancy must be an object")
    else:
        if occupancy.get("established") != 4:
            errors.append("https_occupancy.established must be exactly 4")
        if occupancy.get("pending_handshakes") != 1:
            errors.append("https_occupancy.pending_handshakes must be exactly 1")

    burst = resources.get("transient_burst")
    if not isinstance(burst, dict):
        errors.append("transient_burst must be an object")
    else:
        if burst.get("window_us") != 5_000_000:
            errors.append("transient_burst.window_us must be exactly 5000000")
        if burst.get("wss_frames") != 100:
            errors.append("transient_burst.wss_frames must be exactly 100")
        if burst.get("wss_bytes") != 100 * 16_384:
            errors.append("transient_burst.wss_bytes must be exactly 1638400")
        if burst.get("import_bytes") != 131_072:
            errors.append("transient_burst.import_bytes must be exactly 131072")
        if burst.get("session_items") != 20:
            errors.append("transient_burst.session_items must be exactly 20")
        if burst.get("approval_fragments") != 4:
            errors.append("transient_burst.approval_fragments must be exactly 4")
        if burst.get("approval_bytes") != 65_536:
            errors.append("transient_burst.approval_bytes must be exactly 65536")

    return errors


def _schema_errors(report: dict[str, Any]) -> list[str]:
    validator = Draft202012Validator(SCHEMA)
    return [
        error.message
        for error in sorted(validator.iter_errors(report), key=lambda item: list(item.path))
    ]


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fp:
        for block in iter(lambda: fp.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _validate_artifact(
    *,
    prefix: str,
    artifact: Any,
    report_dir: Path,
    errors: list[str],
) -> None:
    if not isinstance(artifact, dict):
        errors.append(f"{prefix} must be an artifact object")
        return

    required_fields = (
        "path",
        "sha256",
        "byte_length",
        "first_runner_receipt_ns",
        "last_runner_receipt_ns",
    )
    for field in required_fields:
        if field not in artifact:
            errors.append(f"{prefix}.{field} missing")

    if any(field not in artifact for field in required_fields):
        return

    artifact_path = artifact["path"]
    if not isinstance(artifact_path, str):
        errors.append(f"{prefix}.path must be a relative path")
        return

    candidate = Path(artifact_path)
    if candidate.is_absolute():
        errors.append(f"{prefix}.path must be relative")
        return

    resolved = (report_dir / candidate).resolve()
    try:
        resolved.relative_to(report_dir.resolve())
    except ValueError:
        errors.append(f"{prefix}.path must stay within the report directory")
        return

    if not resolved.is_file():
        errors.append(f"{prefix}.path does not exist")
        return

    expected_len = artifact["byte_length"]
    if not isinstance(expected_len, int) or expected_len < 0:
        errors.append(f"{prefix}.byte_length invalid")
    else:
        actual_len = resolved.stat().st_size
        if actual_len != expected_len:
            errors.append(
                f"{prefix}.byte_length must be {expected_len}, got {actual_len}"
            )

    expected_sha256 = artifact["sha256"]
    if isinstance(expected_sha256, str):
        if not re.fullmatch(r"^[0-9a-f]{64}$", expected_sha256):
            errors.append(f"{prefix}.sha256 must be a lowercase sha256 hex string")
        else:
            actual_sha256 = _sha256_file(resolved)
            if actual_sha256 != expected_sha256:
                errors.append(
                    f"{prefix}.sha256 mismatch: expected {expected_sha256}, got {actual_sha256}"
                )
    else:
        errors.append(f"{prefix}.sha256 must be a string")

    first_receipt = artifact["first_runner_receipt_ns"]
    last_receipt = artifact["last_runner_receipt_ns"]
    if not isinstance(first_receipt, int) or first_receipt < 0:
        errors.append(f"{prefix}.first_runner_receipt_ns must be a non-negative integer")
    if not isinstance(last_receipt, int) or last_receipt < 0:
        errors.append(f"{prefix}.last_runner_receipt_ns must be a non-negative integer")
    if (
        isinstance(first_receipt, int)
        and isinstance(last_receipt, int)
        and first_receipt > last_receipt
    ):
        errors.append(f"{prefix}.receipt times must be in ascending order")

    redacted = resolved.read_bytes()
    lowered = redacted.lower()
    if PAIRING_CODE_RE.search(redacted):
        errors.append(f"{prefix} must not contain an 8-digit pairing code")
    if PRIVATE_KEY_RE.search(redacted):
        errors.append(f"{prefix} contains a PEM private key")

    for marker in MARKERS:
        if marker in lowered:
            errors.append(f"{prefix} contains redacted secret marker {marker.decode('ascii')}")


def _coerce_evidence_clocks(values: Any) -> list[EvidenceClock]:
    if not isinstance(values, list):
        return []

    clocks: list[EvidenceClock] = []
    for value in values:
        if not isinstance(value, dict):
            continue
        try:
            clocks.append(
                EvidenceClock(
                    producer=value["producer"],
                    run_id=value["run_id"],
                    boot_id=value["boot_id"],
                    app_elf_sha256=value["app_elf_sha256"],
                    firmware_image_sha256=value["firmware_image_sha256"],
                    device_id_sha256=value["device_id_sha256"],
                    first_ns=int(value["first_ns"]),
                    last_ns=int(value["last_ns"]),
                )
            )
        except (KeyError, TypeError, ValueError):
            continue
    return clocks


def validate_report(report: dict[str, Any], report_path: Path | None = None) -> list[str]:
    errors: list[str] = []

    errors.extend(_schema_errors(report))

    if not isinstance(report.get("blockers"), list):
        errors.append("blockers must be a list")
    if not isinstance(report.get("consistency_errors"), list):
        errors.append("consistency_errors must be a list")

    for path in forbidden_verdict_fields(report):
        errors.append(f"forbidden verdict field: {path}")

    if report.get("capture_complete"):
        run = report.get("run")
        if not isinstance(run, dict) or run.get("duration_seconds") != 1800:
            errors.append("run.duration_seconds must equal 1800")

    report_dir = report_path.parent if report_path else Path(".")
    artifacts = report.get("artifacts")
    if isinstance(artifacts, dict):
        for key in RAW_ARTIFACT_KEYS:
            if key in artifacts:
                _validate_artifact(
                    prefix=f"artifacts.{key}",
                    artifact=artifacts[key],
                    report_dir=report_dir,
                    errors=errors,
                )

    evidence_raw = report.get("evidence_clocks")
    if isinstance(evidence_raw, list):
        errors.extend(same_run_errors(evidence_raw))
        evidence = _coerce_evidence_clocks(evidence_raw)
        if evidence:
            errors.extend(validate_continuous_window(evidence))

    if isinstance(report.get("hid"), dict):
        errors.extend(validate_hid_measurement(report["hid"]))
    elif report.get("capture_complete"):
        errors.append("hid is required when capture_complete is true")

    if isinstance(report.get("resources"), dict):
        errors.extend(validate_resource_measurements(report["resources"]))
    elif report.get("capture_complete"):
        errors.append("resources is required when capture_complete is true")

    return errors


def _print_summary(report: dict[str, Any]) -> None:
    measurement_sections = [
        "run",
        "hardware",
        "services",
        "ble_identity",
        "https",
        "web_security",
        "wss",
        "resources",
        "hid",
    ]
    measurement_count = sum(
        1 for key in measurement_sections if isinstance(report.get(key), dict)
    )

    artifacts = report.get("artifacts")
    artifact_count = 0
    if isinstance(artifacts, dict):
        artifact_count += sum(1 for key in RAW_ARTIFACT_KEYS if isinstance(artifacts.get(key), dict))

    print(f"capture_complete={'true' if report.get('capture_complete') else 'false'}")
    print(f"measurement_count={measurement_count}")
    print(f"artifact_count={artifact_count}")


def validate_concurrency_report(report_path: Path | str) -> int:
    path = Path(report_path)
    report = json.loads(path.read_text(encoding="utf-8"))
    errors = validate_report(report, path)

    _print_summary(report)
    for error in errors:
        print(f"error={error}")

    if (
        report.get("capture_complete")
        and not report.get("blockers")
        and not report.get("consistency_errors")
        and not errors
    ):
        return 0

    return 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate firmware concurrency reports")
    parser.add_argument("report_path", type=Path)
    args = parser.parse_args(argv)
    return validate_concurrency_report(args.report_path)


if __name__ == "__main__":
    sys.exit(main())
