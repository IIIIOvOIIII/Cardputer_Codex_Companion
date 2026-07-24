#pragma once

#include <cstddef>
#include <cstdint>

#ifdef ESP_PLATFORM
#include "esp_err.h"

struct DeviceTlsIdentity {
  const uint8_t* certificate = nullptr;
  std::size_t certificate_length = 0;
  const uint8_t* private_key = nullptr;
  std::size_t private_key_length = 0;
};

esp_err_t load_or_create_device_tls_identity(DeviceTlsIdentity* identity);
#endif
