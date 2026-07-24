#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "product/product_types.hpp"

struct BootStageStatus {
  ServiceState state = ServiceState::starting;
  uint16_t error_code = 0;
};

class UiModel {
 public:
  void set_stage(BootStage stage, ServiceState state);
  void set_stage_error(BootStage stage, uint16_t error_code);
  [[nodiscard]] std::string boot_line(BootStage stage) const;

  void set_mode(InputMode mode) { mode_ = mode; }
  void set_ble(ServiceState state) { ble_ = state; }
  void set_wifi(ServiceState state) { wifi_ = state; }
  void set_companion(ServiceState state) { companion_ = state; }
  void set_profile(std::string_view profile);
  void set_web(std::string_view ipv4, std::string_view pairing_code);
  void set_session(std::string_view title, std::string_view cwd,
                   std::string_view state, uint8_t approvals, uint8_t inputs);
  [[nodiscard]] std::string runtime_text() const;

 private:
  static constexpr std::size_t stage_index(BootStage stage) {
    return static_cast<std::size_t>(stage);
  }

  std::array<BootStageStatus, 7> boot_{};
  InputMode mode_ = InputMode::keyboard;
  ServiceState ble_ = ServiceState::offline;
  ServiceState wifi_ = ServiceState::offline;
  ServiceState companion_ = ServiceState::offline;
  std::string profile_ = "SAFE";
  std::string ipv4_ = "0.0.0.0";
  std::string pairing_code_ = "--------";
  std::string session_title_ = "NO SESSION";
  std::string cwd_ = "-";
  std::string session_state_ = "OFFLINE";
  uint8_t approvals_ = 0;
  uint8_t inputs_ = 0;
};
