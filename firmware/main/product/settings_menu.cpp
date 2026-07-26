#include "product/settings_menu.hpp"

#include <algorithm>
#include <array>

namespace {
constexpr uint8_t kUpKey = 39;
constexpr uint8_t kDownKey = 53;
constexpr uint8_t kPreviousKey = 52;
constexpr uint8_t kNextKey = 54;
constexpr uint8_t kPhysicalEsc = 0;
constexpr uint8_t kPhysicalBackspace = 13;
constexpr uint8_t kPhysicalReturn = 41;
constexpr uint8_t kMenuItems = 7;

constexpr std::array<std::string_view, 3> kFixedRows{
    "KEYBOARD PROFILE", "CHANGE PIN", "BIND WIFI"};
}  // namespace

SettingsInputResult SettingsMenu::enter() {
  interaction_ = SettingsInteraction::browse;
  editor_kind_ = Editor::none;
  captured_ = {};
  selected_ = 0;
  scroll_ = 0;
  root_selected_ = 0;
  root_scroll_ = 0;
  screen_ = Screen::root;
  return {.captured = true, .command = SettingsCommandKind::release_hid};
}

void SettingsMenu::leave() {
  interaction_ = SettingsInteraction::inactive;
  editor_kind_ = Editor::none;
  editor_.clear();
  first_pin_.clear();
  first_ssid_.clear();
  result_.clear();
  screen_ = Screen::root;
  selected_ = root_selected_;
  scroll_ = root_scroll_;
  captured_ = {};
}

void SettingsMenu::cancel() {
  if (!active()) return;
  interaction_ = SettingsInteraction::browse;
  editor_kind_ = Editor::none;
  editor_.clear();
  first_pin_.clear();
  first_ssid_.clear();
  result_.clear();
  screen_ = Screen::root;
  selected_ = root_selected_;
  scroll_ = root_scroll_;
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
  first_ssid_.clear();
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

void SettingsMenu::set_result(std::string_view result) {
  result_ = std::string(result.substr(0, 28));
  interaction_ = SettingsInteraction::result;
}

void SettingsMenu::set_profile_choices(
    std::span<const std::string_view> ids,
    std::span<const std::string_view> names
) {
  profile_count_ = static_cast<uint8_t>(
      std::min<std::size_t>({ids.size(), names.size(), profile_ids_.size()}));
  for (uint8_t index = 0; index < profile_count_; ++index) {
    profile_ids_[index] = ids[index].substr(0, 8);
    profile_names_[index] = names[index].substr(0, 20);
  }
}

void SettingsMenu::set_wifi_choices(
    std::span<const std::string_view> ssids
) {
  const uint8_t count = static_cast<uint8_t>(
      std::min<std::size_t>(ssids.size(), wifi_ssids_.size() - 1));
  for (uint8_t index = 0; index < count; ++index) {
    wifi_ssids_[index] = ssids[index].substr(0, 32);
  }
  wifi_ssids_[count] = "HIDDEN NETWORK";
  wifi_count_ = count + 1;
  screen_ = Screen::wifi;
  selected_ = 0;
  scroll_ = 0;
  interaction_ = SettingsInteraction::browse;
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
  if (interaction_ == SettingsInteraction::browse &&
      physical_key != kUpKey && physical_key != kDownKey &&
      physical_key != kPreviousKey && physical_key != kNextKey &&
      physical_key != kPhysicalReturn &&
      physical_key != kPhysicalEsc) {
    return {};
  }
  captured_[physical_key] = true;
  if (interaction_ == SettingsInteraction::browse) {
    return browse_key(physical_key);
  }
  if (interaction_ == SettingsInteraction::text_edit ||
      interaction_ == SettingsInteraction::confirm) {
    return edit_key(physical_key, shift);
  }
  if (interaction_ == SettingsInteraction::result &&
      physical_key == kPhysicalEsc) {
    cancel();
  }
  return {.captured = true};
}

SettingsInputResult SettingsMenu::browse_key(uint8_t physical_key) {
  const uint8_t item_count =
      screen_ == Screen::root
          ? kMenuItems
          : screen_ == Screen::profiles ? profile_count_ : wifi_count_;
  if (item_count == 0) {
    screen_ = Screen::root;
    selected_ = root_selected_;
    scroll_ = root_scroll_;
    return {.captured = true};
  }
  if (physical_key == kUpKey) {
    selected_ = selected_ == 0 ? item_count - 1 : selected_ - 1;
  } else if (physical_key == kDownKey) {
    selected_ = static_cast<uint8_t>((selected_ + 1) % item_count);
  } else if (physical_key == kPhysicalEsc) {
    if (screen_ != Screen::root) {
      screen_ = Screen::root;
      selected_ = root_selected_;
      scroll_ = root_scroll_;
      return {.captured = true};
    }
    return {.captured = true,
            .command = SettingsCommandKind::return_to_pet};
  } else if (physical_key == kPreviousKey ||
             physical_key == kNextKey) {
    const int8_t direction =
        physical_key == kPreviousKey ? -1 : 1;
    if (screen_ != Screen::root) {
      selected_ = direction < 0
          ? (selected_ == 0 ? item_count - 1 : selected_ - 1)
          : static_cast<uint8_t>((selected_ + 1) % item_count);
    } else if (selected_ == 3) {
      const uint8_t value =
          static_cast<uint8_t>(device_settings_.brightness);
      device_settings_.brightness = static_cast<Brightness>(
          direction < 0 ? (value + 3) % 4 : (value + 1) % 4);
    } else if (selected_ == 4) {
      const uint8_t value =
          static_cast<uint8_t>(device_settings_.return_to_pet);
      device_settings_.return_to_pet = static_cast<ReturnToPet>(
          direction < 0 ? (value + 3) % 4 : (value + 1) % 4);
    } else if (selected_ == 5) {
      const uint8_t value =
          static_cast<uint8_t>(device_settings_.pet_frame_rate);
      device_settings_.pet_frame_rate = static_cast<PetFrameRate>(
          direction < 0 ? (value + 2) % 3 : (value + 1) % 3);
    }
  } else if (physical_key == kPhysicalReturn) {
    if (screen_ == Screen::profiles) {
      selected_profile_id_ = profile_ids_[selected_];
      interaction_ = SettingsInteraction::applying;
      return {.captured = true,
              .command = SettingsCommandKind::activate_profile};
    }
    if (screen_ == Screen::wifi) {
      if (selected_ + 1 == wifi_count_) {
        begin_ssid_edit();
      } else {
        first_ssid_ = wifi_ssids_[selected_];
        begin_password_edit();
      }
      return {.captured = true};
    }
    root_selected_ = selected_;
    root_scroll_ = scroll_;
    switch (selected_) {
      case 0:
        screen_ = Screen::profiles;
        selected_ = 0;
        scroll_ = 0;
        break;
      case 1:
        begin_pin_edit();
        break;
      case 2:
        interaction_ = SettingsInteraction::applying;
        return {.captured = true,
                .command = SettingsCommandKind::scan_wifi};
      case 3:
        interaction_ = SettingsInteraction::applying;
        return {.captured = true,
                .command = SettingsCommandKind::apply_display_settings,
                .device_settings = device_settings_};
      case 4:
        interaction_ = SettingsInteraction::applying;
        return {.captured = true,
                .command = SettingsCommandKind::apply_display_settings,
                .device_settings = device_settings_};
      case 5:
        interaction_ = SettingsInteraction::applying;
        return {.captured = true,
                .command = SettingsCommandKind::apply_display_settings,
                .device_settings = device_settings_};
      default:
        interaction_ = SettingsInteraction::result;
        break;
    }
  }
  constexpr uint8_t visible = 5;
  if (selected_ < scroll_) scroll_ = selected_;
  if (selected_ >= scroll_ + visible) scroll_ = selected_ - visible + 1;
  if (screen_ == Screen::root) {
    root_selected_ = selected_;
    root_scroll_ = scroll_;
  }
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
    if (editor_kind_ == Editor::ssid) {
      if (!editor_.empty()) {
        first_ssid_ = editor_;
        begin_password_edit();
      }
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
  if (interaction_ == SettingsInteraction::text_edit ||
      interaction_ == SettingsInteraction::confirm) {
    output.count = 3;
    if (editor_kind_ == Editor::pin_first) {
      output.lines[0] = "NEW PIN (8 DIGITS)";
    } else if (editor_kind_ == Editor::pin_confirm) {
      output.lines[0] = "CONFIRM NEW PIN";
    } else if (editor_kind_ == Editor::ssid) {
      output.lines[0] = "ENTER WIFI SSID";
    } else {
      output.lines[0] = "ENTER WIFI PASSWORD";
    }
    output.lines[1] = masked_value();
    output.lines[2] = "ENTER=OK ESC=CANCEL";
    return output;
  }
  if (interaction_ == SettingsInteraction::applying) {
    output.count = 2;
    output.lines[0] = "APPLYING...";
    output.lines[1] = "PLEASE WAIT";
    return output;
  }
  if (interaction_ == SettingsInteraction::result) {
    output.count = 2;
    output.lines[0] = result_.empty() ? "ABOUT" : result_;
    output.lines[1] = "ESC TO RETURN";
    return output;
  }
  if (screen_ == Screen::profiles) {
    output.count = profile_count_;
    output.selected = selected_;
    output.scroll = scroll_;
    for (uint8_t index = 0; index < profile_count_; ++index) {
      output.lines[index] =
          std::string(index == selected_ ? "> " : "  ") +
          profile_names_[index];
    }
    return output;
  }
  if (screen_ == Screen::wifi) {
    output.count = wifi_count_;
    output.selected = selected_;
    output.scroll = scroll_;
    for (uint8_t index = 0; index < wifi_count_; ++index) {
      output.lines[index] =
          std::string(index == selected_ ? "> " : "  ") +
          wifi_ssids_[index];
    }
    return output;
  }
  output.count = kMenuItems;
  output.selected = selected_;
  output.scroll = scroll_;
  std::array<std::string, kMenuItems> rows{};
  for (uint8_t index = 0; index < kFixedRows.size(); ++index) {
    rows[index] = kFixedRows[index];
  }
  constexpr std::array brightness{"25%", "50%", "75%", "100%"};
  constexpr std::array timeout{"OFF", "15S", "30S", "60S"};
  constexpr std::array fps{"2", "2.5", "3"};
  rows[3] = "BRIGHTNESS " +
            std::string(brightness[
                static_cast<uint8_t>(device_settings_.brightness)]);
  rows[4] = "RETURN TO PET " +
            std::string(timeout[
                static_cast<uint8_t>(device_settings_.return_to_pet)]);
  rows[5] = "PET FPS " +
            std::string(fps[
                static_cast<uint8_t>(device_settings_.pet_frame_rate)]);
  rows[6] = "ABOUT";
  for (uint8_t index = 0; index < kMenuItems; ++index) {
    output.lines[index] =
        std::string(index == selected_ ? "> " : "  ") + rows[index];
  }
  return output;
}
