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
  return_to_pet,
};

struct UiNavigationResult {
  bool captured = false;
  UiNavAction action = UiNavAction::none;
};

enum class UiInteractionContext : uint8_t {
  normal,
  settings_browse,
  settings_modal,
};

class UiNavigation {
 public:
  UiNavigationResult on_key(uint8_t physical_key, bool pressed,
                            bool fn_pressed,
                            UiInteractionContext context =
                                UiInteractionContext::normal);
  UiNavigationResult on_return_key(
      uint8_t physical_key,
      bool pressed,
      bool enabled);

 private:
  std::array<bool, kPhysicalKeyCount> captured_{};
};
