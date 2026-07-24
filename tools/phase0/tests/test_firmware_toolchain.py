import json
from pathlib import Path


def test_esp_idf_is_pinned_to_the_reviewed_commit() -> None:
    lock = json.loads(Path("toolchain.lock.json").read_text())
    assert lock["esp_idf"] == {
        "tag": "v5.5.4",
        "commit": "735507283d5b2f9fb363a1901172dbd9e847945d",
    }
    assert lock["components"]["espressif/esp_websocket_client"] == "1.7.0"
    assert lock["components"]["m5stack/m5unified"] == "0.2.17"
