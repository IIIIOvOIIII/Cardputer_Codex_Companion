#include "product/input_router.hpp"

HidReport InputRouter::route(
    const std::array<bool, kPhysicalKeyCount>& pressed) const {
  HidReport report{};
  if (mode_ != InputMode::keyboard) {
    return report;
  }

  bool fn = false;
  for (std::size_t key = 0; key < pressed.size(); ++key) {
    if (pressed[key] && kPhysicalKeymap[key].fn) {
      fn = true;
      break;
    }
  }

  std::size_t usage_index = 0;
  for (std::size_t key = 0; key < pressed.size(); ++key) {
    if (!pressed[key]) {
      continue;
    }
    const PhysicalKeySpec spec = kPhysicalKeymap[key];
    report.modifiers |= spec.modifier;
    const uint8_t usage = fn && spec.fn_usage != 0 ? spec.fn_usage : spec.usage;
    if (usage != 0 && usage_index < report.keys.size()) {
      report.keys[usage_index++] = usage;
    }
  }
  return report;
}

void InputRouter::toggle_mode() {
  mode_ = mode_ == InputMode::keyboard ? InputMode::codex_remote
                                       : InputMode::keyboard;
}

void InputRouter::enter_safe_profile() {
  safe_profile_ = true;
  mode_ = InputMode::keyboard;
}
