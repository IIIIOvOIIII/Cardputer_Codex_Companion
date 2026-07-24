#pragma once

#include <array>
#include <cstdint>

struct ProbeIdentity {
  std::array<uint8_t, 16> run_id;
  std::array<uint8_t, 16> boot_id;
  std::array<uint8_t, 32> app_elf_sha256;
  std::array<uint8_t, 32> firmware_image_sha256;
  std::array<uint8_t, 16> device_id;
};

struct ServiceSnapshot {
  bool ble_hid = false;
  bool encrypted_gatt = false;
  bool wifi = false;
  bool https = false;
  bool wss_authenticated = false;

  [[nodiscard]] bool all_live() const {
    return ble_hid && encrypted_gatt && wifi && https && wss_authenticated;
  }
};
