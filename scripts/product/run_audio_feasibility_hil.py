#!/usr/bin/env python3
"""Run the metrics-only Cardputer BLE audio feasibility gate."""

from __future__ import annotations

import argparse
import json
import os
import select
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any


REQUIRED_REPORT_FIELDS = {
    "duration_seconds",
    "captured_frames",
    "received_frames",
    "source_overruns",
    "transport_drops",
    "sequence_gaps",
    "max_gap_ms",
    "sample_rate_hz",
    "ble_reconnects",
    "hid_p95_us",
    "steady_free_internal",
    "steady_largest_internal",
    "tls_burst_free_internal",
    "allocation_failures",
    "stack_passed",
}
FORBIDDEN_CONTENT_KEYS = {"audio", "pcm", "adpcm", "payload", "samples"}


def validate_duration(duration: int) -> int:
    if not 10 <= duration <= 1800:
        raise ValueError("duration must be within 10..1800 seconds")
    return duration


def _contains_content_key(value: Any) -> bool:
    if isinstance(value, dict):
        return any(
            str(key).lower() in FORBIDDEN_CONTENT_KEYS
            or _contains_content_key(child)
            for key, child in value.items()
        )
    if isinstance(value, list):
        return any(_contains_content_key(child) for child in value)
    return False


def validate_report(report: dict[str, Any], expected_duration: int) -> None:
    missing = REQUIRED_REPORT_FIELDS - set(report)
    if missing:
        raise ValueError(f"missing report fields: {sorted(missing)}")
    if _contains_content_key(report):
        raise ValueError("report contains an audio-content field")
    if report["duration_seconds"] != expected_duration:
        raise ValueError("duration mismatch")
    captured = int(report["captured_frames"])
    if captured <= 0:
        raise ValueError("no captured audio frames")
    losses = (
        int(report["source_overruns"])
        + int(report["transport_drops"])
        + int(report["sequence_gaps"])
    )
    if losses / captured >= 0.01:
        raise ValueError("audio frame loss is not below 1 percent")
    gates = [
        (int(report["max_gap_ms"]) <= 150, "audio gap exceeds 150 ms"),
        (int(report["ble_reconnects"]) == 0, "BLE reconnect observed"),
        (int(report["hid_p95_us"]) <= 20_000, "HID p95 exceeds 20 ms"),
        (
            int(report["steady_free_internal"]) >= 65_536,
            "steady internal heap below 64 KiB",
        ),
        (
            int(report["steady_largest_internal"]) >= 32_768,
            "largest internal block below 32 KiB",
        ),
        (
            int(report["tls_burst_free_internal"]) >= 40_960,
            "TLS-burst internal heap below 40 KiB",
        ),
        (
            int(report["allocation_failures"]) == 0,
            "allocation failure observed",
        ),
        (bool(report["stack_passed"]), "task stack gate failed"),
    ]
    for passed, message in gates:
        if not passed:
            raise ValueError(message)


def _parse_json_from_line(line: str) -> dict[str, Any] | None:
    start = line.find("{")
    if start < 0:
        return None
    try:
        value = json.loads(line[start:])
    except json.JSONDecodeError:
        return None
    return value if isinstance(value, dict) else None


def _read_serial(
    port: Path,
    stop: threading.Event,
    samples: list[dict[str, Any]],
) -> None:
    descriptor = os.open(port, os.O_RDONLY | os.O_NONBLOCK)
    pending = b""
    try:
        while not stop.is_set():
            ready, _, _ = select.select([descriptor], [], [], 0.25)
            if not ready:
                continue
            chunk = os.read(descriptor, 4096)
            if not chunk:
                continue
            pending += chunk
            while b"\n" in pending:
                raw, pending = pending.split(b"\n", 1)
                line = raw.decode("utf-8", errors="replace")
                value = _parse_json_from_line(line)
                if value is not None:
                    samples.append(value)
    finally:
        os.close(descriptor)


def _stack_passed(samples: list[dict[str, Any]]) -> bool:
    task_rows = [
        task
        for sample in samples
        for task in sample.get("tasks", [])
        if isinstance(task, dict) and int(task.get("configured", 0)) > 0
    ]
    return bool(task_rows) and all(
        int(task.get("high_water_free_bytes", 0))
        >= max(1024, int(task["configured"]) // 5)
        for task in task_rows
    )


def merge_metrics(
    companion: dict[str, Any],
    resource_samples: list[dict[str, Any]],
) -> dict[str, Any]:
    if not resource_samples:
        raise ValueError("no firmware resource samples captured")
    audio_rows = [
        sample["audio"]
        for sample in resource_samples
        if isinstance(sample.get("audio"), dict)
    ]
    if not audio_rows:
        raise ValueError("firmware resource samples contain no audio metrics")
    steady_samples = [
        sample
        for sample in resource_samples
        if sample.get("scenario") == "steady"
    ]
    if not steady_samples:
        raise ValueError("no steady firmware resource samples captured")
    tls_samples = [
        sample
        for sample in resource_samples
        if sample.get("scenario") in {"tls_burst", "transient"}
    ]
    if not tls_samples:
        raise ValueError("no tls_burst firmware resource samples captured")
    last_audio = audio_rows[-1]
    hid_values = [
        int(sample.get("hid", {}).get("p95_upper_bound_us", 0))
        for sample in resource_samples
        if isinstance(sample.get("hid"), dict)
    ]
    report = dict(companion)
    report.update(
        {
            "captured_frames": int(last_audio.get("captured_frames", 0)),
            "source_overruns": int(last_audio.get("source_overruns", 0)),
            "transport_drops": int(last_audio.get("transport_drops", 0)),
            "hid_p95_us": max(hid_values, default=0),
            "steady_free_internal": min(
                int(sample.get("free_internal_heap", 0))
                for sample in steady_samples
            ),
            "steady_largest_internal": min(
                int(sample.get("largest_internal_block", 0))
                for sample in steady_samples
            ),
            "tls_burst_free_internal": min(
                int(sample.get("free_internal_heap", 0))
                for sample in tls_samples
            ),
            "allocation_failures": max(
                int(sample.get("allocation_failures", 0))
                for sample in resource_samples
            ),
            "stack_passed": _stack_passed(resource_samples),
        }
    )
    return report


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True, type=Path)
    parser.add_argument("--companion", required=True, type=Path)
    parser.add_argument("--duration", required=True, type=validate_duration)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if not args.port.exists():
        raise SystemExit(f"serial port not found: {args.port}")
    if not os.access(args.companion, os.X_OK):
        raise SystemExit(f"companion is not executable: {args.companion}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    companion_metrics = args.output.with_suffix(".companion.json")
    samples: list[dict[str, Any]] = []
    stop = threading.Event()
    reader = threading.Thread(
        target=_read_serial,
        args=(args.port, stop, samples),
        daemon=True,
    )
    reader.start()
    print("Press G0 once to start microphone capture.")
    process = subprocess.Popen(
        [
            str(args.companion),
            "audio-probe",
            "--duration",
            str(args.duration),
            "--metrics",
            str(companion_metrics),
        ]
    )
    try:
        return_code = process.wait(timeout=args.duration + 30)
    except subprocess.TimeoutExpired:
        process.terminate()
        process.wait(timeout=5)
        raise SystemExit("audio probe timed out")
    finally:
        stop.set()
        reader.join(timeout=2)
    if return_code != 0:
        raise SystemExit(f"audio probe exited with {return_code}")
    companion_report = json.loads(companion_metrics.read_text())
    report = merge_metrics(companion_report, samples)
    validate_report(report, expected_duration=args.duration)
    args.output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n"
    )
    print(f"PASS: {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
