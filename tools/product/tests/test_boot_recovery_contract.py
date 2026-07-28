from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
CONTROLLER = ROOT / "firmware/main/product/product_controller.cpp"
RECOVERY = ROOT / "firmware/main/product/boot_recovery.cpp"
KEYBOARD = ROOT / "firmware/main/product/keyboard_matrix.cpp"
DISPLAY = ROOT / "firmware/main/product/display.cpp"


def test_recovery_runs_after_nvs_init_before_loading_product_state():
    source = CONTROLLER.read_text(encoding="utf-8")
    config_start = source.index("bool config() override")
    config_end = source.index("bool keyboard() override", config_start)
    config = source[config_start:config_end]

    nvs = config.index("nvs_flash_init()")
    recovery = config.index("product_boot_recovery()")
    onboarding = config.index("g_onboarding_state.load")
    assert nvs < recovery < onboarding


def test_recovery_uses_early_matrix_input_and_never_hid():
    recovery = RECOVERY.read_text(encoding="utf-8")
    keyboard = KEYBOARD.read_text(encoding="utf-8")

    assert "keyboard_matrix_key_pressed" in recovery
    assert "keyboard_matrix_key_pressed" in keyboard
    assert "KeyboardProbe" not in recovery
    assert "send_report" not in recovery


def test_recovery_erases_namespaces_and_build_selected_storage_partition():
    recovery = RECOVERY.read_text(encoding="utf-8")

    assert "nvs_erase_all" in recovery
    assert "nvs_commit" in recovery
    assert "kProductStoragePartitionLabel" in recovery
    assert "esp_partition_erase_range" in recovery
    assert "nvs_flash_erase()" not in recovery


def test_recovery_has_dedicated_prompt_and_result_rendering():
    display = DISPLAY.read_text(encoding="utf-8")

    assert "display_render_boot_recovery_prompt" in display
    assert 'M5.Display.println("DELETE ALL")' in display
    assert 'M5.Display.println("COMPANION DATA?")' in display
    assert "Y = DELETE" in display
    assert "N = CANCEL" in display
    assert "display_render_boot_recovery_result" in display
