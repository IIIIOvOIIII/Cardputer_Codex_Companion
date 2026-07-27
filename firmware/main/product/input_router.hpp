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

enum class InputDispatchTarget : uint8_t {
  ble_passkey,
  onboarding,
  return_to_pet,
  settings,
  navigation,
  local_only,
  pet_hid,
};

constexpr InputDispatchTarget input_dispatch_target(
    bool passkey_active,
    bool onboarding_active,
    bool return_to_pet,
    bool settings_active,
    bool navigation_captured,
    bool host_input_allowed
) {
  if (passkey_active) return InputDispatchTarget::ble_passkey;
  if (onboarding_active) return InputDispatchTarget::onboarding;
  if (return_to_pet) return InputDispatchTarget::return_to_pet;
  if (settings_active) return InputDispatchTarget::settings;
  if (navigation_captured) return InputDispatchTarget::navigation;
  return host_input_allowed ? InputDispatchTarget::pet_hid
                            : InputDispatchTarget::local_only;
}

class InputRouter {
 public:
  [[nodiscard]] HidReport route(
      const std::array<bool, kPhysicalKeyCount>& pressed) const;
  void toggle_mode();
  void set_mode(InputMode mode) { mode_ = mode; }
  void enter_safe_profile();
  void leave_safe_profile() { safe_profile_ = false; }
  [[nodiscard]] InputMode mode() const { return mode_; }
  [[nodiscard]] bool safe_profile() const { return safe_profile_; }

 private:
  InputMode mode_ = InputMode::keyboard;
  bool safe_profile_ = false;
};
