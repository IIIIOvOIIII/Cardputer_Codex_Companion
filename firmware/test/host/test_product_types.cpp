#include <cassert>
#include <string_view>

#include "product/product_types.hpp"

int main() {
  static_assert(kPhysicalKeyCount == 56);
  static_assert(kProductVersion == std::string_view{"1.0.19"});
  static_assert(kProductBootTitle == std::string_view{"CARDPUTER CODEX 1.0.19"});
  static_assert(kProductName == std::string_view{"Cardputer Codex Companion"});

  assert(to_string(BootStage::display) == "DISPLAY");
  assert(to_string(BootStage::companion) == "COMPANION");
  assert(to_string(ServiceState::ok) == "OK");
  assert(to_string(ServiceState::offline) == "OFFLINE");
  assert(to_string(InputMode::keyboard) == "KEYBOARD");
  assert(to_string(InputMode::codex_remote) == "CODEX");
  return 0;
}
