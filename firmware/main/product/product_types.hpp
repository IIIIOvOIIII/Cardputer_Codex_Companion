#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

inline constexpr std::size_t kPhysicalKeyCount = 56;
inline constexpr std::string_view kProductName = "Cardputer Codex Companion";
inline constexpr std::string_view kProductVersion = "1.0.0";

enum class BootStage : uint8_t {
  display,
  config,
  keyboard,
  ble,
  wifi,
  web,
  companion,
};

enum class ServiceState : uint8_t {
  starting,
  ok,
  offline,
  error,
};

enum class InputMode : uint8_t {
  keyboard,
  codex_remote,
};

constexpr std::string_view to_string(BootStage stage) {
  switch (stage) {
    case BootStage::display:
      return "DISPLAY";
    case BootStage::config:
      return "CONFIG";
    case BootStage::keyboard:
      return "KEYBOARD";
    case BootStage::ble:
      return "BLE";
    case BootStage::wifi:
      return "WIFI";
    case BootStage::web:
      return "WEB";
    case BootStage::companion:
      return "COMPANION";
  }
  return "UNKNOWN";
}

constexpr std::string_view to_string(ServiceState state) {
  switch (state) {
    case ServiceState::starting:
      return "...";
    case ServiceState::ok:
      return "OK";
    case ServiceState::offline:
      return "OFFLINE";
    case ServiceState::error:
      return "ERROR";
  }
  return "ERROR";
}

constexpr std::string_view to_string(InputMode mode) {
  return mode == InputMode::keyboard ? "KEYBOARD" : "CODEX";
}
