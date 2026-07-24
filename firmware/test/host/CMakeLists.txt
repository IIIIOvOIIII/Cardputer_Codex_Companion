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
