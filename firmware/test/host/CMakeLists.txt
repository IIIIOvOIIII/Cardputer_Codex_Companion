cmake_minimum_required(VERSION 3.20)

project(phase0_firmware_host LANGUAGES CXX)
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

add_executable(test_ui_model
  test_ui_model.cpp
  ../../main/product/ui_model.cpp
)
target_include_directories(test_ui_model PRIVATE ../../main)
add_test(NAME ui_model COMMAND test_ui_model)

add_executable(test_keyboard_matrix
  test_keyboard_matrix.cpp
  ../../main/product/keyboard_matrix.cpp
)
target_include_directories(test_keyboard_matrix PRIVATE ../../main)
add_test(NAME keyboard_matrix COMMAND test_keyboard_matrix)

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
  ../../main/product/profile_store.cpp
)
target_include_directories(test_profile PRIVATE ../../main)
add_test(NAME profile COMMAND test_profile)

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
