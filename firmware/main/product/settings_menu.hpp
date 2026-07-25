#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <span>

#include "product/keymap.hpp"
#include "product/device_settings.hpp"

enum class SettingsCommandKind : uint8_t {
  none,
  release_hid,
  scan_wifi,
  rotate_pin,
  stage_wifi,
  activate_profile,
  apply_display_settings,
  return_to_pet,
};

struct SettingsInputResult {
  bool captured = false;
  SettingsCommandKind command = SettingsCommandKind::none;
};

enum class SettingsInteraction : uint8_t {
  inactive,
  browse,
  text_edit,
  confirm,
  applying,
  result,
};

struct SettingsMenuContent {
  std::array<std::string, 12> lines{};
  uint8_t count = 0;
  uint8_t selected = 0;
  uint8_t scroll = 0;
};

class SettingsMenu {
 public:
  SettingsInputResult enter();
  void leave();
  void cancel();
  SettingsInputResult on_key(
      uint8_t physical_key,
      bool pressed,
      bool shift,
      uint64_t now_ms
  );

  void begin_pin_edit();
  void begin_ssid_edit();
  void begin_password_edit();
  void finish_result();
  void set_result(std::string_view result);
  void set_profile_choices(
      std::span<const std::string_view> ids,
      std::span<const std::string_view> names);
  void set_wifi_choices(std::span<const std::string_view> ssids);
  void set_device_settings(const DeviceSettings& settings) {
    device_settings_ = settings;
  }

  [[nodiscard]] bool active() const {
    return interaction_ != SettingsInteraction::inactive;
  }
  [[nodiscard]] SettingsInteraction interaction() const {
    return interaction_;
  }
  [[nodiscard]] bool return_timeout_suspended() const {
    return interaction_ == SettingsInteraction::text_edit ||
           interaction_ == SettingsInteraction::confirm ||
           interaction_ == SettingsInteraction::applying;
  }
  [[nodiscard]] std::string_view editor_value() const { return editor_; }
  [[nodiscard]] std::string masked_value() const;
  [[nodiscard]] std::string_view pin_value() const { return first_pin_; }
  [[nodiscard]] std::string_view ssid_value() const { return first_ssid_; }
  [[nodiscard]] std::string_view password_value() const { return editor_; }
  [[nodiscard]] std::string_view profile_id() const {
    return selected_profile_id_;
  }
  [[nodiscard]] SettingsMenuContent content() const;
  [[nodiscard]] uint8_t selected() const { return selected_; }
  [[nodiscard]] const DeviceSettings& device_settings() const {
    return device_settings_;
  }

 private:
  enum class Editor : uint8_t { none, pin_first, pin_confirm, ssid, password };
  enum class Screen : uint8_t { root, profiles, wifi };

  SettingsInputResult browse_key(uint8_t physical_key);
  SettingsInputResult edit_key(uint8_t physical_key, bool shift);
  static char key_character(uint8_t physical_key, bool shift);

  std::array<bool, kPhysicalKeyCount> captured_{};
  SettingsInteraction interaction_ = SettingsInteraction::inactive;
  Editor editor_kind_ = Editor::none;
  uint8_t selected_ = 0;
  uint8_t scroll_ = 0;
  uint8_t root_selected_ = 0;
  uint8_t root_scroll_ = 0;
  Screen screen_ = Screen::root;
  std::string editor_;
  std::string first_pin_;
  std::string first_ssid_;
  std::string result_;
  std::string selected_profile_id_;
  std::array<std::string, 5> profile_ids_{};
  std::array<std::string, 5> profile_names_{};
  uint8_t profile_count_ = 0;
  std::array<std::string, 13> wifi_ssids_{};
  uint8_t wifi_count_ = 0;
  DeviceSettings device_settings_{};
};
