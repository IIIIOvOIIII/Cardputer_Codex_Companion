#include <cassert>
#include <array>
#include <string>
#include <string_view>

#include "product/settings_menu.hpp"

int main() {
  SettingsMenu menu;
  auto result = menu.enter();
  assert(result.captured);
  assert(result.command == SettingsCommandKind::release_hid);
  assert(menu.active());
  assert(menu.on_key(39, true, false, 100).captured);
  assert(menu.on_key(39, false, false, 101).captured);
  assert(menu.on_key(53, true, false, 110).captured);
  assert(menu.on_key(53, false, false, 111).captured);
  assert(menu.on_key(52, true, false, 120).captured);
  assert(menu.on_key(52, false, false, 121).captured);
  assert(menu.on_key(54, true, false, 122).captured);
  assert(menu.on_key(54, false, false, 123).captured);
  result = menu.on_key(0, true, false, 124);
  assert(result.command == SettingsCommandKind::return_to_pet);
  menu.on_key(0, false, false, 125);

  menu.begin_pin_edit();
  assert(menu.return_timeout_suspended());
  assert(menu.on_key(30, true, false, 200).captured);  // A ignored
  for (uint8_t key = 1; key <= 8; ++key) {
    assert(menu.on_key(key, true, false, 210 + key).captured);
    menu.on_key(key, false, false, 220 + key);
  }
  assert(menu.masked_value() == "********");
  menu.on_key(41, true, false, 240);  // Enter
  menu.on_key(41, false, false, 241);
  for (uint8_t key = 1; key <= 8; ++key) {
    menu.on_key(key, true, false, 250 + key);
    menu.on_key(key, false, false, 260 + key);
  }
  result = menu.on_key(41, true, false, 280);
  assert(result.command == SettingsCommandKind::rotate_pin);
  assert(menu.pin_value() == "12345678");

  menu.begin_password_edit();
  menu.on_key(52, true, false, 300);  // comma is text, not back
  menu.on_key(52, false, false, 301);
  menu.on_key(53, true, false, 302);
  menu.on_key(53, false, false, 303);
  menu.on_key(39, true, false, 304);
  menu.on_key(39, false, false, 305);
  menu.on_key(54, true, false, 306);
  menu.on_key(54, false, false, 307);
  assert(menu.editor_value() == ",.;/");
  menu.on_key(13, true, false, 308);  // Backspace
  menu.on_key(13, false, false, 309);
  assert(menu.editor_value() == ",.;");
  menu.on_key(0, true, false, 310);  // Esc cancels
  assert(menu.interaction() == SettingsInteraction::browse);

  menu.begin_ssid_edit();
  for (int count = 0; count < 40; ++count) {
    menu.on_key(30, true, false, 400 + count * 2);
    menu.on_key(30, false, false, 401 + count * 2);
  }
  assert(menu.editor_value().size() == 32);
  menu.cancel();
  menu.begin_password_edit();
  for (int count = 0; count < 70; ++count) {
    menu.on_key(30, true, true, 500 + count * 2);
    menu.on_key(30, false, true, 501 + count * 2);
  }
  assert(menu.editor_value().size() == 64);
  menu.cancel();

  menu.leave();
  assert(!menu.active());
  assert(!menu.on_key(39, true, false, 700).captured);

  SettingsMenu hierarchy;
  hierarchy.enter();
  constexpr std::array<std::string_view, 2> profile_ids{"SAFE", "CODE"};
  constexpr std::array<std::string_view, 2> profile_names{"Safe", "Coding"};
  hierarchy.set_profile_choices(profile_ids, profile_names);
  assert(hierarchy.content().lines[0].find("INPUT MODE") != std::string::npos);
  result = hierarchy.on_key(54, true, false, 790);
  assert(result.command == SettingsCommandKind::none);
  hierarchy.on_key(54, false, false, 791);
  result = hierarchy.on_key(41, true, false, 792);
  assert(result.command == SettingsCommandKind::apply_input_mode);
  assert(result.input_mode == InputMode::codex_remote);
  hierarchy.on_key(41, false, false, 793);
  hierarchy.set_result("INPUT MODE SAVED");
  hierarchy.on_key(0, true, false, 794);
  hierarchy.on_key(0, false, false, 795);
  hierarchy.on_key(53, true, false, 798);
  hierarchy.on_key(53, false, false, 799);
  hierarchy.on_key(41, true, false, 800);
  hierarchy.on_key(41, false, false, 801);
  assert(hierarchy.content().count == 2);
  result = hierarchy.on_key(41, true, false, 804);
  assert(result.command == SettingsCommandKind::activate_safe_profile);
  assert(hierarchy.profile_id() == "SAFE");
  hierarchy.on_key(41, false, false, 805);
  hierarchy.set_result("PROFILE ACTIVATED");
  hierarchy.on_key(0, true, false, 806);
  hierarchy.on_key(0, false, false, 807);
  assert(hierarchy.content().count == 8);

  hierarchy.on_key(53, true, false, 810);
  hierarchy.on_key(53, false, false, 811);
  hierarchy.on_key(53, true, false, 811);
  hierarchy.on_key(53, false, false, 811);
  result = hierarchy.on_key(41, true, false, 812);
  assert(result.command == SettingsCommandKind::scan_wifi);
  hierarchy.on_key(41, false, false, 813);
  constexpr std::array<std::string_view, 1> ssids{"Lynx WiFi"};
  hierarchy.set_wifi_choices(ssids);
  assert(hierarchy.content().count == 2);
  hierarchy.on_key(41, true, false, 814);
  hierarchy.on_key(41, false, false, 815);
  assert(hierarchy.interaction() == SettingsInteraction::text_edit);
  hierarchy.on_key(30, true, false, 816);
  hierarchy.on_key(30, false, false, 817);
  result = hierarchy.on_key(41, true, false, 818);
  assert(result.command == SettingsCommandKind::stage_wifi);
  assert(hierarchy.ssid_value() == "Lynx WiFi");
  assert(hierarchy.password_value() == "a");

  SettingsMenu display;
  display.enter();
  const DeviceSettings original = display.device_settings();
  for (int index = 0; index < 4; ++index) {
    display.on_key(53, true, false, 900 + index * 2);
    display.on_key(53, false, false, 901 + index * 2);
  }
  assert(display.selected() == 4);
  result = display.on_key(54, true, false, 910);
  assert(result.command == SettingsCommandKind::none);
  assert(display.device_settings().brightness != original.brightness);
  display.on_key(54, false, false, 911);
  result = display.on_key(41, true, false, 912);
  assert(result.command == SettingsCommandKind::apply_display_settings);
  assert(result.device_settings.brightness ==
         display.device_settings().brightness);
  return 0;
}
