#pragma once

#include "probe_types.hpp"

enum class Service {
  ble_hid,
  encrypted_gatt,
  wifi,
  https,
  wss_authenticated,
};

class ProbeController {
 public:
  void set(Service service, bool ready);
  [[nodiscard]] ServiceSnapshot snapshot() const;

 private:
  ServiceSnapshot snapshot_;
};
