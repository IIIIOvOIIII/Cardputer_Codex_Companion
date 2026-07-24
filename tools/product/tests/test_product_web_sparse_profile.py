import json
from pathlib import Path


SOURCE = Path("firmware/main/product/product_web.cpp")


def test_sparse_default_profile_fits_nvs_string_limit() -> None:
    payload = {
        "name": "SAFE",
        "revision": 1,
        "bindings": [None] * 224,
    }
    encoded = json.dumps(
        payload,
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode()
    assert len(encoded) == 1161
    assert len(encoded) < 4000


def test_product_web_uses_sparse_null_and_atomic_persistence() -> None:
    source = SOURCE.read_text()
    assert "cJSON_CreateNull()" in source
    assert "cJSON_IsNull(item)" in source
    assert "esp_err_t persist_profile(" in source
    assert '"{\\"error\\":\\"profile_persist_failed\\"}"' in source
    persist = source.index("persist_profile(json)")
    activate = source.index("g_profile = std::move(*candidate)")
    assert persist < activate


def test_profile_put_does_not_place_full_profile_on_https_task_stack() -> None:
    source = SOURCE.read_text()
    handler = source[
        source.index("esp_err_t put_profile_handler")
        : source.index("esp_err_t wifi_handler")
    ]
    assert "std::make_unique<Profile>()" in handler
    assert "Profile candidate;" not in handler
    assert "output = safe_profile();" not in source
    assert "Profile loaded;" not in source
