#!/usr/bin/env python3
"""Verify Cardputer reboot recovery without restarting the macOS Agent."""

from __future__ import annotations

import argparse
import ipaddress
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterator, Sequence
from urllib.parse import urlsplit, urlunsplit


LABEL = "com.lynx.cardputer-companion"
DEFAULT_CONFIG = (
    Path.home()
    / "Library/Application Support/CardputerCodexCompanion/config.json"
)
DEFAULT_OUTPUT = Path("build/hil/gatt-reboot-recovery.json")
PID_PATTERN = re.compile(r"^\s*pid\s*=\s*(\d+)\s*$", re.MULTILINE)


def parse_launchctl_pid(output: str) -> int:
    match = PID_PATTERN.search(output)
    if match is None:
        raise ValueError("macOS Agent PID is unavailable")
    return int(match.group(1))


def ready_snapshot(snapshot: dict[str, Any]) -> bool:
    microphone = snapshot.get("microphone")
    return (
        str(snapshot.get("ble", "")).upper() == "OK"
        and str(snapshot.get("wifi", "")).upper() == "OK"
        and str(snapshot.get("companion", "")).upper() == "OK"
        and isinstance(microphone, dict)
        and str(microphone.get("state", "")).upper() == "READY"
        and str(microphone.get("last_error", "")).upper() == "NONE"
    )


def sanitized_cycle(
    cycle: int,
    agent_pid: int,
    ready_seconds: float,
    microphone_state: str,
) -> dict[str, int | float | str]:
    return {
        "cycle": int(cycle),
        "agent_pid": int(agent_pid),
        "ready_seconds": round(float(ready_seconds), 3),
        "microphone_state": str(microphone_state),
    }


def status_url(device: str) -> str:
    parsed = urlsplit(device)
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
        raise ValueError("device must be a local HTTPS base URL")
    try:
        local_host = ipaddress.ip_address(host).is_private
    except ValueError:
        local_host = host.lower().endswith(".local")
    if not local_host:
        raise ValueError("device must be a local HTTPS base URL")
    authority = parsed.netloc
    return urlunsplit(("https", authority, "/api/v1/status", "", ""))


def load_config(path: Path) -> tuple[str, str]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError("Agent config must contain a JSON object")
    pairing = value.get("pairing")
    if (
        not isinstance(pairing, str)
        or len(pairing) != 8
        or not pairing.isascii()
        or not pairing.isdigit()
    ):
        raise ValueError("Agent config has an invalid device PIN")
    return status_url(str(value.get("device", ""))), pairing


def _curl_quote(value: str) -> str:
    escaped = (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\r", "\\r")
        .replace("\n", "\\n")
    )
    return f'"{escaped}"'


@contextmanager
def private_curl_config(url: str, pairing: str) -> Iterator[Path]:
    descriptor, raw_path = tempfile.mkstemp(prefix="cardputer-hil-", suffix=".conf")
    path = Path(raw_path)
    try:
        os.fchmod(descriptor, 0o600)
        content = "\n".join(
            (
                "silent",
                "show-error",
                "insecure",
                "fail-with-body",
                "connect-timeout = 5",
                "max-time = 10",
                f"url = {_curl_quote(url)}",
                "header = "
                + _curl_quote(f"X-Cardputer-Pairing: {pairing}"),
                "",
            )
        ).encode()
        os.write(descriptor, content)
        os.close(descriptor)
        descriptor = -1
        yield path
    finally:
        if descriptor >= 0:
            os.close(descriptor)
        try:
            path.unlink()
        except FileNotFoundError:
            pass


def query_status(curl_config: Path) -> dict[str, Any]:
    result = subprocess.run(
        ["/usr/bin/curl", "--config", str(curl_config)],
        check=True,
        capture_output=True,
        text=True,
    )
    value = json.loads(result.stdout)
    if not isinstance(value, dict):
        raise ValueError("device status is not a JSON object")
    return value


def current_agent_pid() -> int:
    result = subprocess.run(
        [
            "/bin/launchctl",
            "print",
            f"gui/{os.getuid()}/{LABEL}",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    return parse_launchctl_pid(result.stdout)


def find_idf_python(repo_root: Path) -> Path:
    candidates = sorted(
        (repo_root / ".tools/espressif/python_env").glob("*/bin/python")
    )
    if not candidates:
        raise FileNotFoundError(
            "ESP-IDF Python not found; bootstrap the project toolchain or "
            "pass --idf-python"
        )
    return candidates[-1]


def reset_device(idf_python: Path, port: str) -> None:
    subprocess.run(
        [
            str(idf_python),
            "-m",
            "esptool",
            "--chip",
            "esp32s3",
            "--port",
            port,
            "--after",
            "hard_reset",
            "flash_id",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def wait_until_ready(
    curl_config: Path,
    expected_pid: int,
    timeout: float,
) -> tuple[dict[str, Any], float]:
    started = time.monotonic()
    deadline = started + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        if current_agent_pid() != expected_pid:
            raise RuntimeError("macOS Agent PID changed during reboot recovery")
        try:
            snapshot = query_status(curl_config)
            if ready_snapshot(snapshot):
                return snapshot, time.monotonic() - started
        except (
            json.JSONDecodeError,
            subprocess.CalledProcessError,
            ValueError,
        ) as error:
            last_error = error
        time.sleep(0.25)
    message = "Cardputer did not return to MIC READY within the timeout"
    if last_error is not None:
        raise TimeoutError(message) from None
    raise TimeoutError(message)


def write_report(output: Path, cycles: list[dict[str, Any]]) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_text(
        json.dumps(cycles, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(output)


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--cycles", type=int, default=5)
    parser.add_argument("--ready-timeout", type=float, default=15)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--idf-python", type=Path)
    args = parser.parse_args(argv)
    if args.cycles < 1:
        parser.error("--cycles must be at least 1")
    if args.ready_timeout <= 0:
        parser.error("--ready-timeout must be positive")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    repo_root = Path(__file__).resolve().parents[2]
    idf_python = args.idf_python or find_idf_python(repo_root)
    if not idf_python.is_file() or not os.access(idf_python, os.X_OK):
        raise FileNotFoundError("ESP-IDF Python is not executable")

    url, pairing = load_config(args.config.expanduser())
    initial_pid = current_agent_pid()
    results: list[dict[str, Any]] = []
    with private_curl_config(url, pairing) as curl_config:
        snapshot, ready_seconds = wait_until_ready(
            curl_config,
            initial_pid,
            args.ready_timeout,
        )
        for cycle in range(1, args.cycles + 1):
            if current_agent_pid() != initial_pid:
                raise RuntimeError("macOS Agent PID changed before reset")
            reset_device(idf_python, args.port)
            snapshot, ready_seconds = wait_until_ready(
                curl_config,
                initial_pid,
                args.ready_timeout,
            )
            microphone = snapshot["microphone"]
            results.append(
                sanitized_cycle(
                    cycle,
                    initial_pid,
                    ready_seconds,
                    str(microphone["state"]),
                )
            )
            print(
                f"cycle {cycle}: MIC READY in {ready_seconds:.3f}s "
                f"(Agent PID {initial_pid})"
            )
    write_report(args.output, results)
    print(f"wrote metrics-only report: {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, TimeoutError) as error:
        print(f"GATT reboot recovery HIL failed: {error}", file=sys.stderr)
        raise SystemExit(1)
