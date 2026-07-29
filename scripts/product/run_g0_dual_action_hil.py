#!/usr/bin/env python3
"""Run a metrics-only G0 chord-then-microphone hardware gate."""

from __future__ import annotations

import argparse
import getpass
import ipaddress
import json
import os
import re
import select
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import Any, Callable
from urllib.parse import urlsplit, urlunsplit


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
        is_local = ipaddress.ip_address(host).is_private
    except ValueError:
        is_local = host.lower().endswith(".local")
    if not is_local:
        raise ValueError("device URL must be a local HTTPS base URL")
    authority = host if parsed.port is None else f"{host}:{parsed.port}"
    return urlunsplit(("https", authority, "", "", ""))


def g0_config(enabled: bool, modifiers: int, usage: int) -> dict[str, Any]:
    if not 0 <= modifiers <= 15:
        raise ValueError("modifiers must be within 0..15")
    if not 4 <= usage <= 101:
        raise ValueError("usage must be within 4..101")
    return {
        "enabled": bool(enabled),
        "modifiers": modifiers,
        "usages": [usage],
    }


def _curl_config(pairing: str) -> Path:
    if not re.fullmatch(r"[0-9]{8}", pairing):
        raise ValueError("device PIN must be exactly 8 digits")
    descriptor, name = tempfile.mkstemp(prefix="cardputer-g0-", suffix=".curl")
    path = Path(name)
    try:
        os.fchmod(descriptor, 0o600)
        content = (
            "silent\n"
            "show-error\n"
            "insecure\n"
            "fail-with-body\n"
            'header = "Content-Type: application/json"\n'
            f'header = "X-Cardputer-Pairing: {pairing}"\n'
            "connect-timeout = 5\n"
            "max-time = 10\n"
        )
        os.write(descriptor, content.encode())
    finally:
        os.close(descriptor)
    return path


def curl_json(
    base_url: str,
    pairing: str,
    path: str,
    *,
    method: str = "GET",
    payload: dict[str, Any] | None = None,
    run: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> dict[str, Any]:
    config = _curl_config(pairing)
    arguments = [
        "/usr/bin/curl",
        "--config",
        str(config),
        "--request",
        method,
        "--url",
        f"{base_url}{path}",
    ]
    if payload is not None:
        arguments.extend(
            ["--data-binary", json.dumps(payload, separators=(",", ":"))]
        )
    try:
        completed = run(
            arguments,
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode != 0:
            raise RuntimeError(
                f"device request failed with curl exit {completed.returncode}"
            )
        value = json.loads(completed.stdout)
        if not isinstance(value, dict):
            raise RuntimeError("device returned a non-object response")
        return value
    finally:
        config.unlink(missing_ok=True)


def microphone_state(snapshot: dict[str, Any]) -> str:
    microphone = snapshot.get("microphone")
    if not isinstance(microphone, dict):
        return ""
    return str(microphone.get("state", "")).upper()


def _json_from_line(line: str) -> dict[str, Any] | None:
    start = line.find("{")
    if start < 0:
        return None
    try:
        value = json.loads(line[start:])
    except json.JSONDecodeError:
        return None
    return value if isinstance(value, dict) else None


def _hid_values(lines: list[str], key: str) -> list[int]:
    values = []
    for line in lines:
        value = _json_from_line(line)
        hid = value.get("hid") if value is not None else None
        if isinstance(hid, dict):
            values.append(int(hid.get(key, 0)))
    return values


def _task_stack_free_values(lines: list[str], name: str) -> list[int]:
    values = []
    for line in lines:
        value = _json_from_line(line)
        tasks = value.get("tasks") if value is not None else None
        if not isinstance(tasks, list):
            continue
        for task in tasks:
            if (
                isinstance(task, dict)
                and task.get("name") == name
                and "high_water_free_bytes" in task
            ):
                values.append(int(task["high_water_free_bytes"]))
    return values


def _reset_reason(lines: list[str]) -> tuple[int, str]:
    reasons = []
    for line in lines:
        match = re.search(r"rst:0x[0-9a-fA-F]+\s+\(([^)]+)\)", line)
        if match:
            reasons.append(match.group(1))
    return len(reasons), reasons[-1] if reasons else "none"


def build_report(
    lines: list[str],
    *,
    before_state: str,
    after_state: str,
    command_result: str,
    elapsed_ms: int,
) -> dict[str, Any]:
    queue_values = _hid_values(lines, "queue_failures")
    queue_delta = (
        max(0, queue_values[-1] - queue_values[0])
        if len(queue_values) >= 2
        else 0
    )
    boot_count, reset_reason = _reset_reason(lines)
    return {
        "microphone_before": before_state,
        "microphone_after": after_state,
        "microphone_transitioned": before_state != after_state,
        "command_result": command_result,
        "completed": any(
            "g0 dual action completed result=" in line for line in lines
        ),
        "elapsed_ms": int(elapsed_ms),
        "boot_count": boot_count,
        "reset_reason": reset_reason,
        "hid_queue_failure_delta": queue_delta,
    }


def validate_report(report: dict[str, Any], *, enabled: bool = True) -> None:
    if int(report["boot_count"]) != 0:
        raise ValueError("device reset during G0 HIL")
    if not report["microphone_transitioned"]:
        raise ValueError("microphone state did not transition")
    if int(report["hid_queue_failure_delta"]) != 0:
        raise ValueError("HID queue failure observed")
    if enabled:
        if report["command_result"] != "queued":
            raise ValueError("G0 dual action was not queued")
        if not report["completed"]:
            raise ValueError("G0 dual action did not complete")
    else:
        if report["command_result"] != "mic_only":
            raise ValueError("disabled G0 did not use Mic-only path")
        if report["completed"]:
            raise ValueError("disabled G0 unexpectedly ran dual action")


def build_stress_report(
    lines: list[str],
    *,
    expected_iterations: int,
    microphone_transition_count: int,
    elapsed_ms: int,
) -> dict[str, Any]:
    queue_values = _hid_values(lines, "queue_failures")
    queue_delta = (
        max(0, queue_values[-1] - queue_values[0])
        if len(queue_values) >= 2
        else 0
    )
    stack_values = _task_stack_free_values(lines, "g0-dual")
    boot_count, reset_reason = _reset_reason(lines)
    return {
        "expected_iterations": int(expected_iterations),
        "acknowledgement_count": sum(
            "HIL G0 CLICK " in line for line in lines
        ),
        "completion_count": sum(
            "g0 dual action completed result=" in line for line in lines
        ),
        "microphone_transition_count": int(
            microphone_transition_count
        ),
        "boot_count": boot_count,
        "reset_reason": reset_reason,
        "hid_queue_failure_delta": queue_delta,
        "g0_stack_min_free_bytes": (
            min(stack_values) if stack_values else 0
        ),
        "elapsed_ms": int(elapsed_ms),
    }


def validate_stress_report(
    report: dict[str, Any],
    *,
    expected_iterations: int,
    minimum_stack_free_bytes: int = 768,
) -> None:
    if int(report["boot_count"]) != 0:
        raise ValueError("device reset during repeated G0 HIL")
    if int(report["acknowledgement_count"]) != expected_iterations:
        raise ValueError("G0 acknowledgement count mismatch")
    if int(report["completion_count"]) != expected_iterations:
        raise ValueError("G0 completion count mismatch")
    if int(report["microphone_transition_count"]) != expected_iterations:
        raise ValueError("microphone transition count mismatch")
    if int(report["hid_queue_failure_delta"]) != 0:
        raise ValueError("HID queue failure observed")
    if int(report["g0_stack_min_free_bytes"]) < minimum_stack_free_bytes:
        raise ValueError("G0 task stack headroom below minimum")


class SerialMonitor:
    def __init__(self, descriptor: int) -> None:
        self._descriptor = descriptor
        self._stop = threading.Event()
        self._condition = threading.Condition()
        self._lines: list[str] = []
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

    def wait_for_any(
        self, patterns: tuple[str, ...], timeout: float
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

    def wait_for_line_count(
        self, pattern: str, count: int, timeout: float
    ) -> bool:
        deadline = time.monotonic() + timeout
        with self._condition:
            while True:
                if sum(pattern in line for line in self._lines) >= count:
                    return True
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return False
                self._condition.wait(remaining)

    def wait_for_metric_count(self, count: int, timeout: float) -> bool:
        deadline = time.monotonic() + timeout
        with self._condition:
            while True:
                if len(_hid_values(self._lines, "queue_failures")) >= count:
                    return True
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return False
                self._condition.wait(remaining)

    def wait_for_task_metric_count(
        self, name: str, count: int, timeout: float
    ) -> bool:
        deadline = time.monotonic() + timeout
        with self._condition:
            while True:
                if len(_task_stack_free_values(self._lines, name)) >= count:
                    return True
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return False
                self._condition.wait(remaining)

    @property
    def lines(self) -> list[str]:
        with self._condition:
            return list(self._lines)

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
                with self._condition:
                    self._lines.append(line)
                    if len(self._lines) > 2048:
                        del self._lines[:-2048]
                    self._condition.notify_all()


def _read_pin(path: Path | None) -> str:
    if path is None:
        value = getpass.getpass("Cardputer device PIN: ").strip()
    else:
        if path.stat().st_mode & 0o077:
            raise ValueError("PIN file must not be accessible by group/other")
        value = path.read_text().strip()
    if not re.fullmatch(r"[0-9]{8}", value):
        raise ValueError("device PIN must be exactly 8 digits")
    return value


def _is_stable_microphone_state(state: str) -> bool:
    return state == "READY" or state in {"LIVE16", "LIVE24"}


def _microphone_is_live(state: str) -> bool:
    return state in {"LIVE16", "LIVE24"}


def _wait_for_stable_microphone_state(
    base_url: str,
    pairing: str,
    timeout: float = 10,
    *,
    opposite_of: str | None = None,
) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        state = microphone_state(
            curl_json(base_url, pairing, "/api/v1/status")
        )
        if _is_stable_microphone_state(state) and (
            opposite_of is None
            or _microphone_is_live(state)
            != _microphone_is_live(opposite_of)
        ):
            return state
        time.sleep(0.1)
    if opposite_of is None:
        raise TimeoutError("microphone did not reach a stable state")
    raise TimeoutError("microphone did not reach the opposite stable state")


def _bounded_iterations(value: str) -> int:
    parsed = int(value)
    if not 1 <= parsed <= 100:
        raise argparse.ArgumentTypeError("iterations must be within 1..100")
    return parsed


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True, type=Path)
    parser.add_argument(
        "--device-url", required=True, type=validate_device_url
    )
    parser.add_argument("--pin-file", type=Path)
    parser.add_argument("--modifiers", type=int, default=4)
    parser.add_argument("--usage", type=int, default=25)
    parser.add_argument("--disabled", action="store_true")
    parser.add_argument(
        "--iterations", type=_bounded_iterations, default=1
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("build/hil/g0-dual-action.json"),
    )
    args = parser.parse_args(argv)
    if args.disabled and args.iterations != 1:
        parser.error("--disabled requires --iterations 1")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if not args.port.exists():
        raise SystemExit(f"serial port not found: {args.port}")
    try:
        pairing = _read_pin(args.pin_file)
        desired = g0_config(
            not args.disabled, args.modifiers, args.usage
        )
    except ValueError as error:
        raise SystemExit(str(error)) from error

    prior = curl_json(
        args.device_url, pairing, "/api/v1/settings/g0-chord"
    )
    descriptor = os.open(args.port, os.O_RDWR | os.O_NONBLOCK)
    monitor = SerialMonitor(descriptor)
    monitor.start()
    after_state = ""
    try:
        saved = curl_json(
            args.device_url,
            pairing,
            "/api/v1/settings/g0-chord",
            method="PUT",
            payload=desired,
        )
        if saved.get("saved") is not True:
            raise RuntimeError("device did not confirm G0 settings")
        if curl_json(
            args.device_url, pairing, "/api/v1/settings/g0-chord"
        ) != desired:
            raise RuntimeError("G0 settings readback mismatch")
        if not monitor.wait_for_metric_count(1, timeout=3):
            raise TimeoutError("runtime metrics unavailable")
        if not monitor.wait_for_task_metric_count(
            "g0-dual", 1, timeout=3
        ):
            raise TimeoutError("G0 task metrics unavailable")
        metric_count = len(
            _hid_values(monitor.lines, "queue_failures")
        )
        g0_metric_count = len(
            _task_stack_free_values(monitor.lines, "g0-dual")
        )
        started_at = time.monotonic()
        if args.disabled:
            before_state = _wait_for_stable_microphone_state(
                args.device_url, pairing
            )
            monitor.send(b"HIL G0 CLICK\n")
            if not monitor.wait_for_line_count(
                "HIL G0 CLICK ", 1, timeout=2
            ):
                raise TimeoutError(
                    "G0 HIL command acknowledgement timed out"
                )
            response = next(
                line
                for line in reversed(monitor.lines)
                if "HIL G0 CLICK " in line
            )
            command_result = response.rsplit(" ", 1)[-1].lower()
            after_state = _wait_for_stable_microphone_state(
                args.device_url, pairing, opposite_of=before_state
            )
            monitor.wait_for_metric_count(
                metric_count + 1, timeout=3
            )
            elapsed_ms = int(
                (time.monotonic() - started_at) * 1000
            )
            report = build_report(
                monitor.lines,
                before_state=before_state,
                after_state=after_state,
                command_result=command_result,
                elapsed_ms=elapsed_ms,
            )
            validate_report(report, enabled=False)
        else:
            transition_count = 0
            for iteration in range(1, args.iterations + 1):
                before_state = _wait_for_stable_microphone_state(
                    args.device_url, pairing
                )
                monitor.send(b"HIL G0 CLICK\n")
                if not monitor.wait_for_line_count(
                    "HIL G0 CLICK ", iteration, timeout=2
                ):
                    raise TimeoutError(
                        "G0 HIL command acknowledgement timed out"
                    )
                acknowledgement = next(
                    line
                    for line in reversed(monitor.lines)
                    if "HIL G0 CLICK " in line
                )
                if not acknowledgement.endswith("QUEUED"):
                    raise RuntimeError(
                        "G0 dual action was not queued"
                    )
                if not monitor.wait_for_line_count(
                    "g0 dual action completed result=",
                    iteration,
                    timeout=2,
                ):
                    raise TimeoutError(
                        "G0 dual action completion timed out"
                    )
                after_state = _wait_for_stable_microphone_state(
                    args.device_url,
                    pairing,
                    opposite_of=before_state,
                )
                transition_count += 1
            if not monitor.wait_for_metric_count(
                metric_count + 1, timeout=3
            ):
                raise TimeoutError("post-action runtime metrics unavailable")
            if not monitor.wait_for_task_metric_count(
                "g0-dual", g0_metric_count + 1, timeout=3
            ):
                raise TimeoutError("post-action G0 metrics unavailable")
            elapsed_ms = int(
                (time.monotonic() - started_at) * 1000
            )
            report = build_stress_report(
                monitor.lines,
                expected_iterations=args.iterations,
                microphone_transition_count=transition_count,
                elapsed_ms=elapsed_ms,
            )
            validate_stress_report(
                report,
                expected_iterations=args.iterations,
            )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n"
        )
        print(f"PASS: {args.output}")
        return 0
    finally:
        if after_state in {"STARTING", "LIVE24", "LIVE16", "STOPPING"}:
            try:
                monitor.send(b"HIL MIC STOP\n")
                monitor.wait_for_any(
                    (
                        "HIL MIC STOP ACCEPTED",
                        "HIL MIC STOP NOOP",
                        "HIL MIC STOP REJECTED",
                    ),
                    timeout=2,
                )
            except Exception:
                pass
        try:
            curl_json(
                args.device_url,
                pairing,
                "/api/v1/settings/g0-chord",
                method="PUT",
                payload=prior,
            )
        finally:
            monitor.stop()
            os.close(descriptor)


if __name__ == "__main__":
    sys.exit(main())
