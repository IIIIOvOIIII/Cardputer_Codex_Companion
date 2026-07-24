#include "probe_controller.hpp"

void ProbeController::set(Service service, bool ready) {
  if (!ready && service != Service::wss_authenticated) {
    snapshot_.wss_authenticated = false;
  }

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
      if (!ready) {
        snapshot_.wss_authenticated = false;
      }
      break;
  }
}

void ProbeController::begin_wss_connection(uint64_t generation) {
  wss_connection_generation_ = generation;
  has_wss_connection_generation_ = true;
  snapshot_.wss_authenticated = false;
}

bool ProbeController::accept_wss_auth_ok(uint64_t generation) {
  if (!has_wss_connection_generation_ ||
      generation != wss_connection_generation_) {
    return false;
  }
  snapshot_.wss_authenticated = true;
  return true;
}

ServiceSnapshot ProbeController::snapshot() const {
  return snapshot_;
}
