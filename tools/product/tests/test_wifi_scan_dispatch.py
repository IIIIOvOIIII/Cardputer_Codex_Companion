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


def test_wifi_scan_completion_is_deferred_out_of_system_event_task():
    source = WIFI_MANAGER.read_text()
    event_body = _function_body(source, "void event_handler(")
    scan_branch = event_body.split("WIFI_EVENT_SCAN_DONE", 1)[1].split(
        "} else if", 1
    )[0]
    worker_body = _function_body(source, "void wifi_timeout_task(")

    assert "g_wifi_events.push" in scan_branch
    assert "WifiPendingEventKind::scan_done" in scan_branch
    assert "publish_scan_results();" not in scan_branch
    assert "g_wifi_events.pop" in worker_body
    assert "WifiPendingEventKind::scan_done" in worker_body
    assert "publish_scan_results();" in worker_body


def test_wifi_scan_result_path_uses_bounded_storage():
    source = WIFI_MANAGER.read_text()
    publish_body = _function_body(source, "void publish_scan_results(")

    assert "std::vector" not in publish_body
    assert "g_scan_records" in publish_body
    assert "select_wifi_scan_entries" in publish_body
