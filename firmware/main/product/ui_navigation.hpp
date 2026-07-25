#pragma once

#include <array>
#include <cstdint>

#include "product/keymap.hpp"

enum class UiNavAction : uint8_t {
  none,
  previous_page,
  next_page,
  scroll_up,
  scroll_down,
};

struct UiNavigationResult {
  bool captured = false;
  UiNavAction action = UiNavAction::none;
};

class UiNavigation {
 public:
  UiNavigationResult on_key(uint8_t physical_key, bool pressed,
                            bool fn_pressed);

 private:
  std::array<bool, kPhysicalKeyCount> captured_{};
};
