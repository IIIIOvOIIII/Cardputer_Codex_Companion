#pragma once

#include <cstdint>

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
  void begin_wss_connection(uint64_t generation);
  [[nodiscard]] bool accept_wss_auth_ok(uint64_t generation);
  [[nodiscard]] ServiceSnapshot snapshot() const;

 private:
  ServiceSnapshot snapshot_;
  uint64_t wss_connection_generation_ = 0;
  bool has_wss_connection_generation_ = false;
};
