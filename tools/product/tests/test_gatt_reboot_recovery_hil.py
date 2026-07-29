import stat
from pathlib import Path

from scripts.product.run_gatt_reboot_recovery_hil import (
    parse_launchctl_pid,
    private_curl_config,
    ready_snapshot,
    sanitized_cycle,
)


ROOT = Path(__file__).resolve().parents[3]


def test_parse_launchctl_pid():
    assert parse_launchctl_pid("\n\tpid = 48123\n") == 48123


def test_ready_snapshot_requires_all_links():
    assert ready_snapshot(
        {
            "ble": "OK",
            "wifi": "OK",
            "companion": "OK",
            "microphone": {
                "state": "READY",
                "last_error": "NONE",
            },
        }
    )
    assert not ready_snapshot(
        {
            "ble": "OK",
            "wifi": "OK",
            "companion": "OK",
            "microphone": {
                "state": "UNAVAILABLE",
                "last_error": "NONE",
            },
        }
    )


def test_cycle_report_has_no_credentials():
    report = sanitized_cycle(1, 48123, 2.4, "READY")
    assert set(report) == {
        "cycle",
        "agent_pid",
        "ready_seconds",
        "microphone_state",
    }


def test_status_probe_tolerates_slow_lan_without_exposing_pin():
    with private_curl_config(
        "https://192.168.1.195/api/v1/status",
        "12345678",
    ) as config:
        content = config.read_text()
        assert "connect-timeout = 5" in content
        assert "max-time = 10" in content
        assert stat.S_IMODE(config.stat().st_mode) == 0o600
        assert "12345678" in content


def test_hil_retries_transient_status_failure_for_baseline_and_cycles():
    source = (
        ROOT / "scripts/product/run_gatt_reboot_recovery_hil.py"
    ).read_text()
    assert source.count(
        "snapshot, ready_seconds = wait_until_ready("
    ) == 2


def test_hid_readiness_transition_does_not_reset_audio_subscription():
    source = (
        ROOT / "firmware/main/product/product_controller.cpp"
    ).read_text()
    hid_state = source.split("void ble_connection_changed", 1)[1].split(
        "void ble_disconnected", 1
    )[0]
    gap_disconnect = source.split("void ble_disconnected", 1)[1].split(
        "void wifi_status_changed", 1
    )[0]

    assert "g_audio_data_ready.store(false)" not in hid_state
    assert "g_audio_sink_declared.store(false)" not in hid_state
    assert "MicrophoneRuntimeEvent::sink_lost" not in hid_state
    assert "g_audio_data_ready.store(false)" in gap_disconnect
    assert "g_audio_sink_declared.store(false)" in gap_disconnect
    assert "MicrophoneRuntimeEvent::sink_lost" in gap_disconnect


def test_late_gap_connect_event_does_not_reset_live_audio_subscription():
    source = (
        ROOT / "firmware/main/probe/ble_services.cpp"
    ).read_text()
    connect = source.split("case BLE_GAP_EVENT_CONNECT:", 1)[1].split(
        "case BLE_GAP_EVENT_DISCONNECT:", 1
    )[0]
    disconnect = source.split(
        "case BLE_GAP_EVENT_DISCONNECT:", 1
    )[1].split("case BLE_GAP_EVENT_ADV_COMPLETE:", 1)[0]

    assert "reset_audio_link_state()" not in connect
    assert "reset_audio_link_state()" in disconnect
