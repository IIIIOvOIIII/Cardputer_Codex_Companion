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
