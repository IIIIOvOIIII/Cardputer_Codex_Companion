import importlib.util
import json
import socket
import subprocess
import threading
from pathlib import Path

import pytest


SCRIPT = (
    Path(__file__).resolve().parents[3]
    / "scripts"
    / "product"
    / "run_audio_feasibility_hil.py"
)


def load_script():
    spec = importlib.util.spec_from_file_location("audio_hil", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def valid_report():
    return {
        "duration_seconds": 600,
        "captured_frames": 59_900,
        "received_frames": 59_850,
        "source_overruns": 10,
        "transport_drops": 20,
        "sequence_gaps": 20,
        "max_gap_ms": 30,
        "sample_rate_hz": 24_000,
        "ble_reconnects": 0,
        "signal_peak": 12_000,
        "signal_rms": 2_500.0,
        "nonzero_sample_percent": 82.5,
        "hid_generated": 1_000,
        "hid_queued": 1_000,
        "hid_queue_failures": 0,
        "hid_p95_us": 15_000,
        "steady_free_internal": 70_000,
        "steady_largest_internal": 35_000,
        "tls_burst_free_internal": 45_000,
        "allocation_failures": 0,
        "stack_passed": True,
    }


def resource_sample(
    scenario: str,
    free: int,
    largest: int,
    *,
    counter: int = 100,
):
    return {
        "scenario": scenario,
        "free_internal_heap": free,
        "largest_internal_block": largest,
        "allocation_failures": 0,
        "audio": {
            "captured_frames": counter,
            "source_overruns": counter // 10,
            "transport_drops": counter // 20,
        },
        "hid": {
            "generated": counter * 10,
            "queued": counter * 10,
            "queue_failures": 0,
            "p95_upper_bound_us": 1_000,
        },
        "tasks": [
            {
                "name": "https",
                "configured": 7_168,
                "high_water_free_bytes": 3_000,
            }
        ],
    }


def test_report_contract_contains_metrics_only():
    module = load_script()
    report = valid_report()
    module.validate_report(report, expected_duration=600)
    required = {
        "duration_seconds",
        "captured_frames",
        "received_frames",
        "source_overruns",
        "transport_drops",
        "sequence_gaps",
        "max_gap_ms",
        "sample_rate_hz",
        "signal_peak",
        "signal_rms",
        "nonzero_sample_percent",
    }
    assert set(report) >= required
    assert "audio" not in report
    assert "pcm" not in json.dumps(report).lower()
    assert "adpcm" not in json.dumps(report).lower()


@pytest.mark.parametrize("duration", [9, 1801])
def test_duration_is_bounded(duration):
    module = load_script()
    with pytest.raises(ValueError, match="10..1800"):
        module.validate_duration(duration)


@pytest.mark.parametrize(
    ("url", "expected"),
    [
        (
            "https://192.168.1.195",
            "https://192.168.1.195/api/v1/status",
        ),
        (
            "https://cardputer.local/",
            "https://cardputer.local/api/v1/status",
        ),
    ],
)
def test_device_url_is_local_https_status_endpoint(url, expected):
    module = load_script()
    assert module.validate_device_url(url) == expected


@pytest.mark.parametrize(
    "url",
    [
        "http://192.168.1.195",
        "https://8.8.8.8",
        "https://user:password@192.168.1.195",
        "https://192.168.1.195/config",
    ],
)
def test_device_url_rejects_nonlocal_or_ambiguous_targets(url):
    module = load_script()
    with pytest.raises(ValueError, match="local HTTPS"):
        module.validate_device_url(url)


def test_tls_probe_schedule_preserves_steady_window_and_repeats():
    module = load_script()
    schedule = module.tls_probe_schedule(600)
    assert schedule[0] == 8
    assert schedule[-1] < 600
    assert max(right - left for left, right in zip(schedule, schedule[1:])) <= 15


def test_tls_probe_keeps_resource_sampling_window_active_after_curl():
    source = SCRIPT.read_text(encoding="utf-8")

    wait = source.index("stop.wait(TLS_RESOURCE_SAMPLE_SETTLE_SECONDS)")
    clear = source.index("active.clear()", wait)
    assert module_value("TLS_RESOURCE_SAMPLE_SETTLE_SECONDS", source) >= 1.0
    assert wait < clear


def module_value(name, source):
    assignment = next(
        line for line in source.splitlines() if line.startswith(f"{name} = ")
    )
    return float(assignment.split("=", 1)[1].strip())


def test_cli_parses_duration_as_an_integer(tmp_path):
    module = load_script()
    args = module.parse_args(
        [
            "--port",
            "/dev/null",
            "--companion",
            "/usr/bin/true",
            "--device-url",
            "https://192.168.1.195",
            "--duration",
            "600",
            "--output",
            str(tmp_path / "report.json"),
        ]
    )
    assert args.duration == 600


def test_gate_rejects_loss_gap_reconnect_and_resource_regressions():
    module = load_script()
    for key, value in [
        ("transport_drops", 1_000),
        ("max_gap_ms", 151),
        ("ble_reconnects", 1),
        ("hid_generated", 999),
        ("hid_queued", 999),
        ("hid_queue_failures", 1),
        ("hid_p95_us", 20_001),
        ("steady_free_internal", 65_535),
        ("steady_largest_internal", 32_767),
        ("tls_burst_free_internal", 40_959),
        ("allocation_failures", 1),
        ("stack_passed", False),
        ("signal_peak", 0),
        ("signal_rms", 0),
        ("nonzero_sample_percent", 0),
    ]:
        report = valid_report()
        report[key] = value
        with pytest.raises(ValueError):
            module.validate_report(report, expected_duration=600)


def test_gate_rejects_constant_low_level_pcm_false_positive():
    module = load_script()
    report = valid_report()
    report["signal_peak"] = 8
    report["signal_rms"] = 8
    report["nonzero_sample_percent"] = 100

    with pytest.raises(ValueError, match="signal"):
        module.validate_report(report, expected_duration=600)


@pytest.mark.parametrize("key", ["captured_frames", "received_frames"])
def test_gate_rejects_truncated_audio_stream_even_without_reported_loss(key):
    module = load_script()
    report = valid_report()
    report["source_overruns"] = 0
    report["transport_drops"] = 0
    report["sequence_gaps"] = 0
    report[key] = 1_000
    with pytest.raises(ValueError, match="continuous audio"):
        module.validate_report(report, expected_duration=600)


def test_gate_uses_rate_specific_frame_duration_for_16khz():
    module = load_script()
    report = valid_report()
    report["duration_seconds"] = 60
    report["sample_rate_hz"] = 16_000
    report["captured_frames"] = 2_200
    report["received_frames"] = 2_200
    report["source_overruns"] = 0
    report["transport_drops"] = 0
    report["sequence_gaps"] = 0
    module.validate_report(report, expected_duration=60)


def test_failed_gate_still_persists_the_merged_report(tmp_path):
    module = load_script()
    report = valid_report()
    report["max_gap_ms"] = 151
    output = tmp_path / "failed-report.json"

    with pytest.raises(ValueError, match="audio gap"):
        module.persist_and_validate_report(
            report,
            output,
            expected_duration=600,
        )

    assert json.loads(output.read_text()) == report


def test_merge_separates_steady_and_tls_resource_windows():
    module = load_script()
    report = module.merge_metrics(
        {},
        [
            resource_sample("steady", 70_000, 33_000, counter=0),
            resource_sample("tls_burst", 45_000, 20_000),
        ],
    )
    assert report["steady_free_internal"] == 70_000
    assert report["steady_largest_internal"] == 33_000
    assert report["tls_burst_free_internal"] == 45_000
    assert report["hid_generated"] == 1_000
    assert report["hid_queued"] == 1_000
    assert report["hid_queue_failures"] == 0


def test_merge_uses_run_deltas_for_cumulative_firmware_counters():
    module = load_script()
    baseline = resource_sample("steady", 70_000, 33_000, counter=400)
    final = resource_sample("tls_burst", 45_000, 20_000, counter=500)
    baseline["hid"]["queue_failures"] = 8
    final["hid"]["queue_failures"] = 10

    report = module.merge_metrics({}, [baseline, final])

    assert report["captured_frames"] == 100
    assert report["source_overruns"] == 10
    assert report["transport_drops"] == 5
    assert report["hid_generated"] == 1_000
    assert report["hid_queued"] == 1_000
    assert report["hid_queue_failures"] == 2


@pytest.mark.parametrize("missing", ["steady", "tls_burst"])
def test_merge_requires_both_resource_windows(missing):
    module = load_script()
    scenario = "tls_burst" if missing == "steady" else "steady"
    with pytest.raises(ValueError, match=missing):
        module.merge_metrics(
            {},
            [resource_sample(scenario, 70_000, 33_000)],
        )


def test_serial_monitor_uses_one_duplex_descriptor():
    module = load_script()
    monitor_side, device_side = socket.socketpair()
    samples = []
    monitor = module.SerialMonitor(monitor_side.fileno(), samples)
    try:
        monitor.start()
        device_side.sendall(b"BLE audio sink ready=1\n")
        assert monitor.wait_for("BLE audio sink ready=1", timeout=1)
        assert "BLE audio sink ready=1" in monitor.lines
        monitor.send(b"HIL MIC START\n")
        assert device_side.recv(64) == b"HIL MIC START\n"
    finally:
        monitor.stop()
        monitor_side.close()
        device_side.close()


def test_serial_monitor_marks_samples_seen_during_tls_as_tls_burst():
    module = load_script()
    monitor_side, device_side = socket.socketpair()
    samples = []
    tls_active = threading.Event()
    monitor = module.SerialMonitor(
        monitor_side.fileno(), samples, tls_active=tls_active
    )
    try:
        monitor.start()
        tls_active.set()
        device_side.sendall(
            b'{"scenario":"steady","audio":{"captured_frames":0}}\n'
        )
        assert monitor.wait_for_resource_sample(timeout=1)
        assert samples[-1]["scenario"] == "tls_burst"
    finally:
        monitor.stop()
        monitor_side.close()
        device_side.close()


def test_serial_monitor_reclassifies_sample_preceding_tls_handshake():
    module = load_script()
    monitor_side, device_side = socket.socketpair()
    samples = []
    monitor = module.SerialMonitor(monitor_side.fileno(), samples)
    try:
        monitor.start()
        device_side.sendall(
            b'{"scenario":"steady","audio":{"captured_frames":0}}\n'
        )
        assert monitor.wait_for_resource_sample(timeout=1)
        device_side.sendall(
            b"I (1234) esp_https_server: performing session handshake\n"
        )
        assert monitor.wait_for("performing session handshake", timeout=1)
        assert samples[-1]["scenario"] == "tls_burst"
    finally:
        monitor.stop()
        monitor_side.close()
        device_side.close()


def test_serial_monitor_marks_sample_following_tls_handshake():
    module = load_script()
    monitor_side, device_side = socket.socketpair()
    samples = []
    monitor = module.SerialMonitor(monitor_side.fileno(), samples)
    try:
        monitor.start()
        device_side.sendall(
            b"I (1234) esp_https_server: performing session handshake\n"
        )
        assert monitor.wait_for("performing session handshake", timeout=1)
        device_side.sendall(
            b'{"scenario":"steady","audio":{"captured_frames":0}}\n'
        )
        assert monitor.wait_for_resource_sample(timeout=1)
        assert samples[-1]["scenario"] == "tls_burst"
    finally:
        monitor.stop()
        monitor_side.close()
        device_side.close()


class FakeSerialMonitor:
    def __init__(
        self,
        fail_stop=False,
        start_results=None,
        hid_start_results=None,
    ):
        self.commands = []
        self.fail_stop = fail_stop
        self.baseline_waits = 0
        self.cleanup_waits = 0
        self.start_results = list(
            start_results or ["HIL MIC START ACCEPTED"]
        )
        self.hid_start_results = list(
            hid_start_results or ["HIL HID START ACCEPTED"]
        )

    def wait_for(self, pattern, timeout):
        if pattern == "BLE audio sink ready=1":
            assert timeout == 30
            return True
        raise AssertionError(pattern)

    def wait_for_resource_sample(self, timeout):
        assert timeout == 2
        self.baseline_waits += 1
        return True

    def clear_lines(self):
        pass

    def wait_for_any(self, patterns, timeout):
        if patterns == (
            "HIL HID STOP STOPPED",
            "HIL HID STOP NOOP",
        ):
            assert timeout == 1
            self.cleanup_waits += 1
            return "HIL HID STOP STOPPED"
        if patterns == (
            "HIL MIC STOP ACCEPTED",
            "HIL MIC STOP REJECTED",
            "HIL MIC STOP NOOP",
        ):
            assert timeout == 1
            self.cleanup_waits += 1
            return "HIL MIC STOP NOOP"
        if patterns == (
            "HIL HID START ACCEPTED",
            "HIL HID START REJECTED",
        ):
            assert timeout == 1
            if not self.hid_start_results:
                return None
            return self.hid_start_results.pop(0)
        assert patterns == (
            "HIL MIC START ACCEPTED",
            "HIL MIC START REJECTED",
        )
        assert timeout == 1
        if not self.start_results:
            return None
        return self.start_results.pop(0)

    def send(self, command):
        self.commands.append(command)
        if self.fail_stop and command == b"HIL MIC STOP\n":
            raise OSError("stop write failed")


class FakeProcess:
    def __init__(self, return_code=0, times_out=False):
        self.return_code = return_code
        self.times_out = times_out
        self.terminated = False

    def wait(self, timeout):
        if self.times_out and not self.terminated:
            raise subprocess.TimeoutExpired("audio-probe", timeout)
        return self.return_code

    def terminate(self):
        self.terminated = True


def test_probe_process_always_stops_microphone():
    module = load_script()
    monitor = FakeSerialMonitor()
    result = module.run_probe_process(
        FakeProcess(),
        monitor,
        timeout=40,
    )
    assert result == 0
    assert monitor.baseline_waits == 1
    assert monitor.commands == [
        b"HIL MIC START\n",
        b"HIL HID START\n",
        b"HIL HID STOP\n",
        b"HIL MIC STOP\n",
    ]
    assert monitor.cleanup_waits == 2


def test_probe_timeout_still_stops_microphone():
    module = load_script()
    monitor = FakeSerialMonitor()
    process = FakeProcess(times_out=True)
    with pytest.raises(TimeoutError, match="timed out"):
        module.run_probe_process(process, monitor, timeout=40)
    assert process.terminated
    assert monitor.commands == [
        b"HIL MIC START\n",
        b"HIL HID START\n",
        b"HIL HID STOP\n",
        b"HIL MIC STOP\n",
    ]


def test_probe_retries_start_until_firmware_accepts_readiness():
    module = load_script()
    monitor = FakeSerialMonitor(
        start_results=[
            "HIL MIC START REJECTED",
            "HIL MIC START ACCEPTED",
        ]
    )
    assert (
        module.run_probe_process(FakeProcess(), monitor, timeout=40) == 0
    )
    assert monitor.commands == [
        b"HIL MIC START\n",
        b"HIL MIC START\n",
        b"HIL HID START\n",
        b"HIL HID STOP\n",
        b"HIL MIC STOP\n",
    ]


def test_probe_retries_hid_until_keyboard_subscription_is_ready(monkeypatch):
    module = load_script()
    monkeypatch.setattr(module.time, "sleep", lambda _: None)
    monitor = FakeSerialMonitor(
        hid_start_results=[
            "HIL HID START REJECTED",
            "HIL HID START ACCEPTED",
        ]
    )

    assert module.run_probe_process(FakeProcess(), monitor, timeout=40) == 0
    assert monitor.commands == [
        b"HIL MIC START\n",
        b"HIL HID START\n",
        b"HIL HID START\n",
        b"HIL HID STOP\n",
        b"HIL MIC STOP\n",
    ]


def test_probe_success_fails_when_stop_cannot_be_sent():
    module = load_script()
    monitor = FakeSerialMonitor(fail_stop=True)
    with pytest.raises(OSError, match="stop write failed"):
        module.run_probe_process(FakeProcess(), monitor, timeout=40)
