#include "product/ui_model.hpp"

#include <algorithm>
#include <cstdio>

namespace {
std::string clipped(std::string_view value, std::size_t maximum) {
  return std::string(value.substr(0, std::min(value.size(), maximum)));
}
}  // namespace

void UiModel::set_stage(BootStage stage, ServiceState state) {
  boot_[stage_index(stage)] = {.state = state, .error_code = 0};
}

void UiModel::set_stage_error(BootStage stage, uint16_t error_code) {
  boot_[stage_index(stage)] = {
      .state = ServiceState::error,
      .error_code = static_cast<uint16_t>(error_code % 1000),
  };
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

void UiModel::set_profile(std::string_view profile) {
  profile_ = clipped(profile, 20);
}

void UiModel::set_web(std::string_view ipv4, std::string_view pairing_code) {
  ipv4_ = clipped(ipv4, 15);
  pairing_code_ = clipped(pairing_code, 8);
}

void UiModel::set_session(std::string_view title, std::string_view cwd,
                          std::string_view state, uint8_t approvals,
                          uint8_t inputs) {
  session_title_ = clipped(title, 28);
  cwd_ = clipped(cwd, 28);
  session_state_ = clipped(state, 16);
  approvals_ = approvals;
  inputs_ = inputs;
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
