#include "product/ui_model.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace {
std::string clipped(std::string_view value, std::size_t maximum) {
  return std::string(value.substr(0, std::min(value.size(), maximum)));
}

std::string_view compact_state(ServiceState state) {
  switch (state) {
    case ServiceState::starting:
      return "...";
    case ServiceState::ok:
      return "OK";
    case ServiceState::offline:
      return "OFF";
    case ServiceState::error:
      return "ERR";
  }
  return "ERR";
}

std::string_view pet_state_name(PetState state) {
  switch (state) {
    case PetState::idle: return "IDLE";
    case PetState::working: return "WORKING";
    case PetState::waiting: return "WAITING";
    case PetState::review: return "REVIEW";
    case PetState::failed: return "FAILED";
  }
  return "IDLE";
}

void add_line(UiPageContent& content, std::string line) {
  if (content.count < content.lines.size()) {
    content.lines[content.count++] = std::move(line);
  }
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
  std::string next_title = clipped(title, 20);
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

void UiModel::set_pet(std::string_view id, std::string_view digest,
                      PetState state, std::string_view sync_result) {
  std::string next_id = clipped(id, 64);
  std::string next_digest = clipped(digest, 64);
  std::string next_result = clipped(sync_result, 24);
  if (pet_id_ == next_id && pet_digest_ == next_digest &&
      pet_state_ == state && pet_sync_result_ == next_result) {
    return;
  }
  pet_id_ = std::move(next_id);
  pet_digest_ = std::move(next_digest);
  pet_state_ = state;
  pet_sync_result_ = std::move(next_result);
  ++revision_;
}

void UiModel::set_pet_storage(uint32_t used_bytes, uint16_t format_version) {
  if (pet_storage_used_ == used_bytes &&
      pet_format_version_ == format_version) {
    return;
  }
  pet_storage_used_ = used_bytes;
  pet_format_version_ = format_version;
  ++revision_;
}

void UiModel::set_heartbeat_age(uint32_t seconds) {
  if (heartbeat_age_seconds_ == seconds) return;
  heartbeat_age_seconds_ = seconds;
  if (page_ == UiPage::connection) ++revision_;
}

void UiModel::set_pet_sync_age(uint32_t seconds) {
  if (pet_sync_age_seconds_ == seconds) return;
  pet_sync_age_seconds_ = seconds;
  if (page_ == UiPage::connection) ++revision_;
}

void UiModel::navigate(UiNavAction action) {
  constexpr uint8_t count = 4;
  if (action == UiNavAction::previous_page ||
      action == UiNavAction::next_page) {
    const uint8_t current = static_cast<uint8_t>(page_);
    page_ = static_cast<UiPage>(
        action == UiNavAction::next_page
            ? (current + 1) % count
            : (current + count - 1) % count);
    scroll_offset_ = 0;
    ++revision_;
    return;
  }
  if (action == UiNavAction::scroll_up) {
    if (scroll_offset_ > 0) {
      --scroll_offset_;
      ++revision_;
    }
    return;
  }
  if (action == UiNavAction::scroll_down) {
    const UiPageContent content = page_content();
    constexpr uint8_t visible_lines = 6;
    const uint8_t maximum =
        content.count > visible_lines ? content.count - visible_lines : 0;
    if (scroll_offset_ < maximum) {
      ++scroll_offset_;
      ++revision_;
    }
  }
}

UiPageContent UiModel::page_content() const {
  UiPageContent content;
  char value[96]{};
  switch (page_) {
    case UiPage::pet:
      add_line(content, std::string(pet_state_name(pet_state_)));
      break;
    case UiPage::connection:
      add_line(content, "BLE:" + std::string(compact_state(ble_)));
      add_line(content, "WIFI:" + std::string(compact_state(wifi_)));
      add_line(content, "MAC:" + std::string(compact_state(companion_)));
      add_line(content, "IP:" + ipv4_);
      std::snprintf(value, sizeof(value), "HEARTBEAT:%lus",
                    static_cast<unsigned long>(heartbeat_age_seconds_));
      add_line(content, value);
      std::snprintf(value, sizeof(value), "PET SYNC:%lus",
                    static_cast<unsigned long>(pet_sync_age_seconds_));
      add_line(content, value);
      add_line(content, "SYNC:" + pet_sync_result_);
      break;
    case UiPage::session:
      add_line(content, "TITLE:" + session_title_);
      add_line(content, "STATE:" + session_state_);
      add_line(content, "CWD:" + cwd_);
      std::snprintf(value, sizeof(value), "APPROVALS:%u",
                    static_cast<unsigned>(approvals_));
      add_line(content, value);
      std::snprintf(value, sizeof(value), "INPUTS:%u",
                    static_cast<unsigned>(inputs_));
      add_line(content, value);
      break;
    case UiPage::device:
      add_line(content, "FW:" + std::string(kProductVersion));
      add_line(content, "PROFILE:" + profile_);
      add_line(content, "PIN:" + pairing_code_);
      add_line(content, "PET:" + pet_id_);
      add_line(content, "SHA:" + clipped(pet_digest_, 12));
      std::snprintf(value, sizeof(value), "STORE:%lu",
                    static_cast<unsigned long>(pet_storage_used_));
      add_line(content, value);
      std::snprintf(value, sizeof(value), "FMT:%u",
                    static_cast<unsigned>(pet_format_version_));
      add_line(content, value);
      break;
  }
  return content;
}

std::string UiModel::runtime_text() const {
  char counts[24]{};
  std::snprintf(counts, sizeof(counts), " A:%u I:%u",
                static_cast<unsigned>(approvals_),
                static_cast<unsigned>(inputs_));
  std::string output;
  output.reserve(192);
  output.append("B:").append(compact_state(ble_));
  output.append(" W:").append(compact_state(wifi_));
  output.append(" M:").append(compact_state(companion_)).push_back('\n');
  output.append(to_string(mode_)).append(" / ").append(profile_).push_back('\n');
  output.append("IP:").append(ipv4_).push_back('\n');
  output.append("PIN:").append(pairing_code_).push_back('\n');
  output.append(session_title_).push_back('\n');
  output.append(session_state_).append(counts);
  return output;
}
