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


def test_companion_pet_sync_has_independent_serial_error_boundary():
    main = (
        ROOT / "companion/Sources/cardputer-companion/CardputerCompanionMain.swift"
    ).read_text()
    due = main.index("if petSyncCadence.isDue(at: now)")
    synchronize = main.index(
        "await petSync.synchronize(client: bridge)",
        due,
    )
    action_boundary = main.index("do {", synchronize)
    action = main.index("let action = try await bridge.pollAction()", action_boundary)

    assert due < synchronize < action_boundary < action
    assert "petSyncCadence.record(" in main[due:action_boundary]
    assert "nextPetSynchronization" not in main
    assert "retry in 5 seconds" in main
    assert "next check in 30 seconds" in main


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


def test_pet_renderer_declares_native_rgb565_byte_order():
    display = (ROOT / "firmware/main/product/display.cpp").read_text()
    frame_body = display.split("bool display_render_pet_frame", 1)[1]
    frame_body = frame_body.split("void display_render_placeholder", 1)[0]
    assert "getSwapBytes()" in frame_body
    assert "setSwapBytes(true)" in frame_body
    assert "setSwapBytes(previous_swap)" in frame_body


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


def test_pet_storage_worker_uses_declared_static_stack_depth():
    store = (ROOT / "firmware/main/product/pet_store.cpp").read_text()
    header = (ROOT / "firmware/main/product/pet_store.hpp").read_text()

    assert "std::array<StackType_t, 8192> upload_task_stack" in store
    assert "impl_->upload_task_stack.size(),\n      this" in store
    assert "sizeof(impl_->upload_task_stack)" not in store
    assert "do_initialize" in header
    assert "case kCommandInitialize:" in store


def test_pet_storage_uses_raw_transactional_slots():
    store = (ROOT / "firmware/main/product/pet_store.cpp").read_text()

    assert "esp_partition_erase_range" in store
    assert "esp_partition_write" in store
    assert "PartitionSource" in store
    assert "esp_vfs_spiffs_register" not in store
    assert "std::fopen" not in store


def test_pet_chunk_body_has_single_owned_allocation():
    web = (ROOT / "firmware/main/product/product_web.cpp").read_text()
    store = (ROOT / "firmware/main/product/pet_store.cpp").read_text()
    chunk_handler = web.split("esp_err_t pet_chunk_handler", 1)[1].split(
        "esp_err_t pet_commit_handler", 1
    )[0]

    assert "read_binary_body(request" in chunk_handler
    assert "append_owned(" in chunk_handler
    assert "std::move(body)" in chunk_handler
    assert "command_chunk.assign(" not in store


def test_pet_sync_keeps_companion_online_and_closes_curl_pipes():
    main = (
        ROOT / "companion/Sources/cardputer-companion/CardputerCompanionMain.swift"
    ).read_text()
    bridge = (
        ROOT / "companion/Sources/cardputer-companion/LANBridge.swift"
    ).read_text()
    web = (ROOT / "firmware/main/product/product_web.cpp").read_text()

    assert "postSnapshotIfChanged" in main
    assert main.count("try await postSnapshotIfChanged") == 1
    assert "petSyncCadence.isDue" in main
    assert "note_companion_activity();" in web
    assert "try? output.fileHandleForReading.close()" in bridge
    assert "try? error.fileHandleForReading.close()" in bridge


def test_authenticated_companion_handlers_refresh_heartbeat():
    web = (ROOT / "firmware/main/product/product_web.cpp").read_text()
    boundaries = (
        (
            "esp_err_t companion_status_handler",
            "esp_err_t companion_action_handler",
        ),
        (
            "esp_err_t companion_action_handler",
            "esp_err_t pet_status_response",
        ),
        ("esp_err_t pet_status_handler", "esp_err_t pet_begin_handler"),
        ("esp_err_t pet_begin_handler", "esp_err_t pet_chunk_handler"),
        ("esp_err_t pet_chunk_handler", "esp_err_t pet_commit_handler"),
        ("esp_err_t pet_commit_handler", "}  // namespace"),
    )
    for index, (start, end) in enumerate(boundaries):
        handler = web.split(start, 1)[1].split(end, 1)[0]
        authorization = handler.index(
            "authorize_request(request, true)"
            if index == 1
            else "authorized(request)"
        )
        activity = handler.index("note_companion_activity();")
        assert authorization < activity


def test_pet_frame_decode_does_not_allocate_from_runtime_heap():
    bundle = (ROOT / "firmware/main/product/pet_bundle.cpp").read_text()
    decode = bundle.split("PetBundleError decode_pet_frame", 1)[1]

    assert "std::vector<uint8_t>" not in decode
    assert "std::array<uint8_t, kPetFrameWidth * 2>" in decode


def test_pet_digest_uses_bounded_worker_stack():
    bundle = (ROOT / "firmware/main/product/pet_bundle.cpp").read_text()
    digest = bundle.split("source_digest(", 1)[1].split("}  // namespace", 1)[0]

    assert "std::array<uint8_t, 1024> buffer" in digest
