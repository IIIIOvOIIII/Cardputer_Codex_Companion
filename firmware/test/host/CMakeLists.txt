cmake_minimum_required(VERSION 3.20)

project(phase0_firmware_host LANGUAGES CXX)
enable_testing()

add_executable(test_probe_controller
    test_probe_controller.cpp
    ../../main/probe/probe_controller.cpp
)

target_include_directories(test_probe_controller PRIVATE ../../main)

add_test(NAME probe_controller COMMAND test_probe_controller)
