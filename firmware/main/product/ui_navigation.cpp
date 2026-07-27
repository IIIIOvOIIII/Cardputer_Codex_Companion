#include "product/ui_navigation.hpp"

namespace {
UiNavAction action_for(uint8_t physical_key) {
  switch (physical_key) {
    case 39: return UiNavAction::scroll_up;
    case 52: return UiNavAction::previous_page;
    case 53: return UiNavAction::scroll_down;
    case 54: return UiNavAction::next_page;
    default: return UiNavAction::none;
  }
}
}  // namespace

UiNavigationResult UiNavigation::on_key(uint8_t physical_key, bool pressed,
                                        bool fn_pressed,
                                        UiInteractionContext context) {
  if (physical_key >= captured_.size()) return {};
  if (!pressed && captured_[physical_key]) {
    captured_[physical_key] = false;
    return {.captured = true, .action = UiNavAction::none};
  }
  const UiNavAction action = action_for(physical_key);
  if (pressed && context != UiInteractionContext::normal &&
      action != UiNavAction::none) {
    captured_[physical_key] = true;
    return {.captured = true, .action = UiNavAction::none};
  }
  if (pressed && fn_pressed && action != UiNavAction::none) {
    captured_[physical_key] = true;
    return {.captured = true, .action = action};
  }
  return {};
}

UiNavigationResult UiNavigation::on_return_key(
    uint8_t physical_key,
    bool pressed,
    bool enabled
) {
  constexpr uint8_t kBacktickKey = 0;
  if (physical_key != kBacktickKey) return {};
  if (!pressed && captured_[physical_key]) {
    captured_[physical_key] = false;
    return {.captured = true};
  }
  if (!pressed || !enabled) return {};
  captured_[physical_key] = true;
  return {
      .captured = true,
      .action = UiNavAction::return_to_pet,
  };
}
