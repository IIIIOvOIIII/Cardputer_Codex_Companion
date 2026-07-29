from scripts.product.run_gatt_reboot_recovery_hil import (
    parse_launchctl_pid,
    ready_snapshot,
    sanitized_cycle,
)


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
