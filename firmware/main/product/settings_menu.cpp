#include "product/settings_menu.hpp"

#include <algorithm>
#include <array>

namespace {
constexpr uint8_t kUpKey = 39;
constexpr uint8_t kDownKey = 53;
constexpr uint8_t kBackKey = 52;
constexpr uint8_t kEnterKey = 54;
constexpr uint8_t kPhysicalEsc = 0;
constexpr uint8_t kPhysicalBackspace = 13;
constexpr uint8_t kPhysicalReturn = 41;
constexpr uint8_t kMenuItems = 7;

constexpr std::array<std::string_view, kMenuItems> kRows{
    "KEYBOARD PROFILE", "CHANGE PIN", "BIND WIFI", "BRIGHTNESS 75%",
    "RETURN TO PET 30S", "PET FPS 2.5", "ABOUT"};
}  // namespace

SettingsInputResult SettingsMenu::enter() {
  interaction_ = SettingsInteraction::browse;
  editor_kind_ = Editor::none;
  captured_ = {};
  selected_ = 0;
  scroll_ = 0;
  return {.captured = true, .command = SettingsCommandKind::release_hid};
}

void SettingsMenu::leave() {
  interaction_ = SettingsInteraction::inactive;
  editor_kind_ = Editor::none;
  editor_.clear();
  first_pin_.clear();
  captured_ = {};
}

void SettingsMenu::cancel() {
  if (!active()) return;
  interaction_ = SettingsInteraction::browse;
  editor_kind_ = Editor::none;
  editor_.clear();
  first_pin_.clear();
}

void SettingsMenu::begin_pin_edit() {
  if (!active()) return;
  interaction_ = SettingsInteraction::text_edit;
  editor_kind_ = Editor::pin_first;
  editor_.clear();
  first_pin_.clear();
}

void SettingsMenu::begin_ssid_edit() {
  if (!active()) return;
  interaction_ = SettingsInteraction::text_edit;
  editor_kind_ = Editor::ssid;
  editor_.clear();
}

void SettingsMenu::begin_password_edit() {
  if (!active()) return;
  interaction_ = SettingsInteraction::text_edit;
  editor_kind_ = Editor::password;
  editor_.clear();
}

void SettingsMenu::finish_result() {
  if (interaction_ == SettingsInteraction::applying ||
      interaction_ == SettingsInteraction::result) {
    cancel();
  }
}

SettingsInputResult SettingsMenu::on_key(
    uint8_t physical_key,
    bool pressed,
    bool shift,
    uint64_t
) {
  if (!active() || physical_key >= captured_.size()) return {};
  if (!pressed && captured_[physical_key]) {
    captured_[physical_key] = false;
    return {.captured = true};
  }
  if (!pressed) return {};
  if (captured_[physical_key]) return {.captured = true};
  captured_[physical_key] = true;
  if (interaction_ == SettingsInteraction::browse) {
    return browse_key(physical_key);
  }
  if (interaction_ == SettingsInteraction::text_edit ||
      interaction_ == SettingsInteraction::confirm) {
    return edit_key(physical_key, shift);
  }
  return {.captured = true};
}

SettingsInputResult SettingsMenu::browse_key(uint8_t physical_key) {
  if (physical_key == kUpKey) {
    selected_ = selected_ == 0 ? kMenuItems - 1 : selected_ - 1;
  } else if (physical_key == kDownKey) {
    selected_ = static_cast<uint8_t>((selected_ + 1) % kMenuItems);
  } else if (physical_key == kBackKey) {
    return {.captured = true,
            .command = SettingsCommandKind::return_to_pet};
  } else if (physical_key == kEnterKey) {
    switch (selected_) {
      case 0:
        return {.captured = true,
                .command = SettingsCommandKind::activate_profile};
      case 1:
        begin_pin_edit();
        break;
      case 2:
        return {.captured = true,
                .command = SettingsCommandKind::scan_wifi};
      case 3:
      case 4:
      case 5:
        return {.captured = true,
                .command = SettingsCommandKind::apply_display_settings};
      default:
        interaction_ = SettingsInteraction::result;
        break;
    }
  }
  constexpr uint8_t visible = 5;
  if (selected_ < scroll_) scroll_ = selected_;
  if (selected_ >= scroll_ + visible) scroll_ = selected_ - visible + 1;
  return {.captured = true};
}

SettingsInputResult SettingsMenu::edit_key(
    uint8_t physical_key,
    bool shift
) {
  if (physical_key == kPhysicalEsc) {
    cancel();
    return {.captured = true};
  }
  if (physical_key == kPhysicalBackspace) {
    if (!editor_.empty()) editor_.pop_back();
    return {.captured = true};
  }
  if (physical_key == kPhysicalReturn) {
    if (editor_kind_ == Editor::pin_first) {
      if (editor_.size() == 8) {
        first_pin_ = editor_;
        editor_.clear();
        editor_kind_ = Editor::pin_confirm;
        interaction_ = SettingsInteraction::confirm;
      }
      return {.captured = true};
    }
    if (editor_kind_ == Editor::pin_confirm) {
      if (editor_.size() == 8 && editor_ == first_pin_) {
        interaction_ = SettingsInteraction::applying;
        return {.captured = true,
                .command = SettingsCommandKind::rotate_pin};
      }
      editor_.clear();
      return {.captured = true};
    }
    interaction_ = SettingsInteraction::applying;
    return {
        .captured = true,
        .command = editor_kind_ == Editor::password
                       ? SettingsCommandKind::stage_wifi
                       : SettingsCommandKind::none,
    };
  }

  const char character = key_character(physical_key, shift);
  if (character == '\0') return {.captured = true};
  if ((editor_kind_ == Editor::pin_first ||
       editor_kind_ == Editor::pin_confirm) &&
      (character < '0' || character > '9')) {
    return {.captured = true};
  }
  const std::size_t maximum =
      editor_kind_ == Editor::pin_first ||
              editor_kind_ == Editor::pin_confirm
          ? 8
          : editor_kind_ == Editor::ssid ? 32 : 64;
  if (editor_.size() < maximum) editor_.push_back(character);
  return {.captured = true};
}

char SettingsMenu::key_character(uint8_t physical_key, bool shift) {
  if (physical_key >= kPhysicalKeymap.size()) return '\0';
  const uint8_t usage = kPhysicalKeymap[physical_key].usage;
  if (usage >= 0x04 && usage <= 0x1d) {
    const char lower = static_cast<char>('a' + usage - 0x04);
    return shift ? static_cast<char>(lower - 'a' + 'A') : lower;
  }
  constexpr std::array<char, 10> plain_digits{
      '1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
  constexpr std::array<char, 10> shifted_digits{
      '!', '@', '#', '$', '%', '^', '&', '*', '(', ')'};
  if (usage >= 0x1e && usage <= 0x27) {
    const std::size_t index = usage - 0x1e;
    return shift ? shifted_digits[index] : plain_digits[index];
  }
  switch (usage) {
    case 0x2c: return ' ';
    case 0x2d: return shift ? '_' : '-';
    case 0x2e: return shift ? '+' : '=';
    case 0x2f: return shift ? '{' : '[';
    case 0x30: return shift ? '}' : ']';
    case 0x31: return shift ? '|' : '\\';
    case 0x33: return shift ? ':' : ';';
    case 0x34: return shift ? '"' : '\'';
    case 0x35: return shift ? '~' : '`';
    case 0x36: return shift ? '<' : ',';
    case 0x37: return shift ? '>' : '.';
    case 0x38: return shift ? '?' : '/';
    default: return '\0';
  }
}

std::string SettingsMenu::masked_value() const {
  if (editor_kind_ == Editor::password ||
      editor_kind_ == Editor::pin_first ||
      editor_kind_ == Editor::pin_confirm) {
    return std::string(editor_.size(), '*');
  }
  return editor_;
}

SettingsMenuContent SettingsMenu::content() const {
  SettingsMenuContent output;
  output.count = kMenuItems;
  output.selected = selected_;
  output.scroll = scroll_;
  for (uint8_t index = 0; index < kMenuItems; ++index) {
    output.lines[index] =
        std::string(index == selected_ ? "> " : "  ") +
        std::string(kRows[index]);
  }
  return output;
}

