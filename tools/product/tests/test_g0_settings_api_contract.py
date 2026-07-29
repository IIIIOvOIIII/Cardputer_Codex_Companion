from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
WEB_CPP = ROOT / "firmware/main/product/product_web.cpp"
CONTROLLER_CPP = ROOT / "firmware/main/product/product_controller.cpp"


def function_body(source: str, name: str, next_name: str) -> str:
    start = source.index(name)
    end = source.index(next_name, start)
    return source[start:end]


def test_g0_settings_handlers_are_authenticated_and_controller_owned():
    web = WEB_CPP.read_text(encoding="utf-8")
    controller = CONTROLLER_CPP.read_text(encoding="utf-8")

    get_body = function_body(
        web,
        "esp_err_t get_g0_chord_handler(",
        "esp_err_t put_g0_chord_handler(",
    )
    put_body = function_body(
        web,
        "esp_err_t put_g0_chord_handler(",
        "esp_err_t restart_setup_handler(",
    )
    for body in (get_body, put_body):
        assert "authorized(request)" in body
        assert "normal_configuration_available(request)" in body
        assert "pairing_required" not in body

    assert "g_g0_chord_get_handler" in get_body
    assert "product_web_g0_chord_json" in get_body
    assert "cJSON_IsBool(enabled)" in put_body
    assert "cJSON_IsNumber(modifiers)" in put_body
    assert "cJSON_IsArray(usages)" in put_body
    assert "product_web_g0_chord_is_valid" in put_body
    assert "g_g0_chord_apply_handler" in put_body
    assert "product_web_g0_chord_error" in put_body

    assert "ProductWebG0ChordSettings web_g0_chord_settings()" in controller
    assert "DeviceSettingsResult apply_web_g0_chord_settings(" in controller
    assert "product_web_set_g0_chord_handlers(" in controller
    assert "snapshot_device_settings()" in controller
    assert "commit_device_settings(candidate)" in controller
