#include "probe_controller.hpp"

void ProbeController::set(Service service, bool ready) {
  switch (service) {
    case Service::ble_hid:
      snapshot_.ble_hid = ready;
      break;
    case Service::encrypted_gatt:
      snapshot_.encrypted_gatt = ready;
      break;
    case Service::wifi:
      snapshot_.wifi = ready;
      break;
    case Service::https:
      snapshot_.https = ready;
      break;
    case Service::wss_authenticated:
      snapshot_.wss_authenticated = ready;
      break;
  }
}

ServiceSnapshot ProbeController::snapshot() const {
  return snapshot_;
}
