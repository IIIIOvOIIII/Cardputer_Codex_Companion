#!/usr/bin/env python3
"""Run the metrics-only Cardputer BLE audio feasibility gate."""

from __future__ import annotations

import argparse
import ipaddress
import json
import os
import select
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any
from urllib.parse import urlsplit, urlunsplit


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
    "signal_peak",
    "signal_rms",
    "nonzero_sample_percent",
    "hid_generated",
    "hid_queued",
    "hid_queue_failures",
    "hid_p95_us",
    "steady_free_internal",
    "steady_largest_internal",
    "tls_burst_free_internal",
    "allocation_failures",
    "stack_passed",
}
FORBIDDEN_CONTENT_KEYS = {"audio", "pcm", "adpcm", "payload", "samples"}
AUDIO_FRAME_DURATION_MS_BY_RATE = {24_000: 19, 16_000: 28}
TLS_RESOURCE_SAMPLE_SETTLE_SECONDS = 1.25
TLS_HANDSHAKE_LOOKBACK_SECONDS = 2.0


def validate_duration(duration: int | str) -> int:
    parsed = int(duration)
    if not 10 <= parsed <= 1800:
        raise ValueError("duration must be within 10..1800 seconds")
    return parsed


def validate_device_url(value: str) -> str:
    parsed = urlsplit(value)
    host = parsed.hostname
    if (
        parsed.scheme != "https"
        or host is None
        or parsed.username is not None
        or parsed.password is not None
        or parsed.path not in {"", "/"}
        or parsed.query
        or parsed.fragment
    ):
        raise ValueError("device URL must be a local HTTPS base URL")
    try:
        local_host = ipaddress.ip_address(host).is_private
    except ValueError:
        local_host = host.lower().endswith(".local")
    if not local_host:
        raise ValueError("device URL must be a local HTTPS base URL")
    authority = host if parsed.port is None else f"{host}:{parsed.port}"
    return urlunsplit(("https", authority, "/api/v1/status", "", ""))


def tls_probe_schedule(duration: int) -> list[int]:
    return list(range(8, duration, 15))


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
    received = int(report["received_frames"])
    duration_ms = AUDIO_FRAME_DURATION_MS_BY_RATE.get(
        int(report["sample_rate_hz"])
    )
    if duration_ms is None:
        raise ValueError("unsupported sample rate")
    minimum_continuous_frames = (
        expected_duration * 1_000 // duration_ms * 99 // 100
    )
    if captured < minimum_continuous_frames or received < minimum_continuous_frames:
        raise ValueError("continuous audio stream ended before the HIL window")
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
        (int(report["signal_peak"]) > 0, "microphone signal peak is zero"),
        (float(report["signal_rms"]) > 0, "microphone signal RMS is zero"),
        (
            0 < float(report["nonzero_sample_percent"]) <= 100,
            "microphone nonzero sample percentage is invalid",
        ),
        (
            int(report["hid_generated"]) >= 1_000,
            "fewer than 1000 HID events generated",
        ),
        (
            int(report["hid_queued"]) >= int(report["hid_generated"]),
            "HID event was not queued",
        ),
        (
            int(report["hid_queue_failures"]) == 0,
            "HID queue failure observed",
        ),
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


def persist_and_validate_report(
    report: dict[str, Any],
    output: Path,
    expected_duration: int,
) -> None:
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    validate_report(report, expected_duration=expected_duration)


def _parse_json_from_line(line: str) -> dict[str, Any] | None:
    start = line.find("{")
    if start < 0:
        return None
    try:
        value = json.loads(line[start:])
    except json.JSONDecodeError:
        return None
    return value if isinstance(value, dict) else None


class SerialMonitor:
    def __init__(
        self,
        descriptor: int,
        samples: list[dict[str, Any]],
        tls_active: threading.Event | None = None,
    ) -> None:
        self._descriptor = descriptor
        self._samples = samples
        self._tls_active = tls_active
        self._stop = threading.Event()
        self._condition = threading.Condition()
        self._lines: list[str] = []
        self._all_lines: list[str] = []
        self._recent_samples: list[tuple[float, dict[str, Any]]] = []
        self._thread = threading.Thread(target=self._read, daemon=True)

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2)

    def send(self, command: bytes) -> None:
        pending = memoryview(command)
        deadline = time.monotonic() + 5
        while pending:
            try:
                written = os.write(self._descriptor, pending)
            except BlockingIOError:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("serial command write timed out")
                select.select([], [self._descriptor], [], remaining)
                continue
            if written <= 0:
                raise OSError("serial command write made no progress")
            pending = pending[written:]

    def wait_for(self, pattern: str, timeout: float) -> bool:
        deadline = time.monotonic() + timeout
        with self._condition:
            while True:
                if any(pattern in line for line in self._lines):
                    return True
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return False
                self._condition.wait(remaining)

    def wait_for_resource_sample(self, timeout: float) -> bool:
        deadline = time.monotonic() + timeout
        with self._condition:
            while True:
                if any(
                    isinstance(sample.get("audio"), dict)
                    for sample in self._samples
                ):
                    return True
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return False
                self._condition.wait(remaining)

    def clear_lines(self) -> None:
        with self._condition:
            self._lines.clear()

    @property
    def lines(self) -> list[str]:
        with self._condition:
            return list(self._all_lines)

    def wait_for_any(
        self,
        patterns: tuple[str, ...],
        timeout: float,
    ) -> str | None:
        deadline = time.monotonic() + timeout
        with self._condition:
            while True:
                for pattern in patterns:
                    if any(pattern in line for line in self._lines):
                        return pattern
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                self._condition.wait(remaining)

    def _read(self) -> None:
        pending = b""
        while not self._stop.is_set():
            try:
                ready, _, _ = select.select(
                    [self._descriptor], [], [], 0.25
                )
                if not ready:
                    continue
                chunk = os.read(self._descriptor, 4096)
            except (BlockingIOError, OSError):
                if self._stop.is_set():
                    return
                continue
            if not chunk:
                continue
            pending += chunk
            while b"\n" in pending:
                raw, pending = pending.split(b"\n", 1)
                line = raw.decode("utf-8", errors="replace").rstrip("\r")
                received_at = time.monotonic()
                value = _parse_json_from_line(line)
                if value is not None:
                    if (
                        self._tls_active is not None
                        and self._tls_active.is_set()
                    ):
                        value["scenario"] = "tls_burst"
                    self._samples.append(value)
                    self._recent_samples.append((received_at, value))
                if "performing session handshake" in line:
                    for sample_at, sample in self._recent_samples:
                        if (
                            received_at - sample_at
                            <= TLS_HANDSHAKE_LOOKBACK_SECONDS
                        ):
                            sample["scenario"] = "tls_burst"
                self._recent_samples = [
                    (sample_at, sample)
                    for sample_at, sample in self._recent_samples
                    if (
                        received_at - sample_at
                        <= TLS_HANDSHAKE_LOOKBACK_SECONDS
                    )
                ]
                with self._condition:
                    self._lines.append(line)
                    self._all_lines.append(line)
                    if len(self._lines) > 128:
                        del self._lines[:-128]
                    self._condition.notify_all()


def run_probe_process(
    process: subprocess.Popen[Any],
    monitor: SerialMonitor,
    timeout: float,
) -> int:
    failed = False
    try:
        if not monitor.wait_for("BLE audio sink ready=1", timeout=30):
            process.terminate()
            process.wait(timeout=5)
            raise TimeoutError("BLE audio sink readiness timed out")
        if not monitor.wait_for_resource_sample(timeout=2):
            process.terminate()
            process.wait(timeout=5)
            raise TimeoutError("firmware metric baseline timed out")
        accepted = False
        for _ in range(10):
            monitor.clear_lines()
            monitor.send(b"HIL MIC START\n")
            response = monitor.wait_for_any(
                (
                    "HIL MIC START ACCEPTED",
                    "HIL MIC START REJECTED",
                ),
                timeout=1,
            )
            if response == "HIL MIC START ACCEPTED":
                accepted = True
                break
        if not accepted:
            process.terminate()
            process.wait(timeout=5)
            raise TimeoutError("microphone START was not accepted")
        hid_accepted = False
        for _ in range(20):
            monitor.clear_lines()
            monitor.send(b"HIL HID START\n")
            hid_response = monitor.wait_for_any(
                (
                    "HIL HID START ACCEPTED",
                    "HIL HID START REJECTED",
                ),
                timeout=1,
            )
            if hid_response == "HIL HID START ACCEPTED":
                hid_accepted = True
                break
            time.sleep(0.25)
        if not hid_accepted:
            process.terminate()
            process.wait(timeout=5)
            raise TimeoutError("HID burst was not accepted")
        try:
            return_code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            process.terminate()
            process.wait(timeout=5)
            raise TimeoutError("audio probe timed out") from error
        return return_code
    except BaseException:
        failed = True
        raise
    finally:
        cleanup_errors = []
        cleanup_commands = (
            (
                b"HIL HID STOP\n",
                ("HIL HID STOP STOPPED", "HIL HID STOP NOOP"),
            ),
            (
                b"HIL MIC STOP\n",
                (
                    "HIL MIC STOP ACCEPTED",
                    "HIL MIC STOP REJECTED",
                    "HIL MIC STOP NOOP",
                ),
            ),
        )
        for command, responses in cleanup_commands:
            try:
                monitor.clear_lines()
                monitor.send(command)
                if monitor.wait_for_any(responses, timeout=1) is None:
                    raise TimeoutError(
                        f"no cleanup acknowledgement for {command!r}"
                    )
            except Exception as cleanup_error:
                cleanup_errors.append(cleanup_error)
        if cleanup_errors and not failed:
            raise cleanup_errors[0]
        for cleanup_error in cleanup_errors:
            print(
                f"warning: HIL cleanup failed: {cleanup_error}",
                file=sys.stderr,
            )


def _exercise_tls(
    status_url: str,
    duration: int,
    started_at: float,
    stop: threading.Event,
    active: threading.Event,
) -> None:
    for offset in tls_probe_schedule(duration):
        wait_seconds = max(0.0, started_at + offset - time.monotonic())
        if stop.wait(wait_seconds):
            return
        active.set()
        try:
            subprocess.run(
                [
                    "/usr/bin/curl",
                    "-sk",
                    "--max-time",
                    "5",
                    status_url,
                ],
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        finally:
            stop.wait(TLS_RESOURCE_SAMPLE_SETTLE_SECONDS)
            active.clear()


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


def _counter_growth(values: list[int]) -> int:
    if len(values) < 2:
        return 0
    total = 0
    previous = values[0]
    for current in values[1:]:
        total += current - previous if current >= previous else current
        previous = current
    return total


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
    def audio_growth(name: str) -> int:
        return _counter_growth(
            [int(row.get(name, 0)) for row in audio_rows]
        )

    hid_values = [
        int(sample.get("hid", {}).get("p95_upper_bound_us", 0))
        for sample in resource_samples
        if isinstance(sample.get("hid"), dict)
    ]
    hid_rows = [
        sample["hid"]
        for sample in resource_samples
        if isinstance(sample.get("hid"), dict)
    ]
    def hid_growth(name: str) -> int:
        return _counter_growth(
            [int(row.get(name, 0)) for row in hid_rows]
        )

    report = dict(companion)
    report.update(
        {
            "captured_frames": audio_growth("captured_frames"),
            "source_overruns": audio_growth("source_overruns"),
            "transport_drops": audio_growth("transport_drops"),
            "hid_generated": hid_growth("generated"),
            "hid_queued": hid_growth("queued"),
            "hid_queue_failures": hid_growth("queue_failures"),
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
            "allocation_failures": _counter_growth([
                int(sample.get("allocation_failures", 0))
                for sample in resource_samples
            ]),
            "stack_passed": _stack_passed(resource_samples),
        }
    )
    return report


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True, type=Path)
    parser.add_argument("--companion", required=True, type=Path)
    parser.add_argument(
        "--device-url",
        required=True,
        type=validate_device_url,
        help="local HTTPS Cardputer base URL",
    )
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
    tls_active = threading.Event()
    descriptor = os.open(args.port, os.O_RDWR | os.O_NONBLOCK)
    monitor = SerialMonitor(
        descriptor, samples, tls_active=tls_active
    )
    monitor.start()
    print("USB HIL control ready; microphone will start automatically.")
    started_at = time.monotonic()
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
    tls_probe = threading.Thread(
        target=_exercise_tls,
        args=(
            args.device_url,
            args.duration,
            started_at,
            stop,
            tls_active,
        ),
        daemon=True,
    )
    tls_probe.start()
    try:
        return_code = run_probe_process(
            process,
            monitor,
            timeout=args.duration + 30,
        )
    except TimeoutError as error:
        raise SystemExit(str(error)) from error
    finally:
        stop.set()
        tls_probe.join(timeout=6)
        monitor.stop()
        args.output.with_suffix(".serial.log").write_text(
            "\n".join(monitor.lines) + "\n"
        )
        os.close(descriptor)
    if return_code != 0:
        raise SystemExit(f"audio probe exited with {return_code}")
    companion_report = json.loads(companion_metrics.read_text())
    report = merge_metrics(companion_report, samples)
    persist_and_validate_report(
        report,
        args.output,
        expected_duration=args.duration,
    )
    print(f"PASS: {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
