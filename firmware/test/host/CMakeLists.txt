cmake_minimum_required(VERSION 3.20)

project(phase0_firmware_host LANGUAGES C CXX)
enable_testing()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(PROTOCOL_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../protocol/phase0)
set(GENERATOR_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/../../../tools/phase0/generate_firmware_protocol_vectors.py)
set(PROTOCOL_VECTOR_OUTPUT_DIR ${REPO_ROOT}/build/phase0/generated)
set(PROTOCOL_VECTOR_HEADER ${PROTOCOL_VECTOR_OUTPUT_DIR}/phase0_protocol_vectors.hpp)
find_program(UV_EXECUTABLE uv REQUIRED)

add_custom_command(
  OUTPUT ${PROTOCOL_VECTOR_HEADER}
  DEPENDS
    ${GENERATOR_SCRIPT}
    ${PROTOCOL_ROOT}/pairing-v1.md
    ${PROTOCOL_ROOT}/gatt-auth-v1.md
    ${PROTOCOL_ROOT}/wss-auth-v1.md
    ${PROTOCOL_ROOT}/fixtures/pairing-v1.json
    ${PROTOCOL_ROOT}/fixtures/gatt-auth-v1.json
    ${PROTOCOL_ROOT}/fixtures/wss-auth-v1.json
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PROTOCOL_VECTOR_OUTPUT_DIR}
  COMMAND ${UV_EXECUTABLE} run python ${GENERATOR_SCRIPT}
    --protocol-root ${PROTOCOL_ROOT}
    --output ${PROTOCOL_VECTOR_HEADER}
  COMMAND ${UV_EXECUTABLE} run python ${GENERATOR_SCRIPT}
    --protocol-root ${PROTOCOL_ROOT}
    --output ${PROTOCOL_VECTOR_HEADER}
    --check
  WORKING_DIRECTORY ${REPO_ROOT}
  COMMENT "Generating phase0 protocol vectors"
)

add_custom_target(generate_protocol_vectors DEPENDS ${PROTOCOL_VECTOR_HEADER})

add_executable(test_probe_controller
  test_probe_controller.cpp
  ../../main/probe/probe_controller.cpp
)

target_include_directories(test_probe_controller
  PRIVATE
    ../../main
    ${PROTOCOL_VECTOR_OUTPUT_DIR}
)

add_test(NAME probe_controller COMMAND test_probe_controller)

add_executable(test_protocol_vectors
  test_protocol_vectors.cpp
  ../../main/probe/protocol_codec.cpp
  ${PROTOCOL_VECTOR_HEADER}
)

target_include_directories(test_protocol_vectors
  PRIVATE
    ../../main
    ${PROTOCOL_VECTOR_OUTPUT_DIR}
)

target_link_libraries(test_protocol_vectors PRIVATE OpenSSL::Crypto)
target_compile_options(
  test_protocol_vectors PRIVATE -Wno-deprecated-declarations)
add_dependencies(test_protocol_vectors generate_protocol_vectors)
add_test(NAME protocol_vectors COMMAND test_protocol_vectors)

add_executable(test_hid_engine
  test_hid_engine.cpp
  ../../main/probe/hid_engine.cpp
)
target_include_directories(test_hid_engine
  PRIVATE
    ../../main
)
add_test(NAME hid_engine COMMAND test_hid_engine)

add_executable(test_keyboard_probe
  test_keyboard_probe.cpp
  ../../main/probe/keyboard_probe.cpp
  ../../main/probe/hid_engine.cpp
  ../../main/probe/resource_metrics.cpp
)
target_include_directories(test_keyboard_probe
  PRIVATE
    ../../main
)
add_test(NAME keyboard_probe COMMAND test_keyboard_probe)

add_executable(test_ble_manifest
  test_ble_manifest.cpp
  ../../main/probe/ble_services.cpp
)
target_include_directories(test_ble_manifest
  PRIVATE
    ../../main
)
add_test(NAME ble_manifest COMMAND test_ble_manifest)

add_executable(test_gatt_sender
  test_gatt_sender.cpp
  ../../main/probe/protocol_codec.cpp
  ${PROTOCOL_VECTOR_HEADER}
)

target_include_directories(test_gatt_sender
  PRIVATE
    ../../main
    ${PROTOCOL_VECTOR_OUTPUT_DIR}
)

target_link_libraries(test_gatt_sender PRIVATE OpenSSL::Crypto)
target_compile_options(
  test_gatt_sender PRIVATE -Wno-deprecated-declarations)
add_dependencies(test_gatt_sender generate_protocol_vectors)
add_test(NAME gatt_sender COMMAND test_gatt_sender)

add_executable(test_pre_tls_limiter
  test_pre_tls_limiter.cpp
  ../../main/probe/pre_tls_limiter.cpp
)

target_include_directories(test_pre_tls_limiter
  PRIVATE
    ../../main
)

add_test(NAME pre_tls_limiter COMMAND test_pre_tls_limiter)

add_executable(test_web_guard
  test_web_guard.cpp
  ../../main/probe/web_guard.cpp
)

target_include_directories(test_web_guard
  PRIVATE
    ../../main
)

target_link_libraries(test_web_guard PRIVATE OpenSSL::Crypto)
add_test(NAME web_guard COMMAND test_web_guard)

add_executable(test_web_async_dispatch
  test_web_async_dispatch.cpp
  ../../main/probe/web_async_dispatch.cpp
)

target_include_directories(test_web_async_dispatch
  PRIVATE
    ../../main
)

add_test(NAME web_async_dispatch COMMAND test_web_async_dispatch)

add_executable(test_wss_contract
  test_wss_contract.cpp
  ../../main/probe/protocol_codec.cpp
  ../../main/probe/pinned_wss_transport.cpp
)

target_include_directories(test_wss_contract
  PRIVATE
    ../../main
    ${PROTOCOL_VECTOR_OUTPUT_DIR}
)

target_link_libraries(test_wss_contract PRIVATE OpenSSL::Crypto)
add_dependencies(test_wss_contract generate_protocol_vectors)
add_test(NAME wss_contract COMMAND test_wss_contract)

add_executable(test_resource_metrics
  test_resource_metrics.cpp
  ../../main/probe/resource_metrics.cpp
)
target_include_directories(test_resource_metrics
  PRIVATE
    ../../main
)
add_test(NAME resource_metrics COMMAND test_resource_metrics)

add_executable(test_product_types test_product_types.cpp)
target_include_directories(test_product_types PRIVATE ../../main)
add_test(NAME product_types COMMAND test_product_types)

add_executable(test_product_types_launcher test_product_types_launcher.cpp)
target_include_directories(test_product_types_launcher PRIVATE ../../main)
target_compile_definitions(
  test_product_types_launcher
  PRIVATE CARDPUTER_PRODUCT_VERSION="1.3.2l"
)
add_test(NAME product_types_launcher COMMAND test_product_types_launcher)

add_executable(test_hil_serial_control
  test_hil_serial_control.cpp
  ../../main/product/hil_serial_control.cpp
)
target_include_directories(test_hil_serial_control PRIVATE ../../main)
add_test(NAME hil_serial_control COMMAND test_hil_serial_control)

add_executable(test_display_policy
  test_display_policy.cpp
  ../../main/product/display_policy.cpp
)
target_include_directories(test_display_policy PRIVATE ../../main)
add_test(NAME display_policy COMMAND test_display_policy)

add_executable(test_ui_model
  test_ui_model.cpp
  ../../main/product/ui_model.cpp
)
target_include_directories(test_ui_model PRIVATE ../../main)
add_test(NAME ui_model COMMAND test_ui_model)

add_executable(test_storage_compatibility
  test_storage_compatibility.cpp
  ../../main/product/storage_compatibility.cpp
)
target_include_directories(test_storage_compatibility PRIVATE ../../main)
add_test(NAME storage_compatibility COMMAND test_storage_compatibility)

add_executable(test_keyboard_matrix
  test_keyboard_matrix.cpp
  ../../main/product/keyboard_matrix.cpp
)
target_include_directories(test_keyboard_matrix PRIVATE ../../main)
add_test(NAME keyboard_matrix COMMAND test_keyboard_matrix)

add_executable(test_boot_recovery
  test_boot_recovery.cpp
  ../../main/product/boot_recovery.cpp
)
target_include_directories(test_boot_recovery PRIVATE ../../main)
add_test(NAME boot_recovery COMMAND test_boot_recovery)

add_executable(test_input_router
  test_input_router.cpp
  ../../main/product/input_router.cpp
  ../../main/probe/hid_engine.cpp
)
target_include_directories(test_input_router PRIVATE ../../main)
add_test(NAME input_router COMMAND test_input_router)

add_executable(test_profile
  test_profile.cpp
  ../../main/product/profile.cpp
  ../../main/product/profile_codec.cpp
  ../../main/product/profile_store.cpp
  ../../../.tools/esp-idf/components/json/cJSON/cJSON.c
)
target_include_directories(test_profile PRIVATE
  ../../main
  ../../../.tools/esp-idf/components/json/cJSON
)
add_test(NAME profile COMMAND test_profile)

add_executable(test_profile_catalog
  test_profile_catalog.cpp
  ../../main/product/profile.cpp
  ../../main/product/profile_codec.cpp
  ../../main/product/profile_catalog.cpp
  ../../../.tools/esp-idf/components/json/cJSON/cJSON.c
)
target_include_directories(test_profile_catalog PRIVATE
  ../../main
  ../../../.tools/esp-idf/components/json/cJSON
)
add_test(NAME profile_catalog COMMAND test_profile_catalog)

add_executable(test_macro_engine
  test_macro_engine.cpp
  ../../main/product/profile.cpp
  ../../main/product/macro_engine.cpp
  ../../main/probe/hid_engine.cpp
)
target_include_directories(test_macro_engine PRIVATE ../../main)
add_test(NAME macro_engine COMMAND test_macro_engine)

add_executable(test_wifi_manager
  test_wifi_manager.cpp
  ../../main/product/wifi_manager.cpp
)
target_include_directories(test_wifi_manager PRIVATE ../../main)
add_test(NAME wifi_manager COMMAND test_wifi_manager)

add_executable(test_product_web test_product_web.cpp)
target_include_directories(test_product_web PRIVATE ../../main)
add_test(NAME product_web COMMAND test_product_web)

add_executable(test_device_identity test_device_identity.cpp)
target_include_directories(test_device_identity PRIVATE ../../main)
add_test(NAME device_identity COMMAND test_device_identity)

add_executable(test_companion_protocol
  test_companion_protocol.cpp
  ../../main/product/companion_protocol.cpp
)
target_include_directories(test_companion_protocol PRIVATE ../../main)
add_test(NAME companion_protocol COMMAND test_companion_protocol)

add_executable(test_product_controller
  test_product_controller.cpp
  ../../main/product/product_controller.cpp
)
target_include_directories(test_product_controller PRIVATE ../../main)
add_test(NAME product_controller COMMAND test_product_controller)

add_executable(test_pet_bundle
  test_pet_bundle.cpp
  ../../main/product/pet_bundle.cpp
)
target_include_directories(test_pet_bundle PRIVATE ../../main)
target_link_libraries(test_pet_bundle PRIVATE OpenSSL::Crypto)
add_test(NAME pet_bundle COMMAND test_pet_bundle)

add_executable(test_pet_store test_pet_store.cpp)
target_include_directories(test_pet_store PRIVATE ../../main)
add_test(NAME pet_store COMMAND test_pet_store)

add_executable(test_ui_navigation
  test_ui_navigation.cpp
  ../../main/product/ui_navigation.cpp
)
target_include_directories(test_ui_navigation PRIVATE ../../main)
add_test(NAME ui_navigation COMMAND test_ui_navigation)

add_executable(test_device_settings
  test_device_settings.cpp
  ../../main/product/device_settings.cpp
)
target_include_directories(test_device_settings PRIVATE ../../main)
add_test(NAME device_settings COMMAND test_device_settings)

add_executable(test_onboarding
  test_onboarding.cpp
  ../../main/product/onboarding.cpp
)
target_include_directories(test_onboarding PRIVATE ../../main)
add_test(NAME onboarding COMMAND test_onboarding)

add_executable(test_settings_menu
  test_settings_menu.cpp
  ../../main/product/settings_menu.cpp
)
target_include_directories(test_settings_menu PRIVATE ../../main)
add_test(NAME settings_menu COMMAND test_settings_menu)

add_executable(test_pin_rotation
  test_pin_rotation.cpp
  ../../main/product/pin_rotation.cpp
)
target_include_directories(test_pin_rotation PRIVATE ../../main)
add_test(NAME pin_rotation COMMAND test_pin_rotation)

add_executable(test_audio_protocol
  test_audio_protocol.cpp
  ../../main/product/audio_protocol.cpp
)
target_include_directories(test_audio_protocol PRIVATE ../../main)
add_test(NAME audio_protocol COMMAND test_audio_protocol)

add_executable(test_ima_adpcm
  test_ima_adpcm.cpp
  ../../main/product/ima_adpcm.cpp
)
target_include_directories(test_ima_adpcm PRIVATE ../../main)
add_test(NAME ima_adpcm COMMAND test_ima_adpcm)

add_executable(test_microphone_state
  test_microphone_state.cpp
  ../../main/product/microphone_state.cpp
)
target_include_directories(test_microphone_state PRIVATE ../../main)
add_test(NAME microphone_state COMMAND test_microphone_state)

add_executable(test_audio_capture
  test_audio_capture.cpp
  ../../main/product/audio_capture.cpp
)
target_include_directories(test_audio_capture PRIVATE ../../main)
add_test(NAME audio_capture COMMAND test_audio_capture)

add_executable(test_ble_audio_transport
  test_ble_audio_transport.cpp
  ../../main/product/ble_audio_transport.cpp
  ../../main/probe/ble_services.cpp
)
target_include_directories(test_ble_audio_transport PRIVATE ../../main)
add_test(NAME ble_audio_transport COMMAND test_ble_audio_transport)

add_executable(test_microphone_controller
  test_microphone_controller.cpp
  ../../main/product/microphone_controller.cpp
  ../../main/product/microphone_state.cpp
  ../../main/product/audio_protocol.cpp
  ../../main/product/ima_adpcm.cpp
)
target_include_directories(test_microphone_controller PRIVATE ../../main)
add_test(NAME microphone_controller COMMAND test_microphone_controller)
