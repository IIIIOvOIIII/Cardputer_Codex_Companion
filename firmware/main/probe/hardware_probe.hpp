#pragma once

#include <cstdint>

struct HardwareRuntime {
  const char* chip_model;
  uint32_t chip_revision;
  uint32_t flash_jedec_id;
  uint32_t flash_bytes;
  uint32_t psram_bytes;
};

HardwareRuntime probe_hardware();
