#include <cassert>
#include <string>

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
  assert(menu.on_key(54, true, false, 120).captured);
  assert(menu.on_key(54, false, false, 121).captured);

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
  return 0;
}
