import json
from pathlib import Path

import pytest

from tools.product.verify_firmware_memory import validate_firmware_memory

REPO_ROOT = Path(__file__).parents[3]


def test_accepts_release_with_ample_diram_headroom(tmp_path):
    report = tmp_path / "size.json"
    report.write_text(json.dumps({"diram_remain": 149_581}), encoding="utf-8")

    validate_firmware_memory(report, minimum_diram_bytes=96 * 1024)


def test_rejects_release_that_would_starve_runtime_services(tmp_path):
    report = tmp_path / "size.json"
    report.write_text(json.dumps({"diram_remain": 8_909}), encoding="utf-8")

    with pytest.raises(ValueError, match="DIRAM headroom"):
        validate_firmware_memory(report, minimum_diram_bytes=96 * 1024)


def test_product_release_enforces_target_diram_budget():
    release = (REPO_ROOT / "scripts/verify_product_release.sh").read_text(
        encoding="utf-8"
    )

    assert "-m esp_idf_size --format json" in release
    assert "tools/product/verify_firmware_memory.py" in release


def test_product_release_recreates_ignored_sdkconfig():
    release = (REPO_ROOT / "scripts/verify_product_release.sh").read_text(
        encoding="utf-8"
    )

    assert 'rm -f firmware/sdkconfig firmware/sdkconfig.old' in release


def test_product_release_uses_boot_safe_main_task_stack():
    defaults = (REPO_ROOT / "firmware/sdkconfig.defaults").read_text(
        encoding="utf-8"
    )

    assert "CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192" in defaults


def test_product_release_uses_product_ble_gap_name():
    defaults = (REPO_ROOT / "firmware/sdkconfig.defaults").read_text(
        encoding="utf-8"
    )

    assert 'CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME="Cardputer Codex"' in defaults


def test_pet_display_uses_only_a_single_rgb565_row():
    display = (
        REPO_ROOT / "firmware/main/product/display.cpp"
    ).read_text(encoding="utf-8")

    assert "g_pet_frame" not in display
    assert (
        "pushImage(kPetX, kPetY + static_cast<int32_t>(row)," in display
    )
    assert "kPetFrameWidth, 1, pixels.data()" in display


def test_product_runtime_uses_measured_static_stack_budgets():
    controller = (
        REPO_ROOT / "firmware/main/product/product_controller.cpp"
    ).read_text(encoding="utf-8")

    assert (
        "std::array<StackType_t, 2048> g_macro_task_stack{};"
        in controller
    )
    assert (
        "std::array<StackType_t, 2048> g_audio_task_stack{};"
        in controller
    )
    assert (
        "std::array<StackType_t, 4096> g_ui_task_stack{};"
        in controller
    )


def test_wifi_cfg_partition_is_optional_for_wrong_or_generic_flash_layouts():
    wifi_manager = (REPO_ROOT / "firmware/main/product/wifi_manager.cpp").read_text(
        encoding="utf-8"
    )

    assert 'ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_init_partition("wifi_cfg"))' not in wifi_manager
    assert "init_optional_wifi_config_partition()" in wifi_manager
