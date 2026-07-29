import importlib.util
import json
import stat
import subprocess
from pathlib import Path

import pytest


SCRIPT = (
    Path(__file__).resolve().parents[3]
    / "scripts"
    / "product"
    / "run_g0_dual_action_hil.py"
)


def load_script():
    spec = importlib.util.spec_from_file_location("g0_hil", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_device_url_is_bounded_to_local_https():
    module = load_script()
    assert (
        module.validate_device_url("https://192.168.1.195")
        == "https://192.168.1.195"
    )
    assert (
        module.validate_device_url("https://cardputer.local/")
        == "https://cardputer.local"
    )
    for invalid in (
        "http://192.168.1.195",
        "https://8.8.8.8",
        "https://user:secret@192.168.1.195",
        "https://192.168.1.195/path",
    ):
        with pytest.raises(ValueError, match="local HTTPS"):
            module.validate_device_url(invalid)


def test_g0_config_is_single_key_and_retains_disabled_chord():
    module = load_script()
    assert module.g0_config(True, 4, 25) == {
        "enabled": True,
        "modifiers": 4,
        "usages": [25],
    }
    assert module.g0_config(False, 9, 40) == {
        "enabled": False,
        "modifiers": 9,
        "usages": [40],
    }
    with pytest.raises(ValueError):
        module.g0_config(True, 16, 25)
    with pytest.raises(ValueError):
        module.g0_config(True, 4, 0)


def test_curl_config_hides_pin_from_process_arguments_and_is_mode_0600():
    module = load_script()
    pairing = "12345678"
    captured_path = None

    def fake_run(arguments, **kwargs):
        nonlocal captured_path
        assert pairing not in " ".join(str(value) for value in arguments)
        captured_path = Path(
            arguments[arguments.index("--config") + 1]
        )
        assert stat.S_IMODE(captured_path.stat().st_mode) == 0o600
        assert pairing in captured_path.read_text()
        return subprocess.CompletedProcess(
            arguments,
            0,
            stdout='{"saved":true}',
            stderr="",
        )

    assert module.curl_json(
        "https://192.168.1.195",
        pairing,
        "/api/v1/settings/g0-chord",
        method="PUT",
        payload={"enabled": True, "modifiers": 4, "usages": [25]},
        run=fake_run,
    ) == {"saved": True}
    assert captured_path is not None
    assert not captured_path.exists()


def test_report_records_only_metrics_and_detects_reset_or_queue_failure():
    module = load_script()
    lines = [
        'I product: g0 dual action queued',
        '{"hid":{"queue_failures":7}}',
        'I product: g0 dual action completed result=1',
        '{"hid":{"queue_failures":7}}',
    ]
    report = module.build_report(
        lines,
        before_state="READY",
        after_state="STARTING",
        command_result="queued",
        elapsed_ms=43,
    )
    assert report == {
        "microphone_before": "READY",
        "microphone_after": "STARTING",
        "microphone_transitioned": True,
        "command_result": "queued",
        "completed": True,
        "elapsed_ms": 43,
        "boot_count": 0,
        "reset_reason": "none",
        "hid_queue_failure_delta": 0,
    }
    assert "pin" not in json.dumps(report).lower()

    failed = module.build_report(
        [
            '{"hid":{"queue_failures":2}}',
            'rst:0xc (RTC_SW_CPU_RST),boot:0x8',
            '{"hid":{"queue_failures":3}}',
        ],
        before_state="READY",
        after_state="READY",
        command_result="fallback",
        elapsed_ms=1000,
    )
    assert failed["boot_count"] == 1
    assert failed["reset_reason"] == "RTC_SW_CPU_RST"
    assert failed["hid_queue_failure_delta"] == 1
    with pytest.raises(ValueError, match="reset"):
        module.validate_report(failed)


def test_report_gate_requires_mic_transition_and_completion():
    module = load_script()
    base = {
        "microphone_before": "READY",
        "microphone_after": "STARTING",
        "microphone_transitioned": True,
        "command_result": "queued",
        "completed": True,
        "elapsed_ms": 30,
        "boot_count": 0,
        "reset_reason": "none",
        "hid_queue_failure_delta": 0,
    }
    module.validate_report(base)
    for key, value in (
        ("microphone_transitioned", False),
        ("completed", False),
        ("command_result", "fallback"),
        ("hid_queue_failure_delta", 1),
    ):
        report = dict(base)
        report[key] = value
        with pytest.raises(ValueError):
            module.validate_report(report)


def test_disabled_report_requires_mic_only_and_no_hid_activity():
    module = load_script()
    report = {
        "microphone_before": "READY",
        "microphone_after": "STARTING",
        "microphone_transitioned": True,
        "command_result": "mic_only",
        "completed": False,
        "elapsed_ms": 30,
        "boot_count": 0,
        "reset_reason": "none",
        "hid_queue_failure_delta": 0,
    }
    module.validate_report(report, enabled=False)
    for key, value in (
        ("command_result", "queued"),
        ("completed", True),
    ):
        invalid = dict(report)
        invalid[key] = value
        with pytest.raises(ValueError):
            module.validate_report(invalid, enabled=False)
