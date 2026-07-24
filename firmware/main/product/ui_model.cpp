#include "product/ui_model.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace {
std::string clipped(std::string_view value, std::size_t maximum) {
  return std::string(value.substr(0, std::min(value.size(), maximum)));
}
}  // namespace

void UiModel::set_stage(BootStage stage, ServiceState state) {
  BootStageStatus& status = boot_[stage_index(stage)];
  if (status.state == state && status.error_code == 0) return;
  status = {.state = state, .error_code = 0};
  ++revision_;
}

void UiModel::set_stage_error(BootStage stage, uint16_t error_code) {
  const BootStageStatus next{
      .state = ServiceState::error,
      .error_code = static_cast<uint16_t>(error_code % 1000),
  };
  BootStageStatus& status = boot_[stage_index(stage)];
  if (status.state == next.state && status.error_code == next.error_code) {
    return;
  }
  status = next;
  ++revision_;
}

std::string UiModel::boot_line(BootStage stage) const {
  const BootStageStatus status = boot_[stage_index(stage)];
  std::string line(to_string(stage));
  line.push_back(' ');
  if (status.state != ServiceState::error) {
    line.append(to_string(status.state));
    return line;
  }
  char error[5]{};
  const unsigned error_code =
      std::min<unsigned>(status.error_code, 999u);
  std::snprintf(error, sizeof(error), "E%03u",
                error_code);
  line.append(error);
  return line;
}

void UiModel::set_mode(InputMode mode) {
  if (mode_ == mode) return;
  mode_ = mode;
  ++revision_;
}

void UiModel::set_ble(ServiceState state) {
  if (ble_ == state) return;
  ble_ = state;
  ++revision_;
}

void UiModel::set_wifi(ServiceState state) {
  if (wifi_ == state) return;
  wifi_ = state;
  ++revision_;
}

void UiModel::set_companion(ServiceState state) {
  if (companion_ == state) return;
  companion_ = state;
  ++revision_;
}

void UiModel::set_profile(std::string_view profile) {
  std::string next = clipped(profile, 20);
  if (profile_ == next) return;
  profile_ = std::move(next);
  ++revision_;
}

void UiModel::set_web(std::string_view ipv4, std::string_view pairing_code) {
  std::string next_ipv4 = clipped(ipv4, 15);
  std::string next_pairing_code = clipped(pairing_code, 8);
  if (ipv4_ == next_ipv4 && pairing_code_ == next_pairing_code) return;
  ipv4_ = std::move(next_ipv4);
  pairing_code_ = std::move(next_pairing_code);
  ++revision_;
}

void UiModel::set_session(std::string_view title, std::string_view cwd,
                          std::string_view state, uint8_t approvals,
                          uint8_t inputs) {
  std::string next_title = clipped(title, 28);
  std::string next_cwd = clipped(cwd, 28);
  std::string next_state = clipped(state, 16);
  if (session_title_ == next_title && cwd_ == next_cwd &&
      session_state_ == next_state && approvals_ == approvals &&
      inputs_ == inputs) {
    return;
  }
  session_title_ = std::move(next_title);
  cwd_ = std::move(next_cwd);
  session_state_ = std::move(next_state);
  approvals_ = approvals;
  inputs_ = inputs;
  ++revision_;
}

std::string UiModel::runtime_text() const {
  char counts[24]{};
  std::snprintf(counts, sizeof(counts), " A:%u I:%u",
                static_cast<unsigned>(approvals_),
                static_cast<unsigned>(inputs_));
  std::string output;
  output.reserve(192);
  output.append("BLE:").append(to_string(ble_));
  output.append(" WIFI:").append(to_string(wifi_));
  output.append(" MAC:").append(to_string(companion_)).push_back('\n');
  output.append(to_string(mode_)).append(" / ").append(profile_).push_back('\n');
  output.append(ipv4_).append(" PIN:").append(pairing_code_).push_back('\n');
  output.append(session_title_).push_back('\n');
  output.append(cwd_).push_back('\n');
  output.append(session_state_).append(counts);
  return output;
}
