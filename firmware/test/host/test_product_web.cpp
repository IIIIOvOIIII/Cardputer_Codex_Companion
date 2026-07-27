#include <cassert>
#include <string_view>

#include "product/product_web.hpp"

int main() {
  assert(kProductWebUsesTls);
  assert(kProductWebTaskStackBytes == 4864);
  assert(kProductWebTlsCleanupWindowUs == 5'000'000);
  assert(product_web_resource_scenario(0) == "steady");
  assert(product_web_resource_scenario(1) == "tls_burst");
  assert(product_web_resource_scenario(3) == "tls_burst");
  assert(!product_web_tls_resource_window_active(0, 200, 200));
  assert(product_web_tls_resource_window_active(0, 199, 200));
  assert(product_web_tls_resource_window_active(1, 200, 0));
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
  const ProductWebMicrophoneStatus microphone{
      .state = ProductWebMicrophoneState::ready,
      .sample_rate_hz = 0,
      .drop_percent = 0,
      .last_error = ProductWebMicrophoneError::none,
  };
  const std::string microphone_json =
      product_web_microphone_json(microphone);
  assert(microphone_json ==
         "{\"state\":\"READY\",\"sample_rate_hz\":0,"
         "\"drop_percent\":0,\"last_error\":\"NONE\"}");
  assert(kProductWebRoutes.size() == 16);
  assert(kProductWebRoutes[0].path == "/");
  assert(kProductWebRoutes[1].path == "/api/v1/status");
  assert(kProductWebRoutes[2].path == "/api/v1/profile");
  assert(kProductWebRoutes[3].path == "/api/v1/profile");
  assert(kProductWebRoutes[4].path == "/api/v1/profile");
  assert(kProductWebRoutes[4].method == ProductHttpMethod::delete_);
  assert(kProductWebRoutes[5].path == "/api/v1/profiles");
  assert(kProductWebRoutes[5].method == ProductHttpMethod::get);
  assert(kProductWebRoutes[6].path == "/api/v1/profiles");
  assert(kProductWebRoutes[6].method == ProductHttpMethod::post);
  assert(kProductWebRoutes[7].path == "/api/v1/profile/activate");
  assert(kProductWebRoutes[7].method == ProductHttpMethod::post);
  assert(kProductWebRoutes[8].path == "/api/v1/wifi");
  assert(kProductWebRoutes[9].path == "/api/v1/pin");
  assert(kProductWebRoutes[10].path == "/api/v1/companion/status");
  assert(kProductWebRoutes[11].path == "/api/v1/companion/action");
  assert(kProductWebRoutes[12].path == "/api/v1/companion/pet/begin");
  assert(kProductWebRoutes[13].path == "/api/v1/companion/pet/chunk");
  assert(kProductWebRoutes[14].path == "/api/v1/companion/pet/commit");
  assert(kProductWebRoutes[15].path == "/api/v1/companion/pet");
  assert(!kProductWebRoutes[0].requires_pairing);
  assert(!kProductWebRoutes[1].requires_pairing);
  assert(kProductWebRoutes[2].requires_pairing);
  for (std::size_t index = 2; index < kProductWebRoutes.size(); ++index) {
    assert(kProductWebRoutes[index].requires_pairing);
  }
  for (const ProductWebRoute& route : kProductWebRoutes) {
    assert(route.path.find("microphone/start") == std::string_view::npos);
    assert(route.path.find("microphone/stop") == std::string_view::npos);
    assert(route.path.find("capture") == std::string_view::npos);
  }
  return 0;
}
