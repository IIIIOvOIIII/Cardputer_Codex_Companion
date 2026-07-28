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


def test_product_release_enables_reproducible_firmware_builds():
    defaults = (REPO_ROOT / "firmware/sdkconfig.defaults").read_text(
        encoding="utf-8"
    )

    assert "CONFIG_APP_REPRODUCIBLE_BUILD=y" in defaults


def test_product_release_allows_only_versioned_checksum_manifests_in_dist():
    release = (REPO_ROOT / "scripts/verify_product_release.sh").read_text(
        encoding="utf-8"
    )

    assert (
        "grep -Ev "
        "'^dist/[0-9]+\\.[0-9]+\\.[0-9]+-SHA256SUMS$'"
        in release
    )
    assert "private or generated artifacts are tracked" in release


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


def test_product_release_suppresses_per_packet_nimble_info_logs():
    defaults = (REPO_ROOT / "firmware/sdkconfig.defaults").read_text(
        encoding="utf-8"
    )

    assert "CONFIG_BT_NIMBLE_LOG_LEVEL_WARNING=y" in defaults


def test_product_release_uses_usb_serial_jtag_as_bidirectional_console():
    defaults = (REPO_ROOT / "firmware/sdkconfig.defaults").read_text(
        encoding="utf-8"
    )
    controller = (
        REPO_ROOT / "firmware/main/product/product_controller.cpp"
    ).read_text(encoding="utf-8")

    assert "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y" in defaults
    assert "CONFIG_ESP_CONSOLE_SECONDARY_NONE=y" in defaults
    assert "CONFIG_LIBC_STDIN_LINE_ENDING_LF=y" in defaults
    assert "usb_serial_jtag_ll_read_rxfifo" in controller
    assert "read(STDIN_FILENO" not in controller


def test_product_release_mtu_fits_one_unfragmented_audio_frame():
    defaults = (REPO_ROOT / "firmware/sdkconfig.defaults").read_text(
        encoding="utf-8"
    )

    assert "CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=247" in defaults
    assert "CONFIG_BT_NIMBLE_LEGACY_VHCI_ENABLE=y" in defaults
    assert "CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT=24" in defaults
    assert "CONFIG_BT_NIMBLE_MSYS_1_BLOCK_SIZE=128" in defaults
    assert "CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT=24" in defaults
    assert "CONFIG_BT_NIMBLE_MSYS_2_BLOCK_SIZE=320" in defaults


def test_audio_notifications_allocate_directly_from_the_large_mbuf_pool():
    services = (
        REPO_ROOT / "firmware/main/probe/ble_services.cpp"
    ).read_text(encoding="utf-8")
    start = services.index(
        "esp_err_t notify_audio_frame(std::span<const uint8_t> frame)"
    )
    end = services.index(
        "esp_err_t notify_audio_status", start
    )
    audio_notify = services[start:end]

    assert "os_msys_get_pkthdr" in audio_notify
    assert "ble_audio_notification_allocation_bytes" in audio_notify
    assert "ble_hs_mbuf_from_flat" not in audio_notify


def test_audio_notifications_are_paced_before_allocating_mbufs():
    services = (
        REPO_ROOT / "firmware/main/probe/ble_services.cpp"
    ).read_text(encoding="utf-8")
    start = services.index(
        "esp_err_t notify_audio_frame(std::span<const uint8_t> frame)"
    )
    end = services.index("esp_err_t notify_audio_status", start)
    audio_notify = services[start:end]

    pace = audio_notify.index("pace_audio_notification();")
    allocate = audio_notify.index("os_msys_get_pkthdr")
    notify = audio_notify.index("ble_gatts_notify_custom")
    assert pace < allocate < notify


def test_pet_display_uses_a_scoped_complete_frame_for_one_lcd_push():
    display = (
        REPO_ROOT / "firmware/main/product/display.cpp"
    ).read_text(encoding="utf-8")

    assert "display_prepare_pet_frame_buffer" not in display
    assert "std::array<uint16_t, kPetFramePixels> g_pet_frame{};" not in display
    frame_body = display.split("bool display_render_pet_frame", 1)[1]
    frame_body = frame_body.split("void display_render_placeholder", 1)[0]
    assert "new (std::nothrow) uint16_t[kPetFramePixels]" in frame_body
    assert "store.decode(state, frame_index, frame_span)" in frame_body
    assert "store.decode_rows(" not in frame_body
    assert (
        "M5.Display.pushImage(kPetX, kPetY, kPetFrameWidth,"
        in frame_body
    )
    assert "kPetFrameHeight, frame.get())" in frame_body
    assert frame_body.count("M5.Display.startWrite()") == 1
    assert frame_body.count("M5.Display.endWrite()") == 1


def test_status_pages_restore_body_cursor_after_header_microphone():
    display = (
        REPO_ROOT / "firmware/main/product/display.cpp"
    ).read_text(encoding="utf-8")

    microphone = display.index(
        "draw_microphone_status(model, kBackground);"
    )
    body_cursor = display.index("M5.Display.setCursor(0, 20);", microphone)
    content = display.index(
        "const UiPageContent content = model.page_content();",
        body_cursor,
    )
    assert microphone < body_cursor < content


def test_product_runtime_uses_measured_static_stack_budgets():
    controller = (
        REPO_ROOT / "firmware/main/product/product_controller.cpp"
    ).read_text(encoding="utf-8")

    assert (
        "std::array<StackType_t, 1920> g_macro_task_stack{};"
        in controller
    )
    assert (
        "std::array<StackType_t, 3584> g_audio_task_stack{};"
        in controller
    )
    assert (
        "std::array<StackType_t, 4096> g_ui_task_stack{};"
        in controller
    )
    assert 'xTaskGetHandle("pet-upload")' in controller
    assert 'xTaskGetHandle("ble-watchdog")' in controller
    assert 'xTaskGetHandle("wifi-state")' in controller
    assert "constexpr uint32_t kHidSenderTaskStackBytes = 3072;" in (
        REPO_ROOT / "firmware/main/probe/keyboard_probe.hpp"
    ).read_text(encoding="utf-8")
    assert "std::array<StackType_t, 1920> g_ble_watchdog_stack{};" in (
        REPO_ROOT / "firmware/main/probe/ble_services.cpp"
    ).read_text(encoding="utf-8")
    assert "constexpr uint32_t kWifiStateTaskStackBytes = 4608;" in (
        REPO_ROOT / "firmware/main/product/wifi_manager.hpp"
    ).read_text(encoding="utf-8")
    assert (
        "std::array<StackType_t, kWifiStateTaskStackBytes> "
        "g_wifi_task_stack{};"
    ) in (
        REPO_ROOT / "firmware/main/product/wifi_manager.cpp"
    ).read_text(encoding="utf-8")
    assert "static_cast<unsigned>(kWifiStateTaskStackBytes)," in controller
    assert "std::array<StackType_t, 7552> upload_task_stack{};" in (
        REPO_ROOT / "firmware/main/product/pet_store.cpp"
    ).read_text(encoding="utf-8")


def test_audio_capture_preempts_https_tls_work():
    controller = (
        REPO_ROOT / "firmware/main/product/product_controller.cpp"
    ).read_text(encoding="utf-8")

    assert "constexpr UBaseType_t kAudioTaskPriority = tskIDLE_PRIORITY + 6;" in controller
    assert (
        'audio_task, "product-audio", g_audio_task_stack.size(), nullptr,\n'
        "            kAudioTaskPriority,"
    ) in controller


def test_microphone_fallback_uses_complete_loss_windows_only():
    controller = (
        REPO_ROOT / "firmware/main/product/product_controller.cpp"
    ).read_text(encoding="utf-8")

    assert "consecutive_missing" not in controller
    assert controller.count("g_microphone->on_loss_window(") == 1


def test_wifi_cfg_partition_is_optional_for_wrong_or_generic_flash_layouts():
    wifi_manager = (REPO_ROOT / "firmware/main/product/wifi_manager.cpp").read_text(
        encoding="utf-8"
    )

    assert 'ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_init_partition("wifi_cfg"))' not in wifi_manager
    assert "init_optional_wifi_config_partition()" in wifi_manager
