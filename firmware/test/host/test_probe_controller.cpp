#include <cassert>

#include "probe/probe_controller.hpp"

int main() {
  ProbeController controller;
  controller.set(Service::ble_hid, true);
  controller.set(Service::encrypted_gatt, true);
  controller.set(Service::wifi, true);
  controller.set(Service::https, true);
  assert(!controller.snapshot().all_live());

  controller.set(Service::wss_authenticated, true);
  assert(controller.snapshot().all_live());

  controller.set(Service::wifi, false);
  const auto degraded = controller.snapshot();
  assert(!degraded.all_live());
  assert(degraded.ble_hid);
  assert(degraded.encrypted_gatt);
  return 0;
}
