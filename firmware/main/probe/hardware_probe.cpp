#include "hardware_probe.hpp"

#include <cstdint>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"

namespace {
const char* chip_model_name(esp_chip_model_t model) {
  switch (model) {
    case CHIP_ESP32:
      return "ESP32";
    case CHIP_ESP32S2:
      return "ESP32-S2";
    case CHIP_ESP32S3:
      return "ESP32-S3";
    case CHIP_ESP32C3:
      return "ESP32-C3";
    case CHIP_ESP32C2:
      return "ESP32-C2";
    case CHIP_ESP32C6:
      return "ESP32-C6";
    case CHIP_ESP32H2:
      return "ESP32-H2";
    case CHIP_ESP32P4:
      return "ESP32-P4";
    default:
      return "UNKNOWN";
  }
}
}  // namespace

HardwareRuntime probe_hardware() {
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  uint32_t flash_jedec_id = 0;
  uint32_t flash_bytes = 0;
  const esp_err_t flash_id_result = esp_flash_read_id(nullptr, &flash_jedec_id);
  if (flash_id_result != ESP_OK) {
    flash_jedec_id = 0;
  }
  if (esp_flash_get_size(nullptr, &flash_bytes) != ESP_OK) {
    flash_bytes = 0;
  }

  const size_t psram_bytes = esp_psram_get_size();
  return {
    chip_model_name(chip_info.model),
    chip_info.revision,
    flash_jedec_id,
    flash_bytes,
    static_cast<uint32_t>(psram_bytes),
  };
}
