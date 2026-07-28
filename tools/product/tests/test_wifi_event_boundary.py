from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
WIFI_MANAGER = ROOT / "firmware/main/product/wifi_manager.cpp"


def _function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


def test_system_event_callback_only_queues_fixed_events():
    source = WIFI_MANAGER.read_text(encoding="utf-8")
    event_body = _function_body(source, "void event_handler(")

    assert "g_wifi_events.push" in event_body
    for forbidden in (
        "g_machine.",
        "persist_runtime_credentials",
        "notify(",
        "connect_selected",
        "esp_wifi_",
        "nvs_",
        "xSemaphore",
    ):
        assert forbidden not in event_body


def test_wifi_state_task_owns_state_notifications_and_scan_start():
    source = WIFI_MANAGER.read_text(encoding="utf-8")
    worker_body = _function_body(source, "void wifi_timeout_task(")

    assert "g_wifi_events.pop" in worker_body
    assert "notify(" in worker_body
    assert "esp_wifi_scan_start" in worker_body


def test_public_scan_api_queues_request_without_touching_driver():
    source = WIFI_MANAGER.read_text(encoding="utf-8")
    scan_body = _function_body(source, "esp_err_t product_wifi_scan(")

    assert "g_scan_scheduler.request" in scan_body
    assert "esp_wifi_scan_start" not in scan_body
