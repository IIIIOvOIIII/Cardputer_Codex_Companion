get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(PROTOCOL_ROOT "${REPO_ROOT}/protocol/phase0")
set(PROTOCOL_GENERATOR
    "${REPO_ROOT}/tools/phase0/generate_firmware_protocol_vectors.py")
set(PROTOCOL_OUTPUT_DIR "${REPO_ROOT}/build/phase0/generated")
set(PROTOCOL_HEADER
    "${PROTOCOL_OUTPUT_DIR}/phase0_protocol_vectors.hpp")

if(NOT CMAKE_SCRIPT_MODE_FILE)
    find_program(UV_EXECUTABLE uv REQUIRED)
    add_custom_command(
        OUTPUT "${PROTOCOL_HEADER}"
        COMMAND
            "${CMAKE_COMMAND}" -E make_directory "${PROTOCOL_OUTPUT_DIR}"
        COMMAND
            "${UV_EXECUTABLE}" run python "${PROTOCOL_GENERATOR}"
            --protocol-root "${PROTOCOL_ROOT}"
            --output "${PROTOCOL_HEADER}"
        COMMAND
            "${UV_EXECUTABLE}" run python "${PROTOCOL_GENERATOR}"
            --protocol-root "${PROTOCOL_ROOT}"
            --output "${PROTOCOL_HEADER}"
            --check
        DEPENDS
            "${PROTOCOL_GENERATOR}"
            "${PROTOCOL_ROOT}/pairing-v1.md"
            "${PROTOCOL_ROOT}/gatt-auth-v1.md"
            "${PROTOCOL_ROOT}/wss-auth-v1.md"
            "${PROTOCOL_ROOT}/fixtures/pairing-v1.json"
            "${PROTOCOL_ROOT}/fixtures/gatt-auth-v1.json"
            "${PROTOCOL_ROOT}/fixtures/wss-auth-v1.json"
        WORKING_DIRECTORY "${REPO_ROOT}"
        COMMENT "Generating phase0 protocol vectors"
    )
    add_custom_target(
        phase0_protocol_vectors DEPENDS "${PROTOCOL_HEADER}")
endif()

idf_component_register(
    SRCS
        "app_main.cpp"
        "probe/ble_services.cpp"
        "probe/pinned_wss_transport.cpp"
        "probe/bounded_https_server.cpp"
        "probe/hardware_probe.cpp"
        "probe/hid_engine.cpp"
        "probe/keyboard_probe.cpp"
        "probe/pre_tls_limiter.cpp"
        "probe/protocol_codec.cpp"
        "probe/resource_metrics.cpp"
        "probe/web_async_dispatch.cpp"
        "probe/web_guard.cpp"
        "probe/web_handlers.cpp"
        "product/ui_model.cpp"
        "product/ui_navigation.cpp"
        "product/display.cpp"
        "product/keyboard_matrix.cpp"
        "product/input_router.cpp"
        "product/profile.cpp"
        "product/profile_store.cpp"
        "product/macro_engine.cpp"
        "product/wifi_manager.cpp"
        "product/product_web.cpp"
        "product/device_identity.cpp"
        "product/companion_protocol.cpp"
        "product/product_controller.cpp"
        "product/pet_bundle.cpp"
        "product/pet_store.cpp"
    INCLUDE_DIRS
        "."
        "probe"
        "${PROTOCOL_OUTPUT_DIR}"
    PRIV_REQUIRES
        bt
        esp-tls
        esp_hid
        esp_hw_support
        esp_http_server
        esp_https_server
        esp_psram
        esp_timer
        esp_event
        esp_netif
        esp_wifi
        freertos
        json
        lwip
        mbedtls
        nvs_flash
        spi_flash
        m5unified
    WHOLE_ARCHIVE
)

set(HID_EXAMPLE_DIR
    "$ENV{IDF_PATH}/examples/bluetooth/esp_hid_device/main")
execute_process(
    COMMAND
        git -C "$ENV{IDF_PATH}" diff --quiet HEAD --
        examples/bluetooth/esp_hid_device/main/esp_hid_gap.c
        examples/bluetooth/esp_hid_device/main/esp_hid_gap.h
    RESULT_VARIABLE HID_GAP_DIRTY
)
if(NOT HID_GAP_DIRTY EQUAL 0)
    message(FATAL_ERROR "pinned ESP-IDF HID GAP helper is modified")
endif()
target_sources(
    ${COMPONENT_LIB} PRIVATE "${HID_EXAMPLE_DIR}/esp_hid_gap.c")
target_include_directories(
    ${COMPONENT_LIB} PRIVATE "${HID_EXAMPLE_DIR}")
if(TARGET phase0_protocol_vectors)
    add_dependencies(${COMPONENT_LIB} phase0_protocol_vectors)
endif()
