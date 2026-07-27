#include <array>
#include <cassert>

#include "product/input_router.hpp"

int main() {
  assert(input_dispatch_target(true, true, true, true, true, true) ==
         InputDispatchTarget::ble_passkey);
  assert(input_dispatch_target(false, true, true, true, true, true) ==
         InputDispatchTarget::onboarding);
  assert(input_dispatch_target(false, false, true, true, true, true) ==
         InputDispatchTarget::return_to_pet);
  assert(input_dispatch_target(false, false, false, true, true, true) ==
         InputDispatchTarget::settings);
  assert(input_dispatch_target(false, false, false, false, true, true) ==
         InputDispatchTarget::navigation);
  assert(input_dispatch_target(false, false, false, false, false, false) ==
         InputDispatchTarget::local_only);
  assert(input_dispatch_target(false, false, false, false, false, true) ==
         InputDispatchTarget::pet_hid);

  assert(active_profile_layer(InputMode::keyboard, false) == 0);
  assert(active_profile_layer(InputMode::codex_remote, false) == 1);
  assert(active_profile_layer(InputMode::keyboard, true) == 2);
  assert(active_profile_layer(InputMode::codex_remote, true) == 3);

  InputRouter router;
  std::array<bool, kPhysicalKeyCount> pressed{};

  pressed[15] = true;  // Q
  HidReport report = router.route(pressed);
  assert(report.modifiers == 0);
  assert(report.keys[0] == 0x14);

  pressed.fill(false);
  pressed[28] = true;  // Fn
  pressed[1] = true;   // 1 -> F1
  report = router.route(pressed);
  assert(report.keys[0] == 0x3a);

  pressed.fill(false);
  pressed[42] = true;  // Ctrl
  pressed[47] = true;  // C
  report = router.route(pressed);
  assert(report.modifiers == 0x01);
  assert(report.keys[0] == 0x06);

  router.toggle_mode();
  assert(router.mode() == InputMode::codex_remote);
  assert(router.route(pressed) == HidReport{});
  router.enter_safe_profile();
  assert(router.mode() == InputMode::keyboard);
  assert(router.safe_profile());
  router.leave_safe_profile();
  assert(!router.safe_profile());
  return 0;
}
