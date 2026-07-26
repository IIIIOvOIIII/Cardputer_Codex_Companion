#include <cassert>
#include <vector>

#include "product/product_controller.hpp"

struct FakeStartup final : ProductStartupBackend {
  bool display() override { calls.push_back(BootStage::display); return true; }
  bool config() override { calls.push_back(BootStage::config); return true; }
  bool keyboard() override { calls.push_back(BootStage::keyboard); return true; }
  bool ble() override { calls.push_back(BootStage::ble); return true; }
  bool wifi() override { calls.push_back(BootStage::wifi); return false; }
  bool web() override { calls.push_back(BootStage::web); return true; }
  bool companion() override { calls.push_back(BootStage::companion); return false; }
  std::vector<BootStage> calls;
};

int main() {
  static_assert(kProductRuntimeHeapReserveBytes == 44 * 1024);
  FakeStartup startup;
  ProductController controller(startup);
  controller.start();
  assert(startup.calls.size() == 7);
  const std::array expected{
      BootStage::display, BootStage::config, BootStage::keyboard,
      BootStage::wifi, BootStage::web, BootStage::ble,
      BootStage::companion,
  };
  assert(startup.calls == std::vector<BootStage>(
                              expected.begin(), expected.end()));
  assert(controller.state(BootStage::display) == ServiceState::ok);
  assert(controller.state(BootStage::wifi) == ServiceState::offline);
  assert(controller.state(BootStage::web) == ServiceState::ok);
  assert(controller.state(BootStage::companion) == ServiceState::offline);
  return 0;
}
