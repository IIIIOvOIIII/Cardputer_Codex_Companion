#include "probe/ble_services.hpp"
#include "probe/hardware_probe.hpp"
#include "probe/keyboard_probe.hpp"

#include <cinttypes>
#include <cstdio>
#include <optional>

#include "esp_log.h"
#include "nvs_flash.h"

namespace {
constexpr char kTag[] = "cardputer-codex-phase0";
constexpr char kEventType[] = "hardware_runtime";
std::optional<KeyboardProbe> g_keyboard_probe;

void handle_ble_disconnect() {
  if (g_keyboard_probe.has_value()) {
    g_keyboard_probe->on_ble_disconnected();
  }
}
}  // namespace

extern "C" void ble_hid_task_start_up() {
  // Matrix scanning is owned by the firmware keyboard task, not the IDF demo.
}

extern "C" void app_main() {
  ESP_LOGI(kTag, "PHASE 0 / NOT FOR RELEASE");

  const HardwareRuntime runtime = probe_hardware();
  std::printf(
      "{\"type\":\"%s\",\"chip_model\":\"%s\",\"chip_revision\":%" PRIu32
      ",\"flash_jedec_id\":\"%06" PRIx32
      "\",\"flash_bytes\":%" PRIu32 ",\"psram_bytes\":%" PRIu32 "}\n",
      kEventType,
      runtime.chip_model,
      runtime.chip_revision,
      runtime.flash_jedec_id,
      runtime.flash_bytes,
      runtime.psram_bytes);

  const esp_err_t nvs_rc = nvs_flash_init();
  if (nvs_rc != ESP_OK) {
    ESP_LOGE(kTag, "NVS initialization failed: %s", esp_err_to_name(nvs_rc));
    return;
  }

  DeviceId device_id{};
  const esp_err_t identity_rc = load_or_create_device_id(&device_id);
  if (identity_rc != ESP_OK) {
    ESP_LOGE(kTag, "device identity initialization failed: %s",
             esp_err_to_name(identity_rc));
    return;
  }

  esp_hidd_dev_t* hid_device = nullptr;
  const esp_err_t ble_rc = initialize_ble(device_id, &hid_device);
  if (ble_rc != ESP_OK) {
    ESP_LOGE(kTag, "BLE initialization failed: %s", esp_err_to_name(ble_rc));
    return;
  }

  g_keyboard_probe.emplace(hid_device);
  set_ble_disconnect_handler(handle_ble_disconnect);
}
