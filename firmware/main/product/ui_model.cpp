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

std::string_view microphone_state_name(UiMicrophoneState state) {
  switch (state) {
    case UiMicrophoneState::unavailable: return "OFFLINE";
    case UiMicrophoneState::ready: return "READY";
    case UiMicrophoneState::live24: return "LIVE24";
    case UiMicrophoneState::live16: return "LIVE16";
    case UiMicrophoneState::error: return "ERROR";
  }
  return "ERROR";
}

std::string_view storage_compatibility_label(
    StorageCompatibilityState state) {
  switch (state) {
    case StorageCompatibilityState::ready: return "READY";
    case StorageCompatibilityState::missing: return "MISSING";
    case StorageCompatibilityState::wrong_type: return "WRONG TYPE";
    case StorageCompatibilityState::too_small: return "TOO SMALL";
  }
  return "MISSING";
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
  ++pet_revision_;
}

void UiModel::set_wifi(ServiceState state) {
  if (wifi_ == state) return;
  wifi_ = state;
  ++revision_;
  ++pet_revision_;
}

void UiModel::set_companion(ServiceState state) {
  if (companion_ == state) return;
  companion_ = state;
  ++revision_;
  ++pet_revision_;
}

void UiModel::set_microphone(UiMicrophoneState state,
                             uint32_t sample_rate_hz,
                             uint8_t drop_percent,
                             std::string_view last_error,
                             uint64_t now_ms) {
  const uint8_t bounded_drop = std::min<uint8_t>(drop_percent, 100);
  const std::string next_error = clipped(last_error, 28);
  if (microphone_state_ == state &&
      microphone_sample_rate_hz_ == sample_rate_hz &&
      microphone_drop_percent_ == bounded_drop &&
      microphone_last_error_ == next_error) {
    return;
  }
  microphone_state_ = state;
  microphone_sample_rate_hz_ = sample_rate_hz;
  microphone_drop_percent_ = bounded_drop;
  microphone_last_error_ = next_error;
  if (state == UiMicrophoneState::error &&
      !next_error.empty() && next_error != "NONE") {
    microphone_error_ = next_error;
    microphone_error_until_ms_ = now_ms + 1000;
  }
  ++revision_;
  ++pet_revision_;
}

void UiModel::expire_microphone_error(uint64_t now_ms) {
  if (microphone_error_.empty() || now_ms < microphone_error_until_ms_) {
    return;
  }
  microphone_error_.clear();
  microphone_error_until_ms_ = 0;
  ++revision_;
  ++pet_revision_;
}

std::string_view UiModel::microphone_indicator() const {
  switch (microphone_state_) {
    case UiMicrophoneState::unavailable: return "MIC --";
    case UiMicrophoneState::ready: return "MIC READY";
    case UiMicrophoneState::live24: return "MIC 24K";
    case UiMicrophoneState::live16: return "MIC 16K";
    case UiMicrophoneState::error: return "MIC ERR";
  }
  return "MIC ERR";
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

void UiModel::set_codex(std::string_view model,
                        std::string_view thinking_level,
                        std::optional<bool> fast,
                        std::span<const CodexLimitUsage> limits) {
  const std::string next_model = clipped(model, 32);
  const std::string next_thinking = clipped(thinking_level, 16);
  const uint8_t next_count = static_cast<uint8_t>(
      std::min<std::size_t>(limits.size(), limits_.size()));
  bool same = codex_model_ == next_model &&
              thinking_level_ == next_thinking && fast_ == fast &&
              limit_count_ == next_count;
  for (uint8_t index = 0; same && index < next_count; ++index) {
    same = limits_[index] == limits[index];
  }
  if (same) return;
  codex_model_ = next_model;
  thinking_level_ = next_thinking;
  fast_ = fast;
  limits_ = {};
  std::copy_n(limits.begin(), next_count, limits_.begin());
  limit_count_ = next_count;
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
  ++pet_revision_;
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

void UiModel::set_storage_compatibility(
    StorageCompatibility compatibility) {
  if (storage_compatibility_.state == compatibility.state &&
      storage_compatibility_.size_bytes == compatibility.size_bytes) {
    return;
  }
  storage_compatibility_ = compatibility;
  ++revision_;
  ++pet_revision_;
}

void UiModel::set_heartbeat_age(uint32_t seconds) {
  if (heartbeat_age_seconds_ == seconds) return;
  heartbeat_age_seconds_ = seconds;
  if (page_ == UiPage::sync_status) ++revision_;
}

void UiModel::set_pet_sync_age(uint32_t seconds) {
  if (pet_sync_age_seconds_ == seconds) return;
  pet_sync_age_seconds_ = seconds;
  if (page_ == UiPage::sync_status) ++revision_;
}

void UiModel::set_settings_content(
    std::span<const std::string_view> rows,
    uint8_t selected,
    uint8_t scroll
) {
  const uint8_t count =
      static_cast<uint8_t>(std::min(rows.size(), settings_rows_.size()));
  std::array<std::string, 12> next{};
  for (uint8_t index = 0; index < count; ++index) {
    next[index] = clipped(rows[index], 32);
  }
  if (settings_rows_ == next && settings_count_ == count &&
      settings_selected_ == selected && settings_scroll_ == scroll) {
    return;
  }
  settings_rows_ = std::move(next);
  settings_count_ = count;
  settings_selected_ = selected;
  settings_scroll_ = scroll;
  if (page_ == UiPage::settings) scroll_offset_ = scroll;
  ++revision_;
}

void UiModel::show_onboarding(
    std::span<const std::string_view> rows,
    uint8_t selected,
    uint8_t scroll
) {
  const uint8_t count =
      static_cast<uint8_t>(std::min(rows.size(), onboarding_rows_.size()));
  std::array<std::string, 12> next{};
  for (uint8_t index = 0; index < count; ++index) {
    next[index] = clipped(rows[index], 32);
  }
  const bool changed =
      onboarding_rows_ != next || onboarding_count_ != count ||
      onboarding_selected_ != selected || onboarding_scroll_ != scroll ||
      page_ != UiPage::onboarding;
  if (!changed) return;
  onboarding_rows_ = std::move(next);
  onboarding_count_ = count;
  onboarding_selected_ = selected;
  onboarding_scroll_ = scroll;
  page_ = UiPage::onboarding;
  scroll_offset_ = scroll;
  ++revision_;
}

void UiModel::finish_onboarding() {
  if (page_ != UiPage::onboarding) return;
  page_ = UiPage::pet;
  scroll_offset_ = 0;
  ++revision_;
}

void UiModel::navigate(UiNavAction action) {
  if (page_ == UiPage::onboarding) return;
  constexpr uint8_t count = 5;
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
    const uint8_t visible_lines =
        page_ == UiPage::settings ? 5 : 6;
    const uint8_t maximum =
        content.count > visible_lines ? content.count - visible_lines : 0;
    if (scroll_offset_ < maximum) {
      ++scroll_offset_;
      ++revision_;
    }
  }
}

void UiModel::return_to_pet() {
  if (page_ == UiPage::pet && scroll_offset_ == 0) return;
  page_ = UiPage::pet;
  scroll_offset_ = 0;
  ++revision_;
}

UiPageContent UiModel::page_content() const {
  UiPageContent content;
  char value[96]{};
  switch (page_) {
    case UiPage::pet:
      if (!storage_compatibility_.ready()) {
        add_line(content, "PARTITION ERROR");
        add_line(
            content,
            std::string(
                storage_compatibility_label(storage_compatibility_.state)));
        add_line(content, "USE FACTORY 1.3.1");
        add_line(content, "OR LAUNCHER 2.8+");
      } else {
        add_line(content, std::string(pet_state_name(pet_state_)));
      }
      break;
    case UiPage::device_status:
      add_line(content, "VERSION:" + std::string(kProductVersion));
      add_line(content, "PIN:" + pairing_code_);
      add_line(
          content,
          "STORAGE:" +
              std::string(
                  storage_compatibility_label(
                      storage_compatibility_.state)));
      add_line(content, "BLE:" + std::string(compact_state(ble_)));
      add_line(content, "WIFI:" + std::string(compact_state(wifi_)));
      add_line(content, "AGENT:" + std::string(compact_state(companion_)));
      add_line(content, "MIC:" +
                            std::string(microphone_state_name(
                                microphone_state_)));
      break;
    case UiPage::codex_status:
      add_line(content, "SESSION:" + session_title_);
      if (!codex_model_.empty()) {
        add_line(content, "MODEL:" + codex_model_);
      }
      if (fast_.has_value()) {
        add_line(content, std::string("FAST:") +
                              (*fast_ ? "ON" : "OFF"));
      }
      if (!thinking_level_.empty()) {
        add_line(content, "THINKING:" + thinking_level_);
      }
      for (uint8_t index = 0; index < limit_count_; ++index) {
        const CodexLimitUsage& limit = limits_[index];
        std::string label;
        if (limit.scope == CodexLimitScope::spark) {
          label = "SPARK ";
        }
        label += limit.window == CodexLimitWindow::five_hours
                     ? "5H:"
                     : "WEEKLY:";
        std::snprintf(value, sizeof(value), "%u%%",
                      static_cast<unsigned>(limit.used_percent));
        add_line(content, label + value);
      }
      break;
    case UiPage::sync_status:
      add_line(content, "IP:" + ipv4_);
      std::snprintf(value, sizeof(value), "HEARTBEAT:%lus",
                    static_cast<unsigned long>(heartbeat_age_seconds_));
      add_line(content, value);
      std::snprintf(value, sizeof(value), "PET SYNC:%s %lus",
                    pet_sync_result_.c_str(),
                    static_cast<unsigned long>(pet_sync_age_seconds_));
      add_line(content, value);
      add_line(content, "PROFILE:" + profile_);
      break;
    case UiPage::settings:
      if (settings_count_ == 0) {
        add_line(content, "> KEYBOARD PROFILE");
        add_line(content, "  CHANGE PIN");
        add_line(content, "  BIND WIFI");
        add_line(content, "  BRIGHTNESS 75%");
        add_line(content, "  RETURN TO PET 30S");
        add_line(content, "  PET FPS 2.5");
        add_line(content, "  ABOUT");
      } else {
        for (uint8_t index = 0; index < settings_count_; ++index) {
          add_line(content, settings_rows_[index]);
        }
      }
      break;
    case UiPage::onboarding:
      for (uint8_t index = 0; index < onboarding_count_; ++index) {
        add_line(content, onboarding_rows_[index]);
      }
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
