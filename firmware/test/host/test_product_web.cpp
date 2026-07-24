#include <cassert>
#include <string_view>

#include "product/product_web.hpp"

int main() {
  assert(kProductWebUsesTls);
  assert(product_web_pin_is_valid("12345678"));
  assert(!product_web_pin_is_valid("1234567"));
  assert(!product_web_pin_is_valid("123456789"));
  assert(!product_web_pin_is_valid("1234abcd"));
  assert(!product_web_pin_is_valid(""));
  assert(product_web_pin_load_action(true, true, true) ==
         ProductWebPinLoadAction::use_stored);
  assert(product_web_pin_load_action(true, false, false) ==
         ProductWebPinLoadAction::generate_and_persist);
  assert(product_web_pin_load_action(true, true, false) ==
         ProductWebPinLoadAction::generate_and_persist);
  assert(product_web_pin_load_action(false, false, false) ==
         ProductWebPinLoadAction::generate_ephemeral);
  assert(product_web_binding_uses_sparse_null(ActionKind::passthrough));
  assert(!product_web_binding_uses_sparse_null(ActionKind::text_utf8));
  assert(!product_web_binding_uses_sparse_null(ActionKind::hid_chord));
  assert(product_web_profile_activation(true) ==
         ProductWebProfileActivation::replace_active);
  assert(product_web_profile_activation(false) ==
         ProductWebProfileActivation::keep_active);
  assert(product_web_companion_needs_snapshot(ServiceState::offline));
  assert(product_web_companion_needs_snapshot(ServiceState::starting));
  assert(!product_web_companion_needs_snapshot(ServiceState::ok));
  assert(kProductWebRoutes.size() == 8);
  assert(kProductWebRoutes[0].path == "/");
  assert(kProductWebRoutes[1].path == "/api/v1/status");
  assert(kProductWebRoutes[2].path == "/api/v1/profile");
  assert(kProductWebRoutes[3].path == "/api/v1/profile");
  assert(kProductWebRoutes[4].path == "/api/v1/wifi");
  assert(kProductWebRoutes[5].path == "/api/v1/pin");
  assert(kProductWebRoutes[6].path == "/api/v1/companion/status");
  assert(kProductWebRoutes[7].path == "/api/v1/companion/action");
  assert(!kProductWebRoutes[0].requires_pairing);
  assert(!kProductWebRoutes[1].requires_pairing);
  assert(kProductWebRoutes[2].requires_pairing);
  assert(kProductWebRoutes[6].requires_pairing);
  assert(kProductWebRoutes[7].requires_pairing);
  return 0;
}
