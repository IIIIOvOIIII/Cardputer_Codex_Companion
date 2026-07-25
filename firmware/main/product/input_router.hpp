#pragma once

#include <array>
#include <cstdint>

#include "probe/hid_engine.hpp"
#include "product/keymap.hpp"
#include "product/product_types.hpp"

constexpr uint8_t active_profile_layer(InputMode mode, bool fn_pressed) {
  return static_cast<uint8_t>((fn_pressed ? 2u : 0u) +
                              (mode == InputMode::codex_remote ? 1u : 0u));
}

class InputRouter {
 public:
  [[nodiscard]] HidReport route(
      const std::array<bool, kPhysicalKeyCount>& pressed) const;
  void toggle_mode();
  void enter_safe_profile();
  void leave_safe_profile() { safe_profile_ = false; }
  [[nodiscard]] InputMode mode() const { return mode_; }
  [[nodiscard]] bool safe_profile() const { return safe_profile_; }

 private:
  InputMode mode_ = InputMode::keyboard;
  bool safe_profile_ = false;
};
