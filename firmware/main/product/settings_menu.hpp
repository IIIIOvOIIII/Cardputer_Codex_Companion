#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "product/keymap.hpp"

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

  [[nodiscard]] bool active() const {
    return interaction_ != SettingsInteraction::inactive;
  }
  [[nodiscard]] SettingsInteraction interaction() const {
    return interaction_;
  }
  [[nodiscard]] bool return_timeout_suspended() const {
    return interaction_ != SettingsInteraction::browse &&
           interaction_ != SettingsInteraction::inactive;
  }
  [[nodiscard]] std::string_view editor_value() const { return editor_; }
  [[nodiscard]] std::string masked_value() const;
  [[nodiscard]] std::string_view pin_value() const { return first_pin_; }
  [[nodiscard]] SettingsMenuContent content() const;

 private:
  enum class Editor : uint8_t { none, pin_first, pin_confirm, ssid, password };

  SettingsInputResult browse_key(uint8_t physical_key);
  SettingsInputResult edit_key(uint8_t physical_key, bool shift);
  static char key_character(uint8_t physical_key, bool shift);

  std::array<bool, kPhysicalKeyCount> captured_{};
  SettingsInteraction interaction_ = SettingsInteraction::inactive;
  Editor editor_kind_ = Editor::none;
  uint8_t selected_ = 0;
  uint8_t scroll_ = 0;
  std::string editor_;
  std::string first_pin_;
};

