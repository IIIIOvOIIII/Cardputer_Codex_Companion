import importlib.util
import json
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
        "hid_p95_us": 15_000,
        "steady_free_internal": 70_000,
        "steady_largest_internal": 35_000,
        "tls_burst_free_internal": 45_000,
        "allocation_failures": 0,
        "stack_passed": True,
    }


def resource_sample(scenario: str, free: int, largest: int):
    return {
        "scenario": scenario,
        "free_internal_heap": free,
        "largest_internal_block": largest,
        "allocation_failures": 0,
        "audio": {
            "captured_frames": 100,
            "source_overruns": 0,
            "transport_drops": 0,
        },
        "hid": {"p95_upper_bound_us": 1_000},
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


def test_gate_rejects_loss_gap_reconnect_and_resource_regressions():
    module = load_script()
    for key, value in [
        ("transport_drops", 1_000),
        ("max_gap_ms", 151),
        ("ble_reconnects", 1),
        ("hid_p95_us", 20_001),
        ("steady_free_internal", 65_535),
        ("steady_largest_internal", 32_767),
        ("tls_burst_free_internal", 40_959),
        ("allocation_failures", 1),
        ("stack_passed", False),
    ]:
        report = valid_report()
        report[key] = value
        with pytest.raises(ValueError):
            module.validate_report(report, expected_duration=600)


def test_merge_separates_steady_and_tls_resource_windows():
    module = load_script()
    report = module.merge_metrics(
        {},
        [
            resource_sample("steady", 70_000, 33_000),
            resource_sample("tls_burst", 45_000, 20_000),
        ],
    )
    assert report["steady_free_internal"] == 70_000
    assert report["steady_largest_internal"] == 33_000
    assert report["tls_burst_free_internal"] == 45_000


@pytest.mark.parametrize("missing", ["steady", "tls_burst"])
def test_merge_requires_both_resource_windows(missing):
    module = load_script()
    scenario = "tls_burst" if missing == "steady" else "steady"
    with pytest.raises(ValueError, match=missing):
        module.merge_metrics(
            {},
            [resource_sample(scenario, 70_000, 33_000)],
        )
