from pathlib import Path
import plistlib


ROOT = Path(__file__).resolve().parents[3]


def test_companion_bundle_metadata_and_build_script():
    info_path = ROOT / "companion/AppBundle/Info.plist"
    info = plistlib.loads(info_path.read_bytes())
    assert info["CFBundleIdentifier"] == "com.lynx.cardputer-companion"
    assert info["NSBluetoothAlwaysUsageDescription"]
    assert info["NSLocalNetworkUsageDescription"]
    script = (ROOT / "scripts/build_companion.sh").read_text()
    assert "swift build" in script
    assert "cardputer-companion" in script
    assert "codesign" in script


def test_companion_does_not_use_clipboard_or_command_v():
    source = "\n".join(
        path.read_text()
        for path in (ROOT / "companion/Sources").rglob("*.swift")
    ).lower()
    assert "nspasteboard" not in source
    assert "command-v" not in source


def test_companion_run_supports_secret_config_file():
    configuration = (
        ROOT / "companion/Sources/cardputer-companion/Configuration.swift"
    ).read_text()
    main = (
        ROOT / "companion/Sources/cardputer-companion/CardputerCompanionMain.swift"
    ).read_text()
    assert '"--config"' in configuration
    assert "CompanionConfigFile" in configuration
    assert "cardputer-companion run --config" in main


def test_launch_agent_installation_does_not_put_pin_in_plist_or_arguments():
    script_path = ROOT / "scripts/install_companion_launch_agent.py"
    script = script_path.read_text()
    assert "com.lynx.cardputer-companion" in script
    assert "--config" in script
    assert "--pairing" not in script
    assert "pairing" not in script.lower()


def test_launch_agent_includes_codex_cli_search_path():
    script = (ROOT / "scripts/install_companion_launch_agent.py").read_text()
    assert "EnvironmentVariables" in script
    assert "/opt/homebrew/bin" in script
    assert "/usr/local/bin" in script


def test_lan_bridge_uses_single_action_poll_for_launchd_local_network():
    bridge = (ROOT / "companion/Sources/cardputer-companion/LANBridge.swift").read_text()
    assert '"/usr/bin/curl"' in bridge
    assert '"--config"' in bridge
    assert "LocalTLSDelegate" not in bridge
    assert "URLSession" not in bridge
    assert 'path: "api/v1/companion/action"' in bridge
    assert "actionProcess" not in bridge
    assert "api/v1/companion/events" not in bridge


def test_companion_only_posts_changed_snapshots():
    main = (
        ROOT / "companion/Sources/cardputer-companion/CardputerCompanionMain.swift"
    ).read_text()
    assert "lastPostedSnapshot" in main
    assert "hasSameContent" in main
    assert "action.needsSnapshot" in main
    assert "Task.sleep(for: .seconds(2))" in main


def test_cardputer_display_uses_larger_body_text():
    display = (ROOT / "firmware/main/product/display.cpp").read_text()
    assert "kDisplayBodyTextSize = 2" in display
    assert "setTextSize(kDisplayBodyTextSize)" in display


def test_companion_state_is_atomic_across_http_and_ui_tasks():
    controller = (
        ROOT / "firmware/main/product/product_controller.cpp"
    ).read_text()
    assert (
        "std::atomic<ServiceState> g_companion_state{ServiceState::offline};"
        in controller
    )


def test_ble_watchdog_and_keyboard_use_atomic_link_snapshot():
    ble = (ROOT / "firmware/main/probe/ble_services.cpp").read_text()
    assert "std::atomic<bool> g_hid_ready" in ble
    assert "std::atomic<bool> g_hid_gap_connected" in ble
    assert "std::atomic<uint64_t> g_hid_state_changed_ms" in ble
    assert "return g_hid_ready.load();" in ble


def test_pet_renderer_uses_one_bounded_buffer_and_partial_push():
    display = (ROOT / "firmware/main/product/display.cpp").read_text()
    assert "std::array<uint16_t, 96 * 104> g_pet_frame" in display
    assert "constexpr int32_t kPetWidth = 96" in display
    assert "constexpr int32_t kPetHeight = 104" in display
    frame_body = display.split("bool display_render_pet_frame", 1)[1]
    frame_body = frame_body.split("void display_render_placeholder", 1)[0]
    assert "pushImage(kPetX, kPetY, kPetWidth, kPetHeight" in frame_body
    assert "fillScreen" not in frame_body


def test_companion_contains_pet_transcoder_without_selected_pet_assets():
    package = (ROOT / "companion/Package.swift").read_text()
    sources = "\n".join(
        path.read_text()
        for path in (ROOT / "companion/Sources").rglob("*.swift")
    )
    assert 'name: "ProductPet"' in package
    assert "PetTranscoder" in sources
    assert "rocky-spritesheet" not in sources


def test_release_scripts_do_not_package_codex_or_cached_pet_state():
    forbidden = (
        ".codex/config.toml",
        ".codex/cache/tui-pets",
        ".codex/pets",
        "slot-a.ccpt",
        "slot-b.ccpt",
        "upload.tmp",
        "pet.json",
        "spritesheet.webp",
    )
    scripts = "\n".join(
        path.read_text()
        for path in (
            ROOT / "scripts/package_product_firmware.sh",
            ROOT / "scripts/package_private_firmware.sh",
            ROOT / "scripts/build_companion.sh",
        )
    )
    for marker in forbidden:
        assert marker not in scripts


def test_pet_storage_worker_runs_below_keyboard_and_ui_tasks():
    store = (ROOT / "firmware/main/product/pet_store.cpp").read_text()
    controller = (
        ROOT / "firmware/main/product/product_controller.cpp"
    ).read_text()
    keyboard = (
        ROOT / "firmware/main/product/keyboard_matrix.cpp"
    ).read_text()
    assert 'upload_task, "product-pet-upload"' in store
    assert "tskIDLE_PRIORITY, impl_->upload_task_stack.data()" in store
    assert 'ui_task, "product-ui"' in controller
    assert "tskIDLE_PRIORITY + 1" in controller
    assert 'scanner_task, "scanner"' in keyboard
    assert "tskIDLE_PRIORITY + 3" in keyboard
