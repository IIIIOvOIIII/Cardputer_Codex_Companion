#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "product/companion_protocol.hpp"
#include "product/product_types.hpp"
#include "product/pet_bundle.hpp"
#include "product/ui_navigation.hpp"

enum class UiPage : uint8_t {
  pet,
  device_status,
  codex_status,
  sync_status,
  settings,
};

constexpr bool ui_page_allows_host_input(UiPage page) {
  return page == UiPage::pet;
}

struct UiPageContent {
  std::array<std::string, 12> lines{};
  uint8_t count = 0;
};

struct BootStageStatus {
  ServiceState state = ServiceState::starting;
  uint16_t error_code = 0;
};

class UiModel {
 public:
  void set_stage(BootStage stage, ServiceState state);
  void set_stage_error(BootStage stage, uint16_t error_code);
  [[nodiscard]] std::string boot_line(BootStage stage) const;

  void set_mode(InputMode mode);
  void set_ble(ServiceState state);
  void set_wifi(ServiceState state);
  void set_companion(ServiceState state);
  void set_profile(std::string_view profile);
  void set_web(std::string_view ipv4, std::string_view pairing_code);
  void set_session(std::string_view title, std::string_view cwd,
                   std::string_view state, uint8_t approvals, uint8_t inputs);
  void set_codex(std::string_view model, std::string_view thinking_level,
                 std::optional<bool> fast,
                 std::span<const CodexLimitUsage> limits);
  void set_pet(std::string_view id, std::string_view digest,
               PetState state, std::string_view sync_result);
  void set_pet_storage(uint32_t used_bytes, uint16_t format_version);
  void set_heartbeat_age(uint32_t seconds);
  void set_pet_sync_age(uint32_t seconds);
  void set_settings_content(std::span<const std::string_view> rows,
                            uint8_t selected, uint8_t scroll);
  void navigate(UiNavAction action);
  void return_to_pet();
  [[nodiscard]] uint32_t revision() const { return revision_; }
  [[nodiscard]] std::string runtime_text() const;
  [[nodiscard]] UiPage page() const { return page_; }
  [[nodiscard]] uint8_t scroll_offset() const { return scroll_offset_; }
  [[nodiscard]] UiPageContent page_content() const;
  [[nodiscard]] PetState pet_state() const { return pet_state_; }
  [[nodiscard]] ServiceState ble() const { return ble_; }
  [[nodiscard]] ServiceState wifi() const { return wifi_; }
  [[nodiscard]] ServiceState companion() const { return companion_; }

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
  std::string codex_model_;
  std::string thinking_level_;
  std::optional<bool> fast_;
  std::array<CodexLimitUsage, 4> limits_{};
  uint8_t limit_count_ = 0;
  UiPage page_ = UiPage::pet;
  uint8_t scroll_offset_ = 0;
  std::string pet_id_ = "-";
  std::string pet_digest_ = "-";
  PetState pet_state_ = PetState::idle;
  std::string pet_sync_result_ = "none";
  uint32_t pet_storage_used_ = 0;
  uint16_t pet_format_version_ = 0;
  uint32_t heartbeat_age_seconds_ = 0;
  uint32_t pet_sync_age_seconds_ = 0;
  std::array<std::string, 12> settings_rows_{};
  uint8_t settings_count_ = 0;
  uint8_t settings_selected_ = 0;
  uint8_t settings_scroll_ = 0;
  uint32_t revision_ = 0;
};
