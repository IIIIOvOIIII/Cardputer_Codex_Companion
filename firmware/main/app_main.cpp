#include "probe/ble_services.hpp"
#include "probe/hardware_probe.hpp"
#include "probe/keyboard_probe.hpp"
#include "probe/bounded_https_server.hpp"
#include "probe/pinned_wss_transport.hpp"
#include "probe/web_handlers.hpp"
#include "probe/resource_metrics.hpp"
#include "probe/probe_types.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <span>
#include <optional>
#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

extern "C" bool __attribute__((weak)) probe_identity_source(
    ProbeIdentity* identity) {
  (void)identity;
  return false;
}

namespace {
constexpr char kTag[] = "cardputer-codex-phase0";
constexpr char kEventType[] = "hardware_runtime";
constexpr uint32_t kInternalHeapCaps =
    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr uint32_t kMetricsTaskStackBytes = 8192;
constexpr TickType_t kMetricsPeriodTicks = pdMS_TO_TICKS(1000);

std::optional<KeyboardProbe> g_keyboard_probe;
std::atomic<uint32_t> g_allocation_failures = 0;
std::atomic<uint32_t> g_resource_sample_encode_failures = 0;
std::atomic<bool> g_missing_identity_logged = false;
std::array<char, 4096> g_resource_sample_line{};
StaticTask_t g_metrics_task_storage{};
std::array<StackType_t, kMetricsTaskStackBytes> g_metrics_task_stack{};
TaskHandle_t g_metrics_task = nullptr;

void handle_ble_disconnect() {
  if (g_keyboard_probe.has_value()) {
    g_keyboard_probe->on_ble_disconnected();
  }
}

void on_heap_allocation_failed(size_t,
                              uint32_t,
                              const char*) {
  g_allocation_failures.fetch_add(1, std::memory_order_relaxed);
}

bool any_byte_set(std::span<const uint8_t> bytes) {
  return std::any_of(bytes.begin(), bytes.end(),
                     [](uint8_t value) { return value != 0; });
}

bool complete_probe_identity(const ProbeIdentity& identity) {
  return any_byte_set(identity.run_id) &&
         any_byte_set(identity.boot_id) &&
         any_byte_set(identity.app_elf_sha256) &&
         any_byte_set(identity.firmware_image_sha256) &&
         any_byte_set(identity.device_id);
}

TaskStackMetric task_stack_metric(std::string_view name, uint32_t configured_bytes,
                                 TaskHandle_t handle) {
  return {
      .name = name.data(),
      .configured_bytes = configured_bytes,
      .high_water_free_bytes =
          handle == nullptr ? 0u : uxTaskGetStackHighWaterMark2(handle),
  };
}

void emit_resource_sample_if_identity_available() {
  ProbeIdentity identity{};
  if (!probe_identity_source(&identity) || !complete_probe_identity(identity)) {
    if (!g_missing_identity_logged.exchange(true, std::memory_order_relaxed)) {
      ESP_LOGW(kTag, "probe identity not supplied; metrics samples suppressed");
    }
    return;
  }

  const AdmissionSnapshot https_snapshot = bounded_https_server_snapshot();
  ResourceSample sample{};
  sample.identity = identity;
  if (mbedtls_sha256(identity.device_id.data(), identity.device_id.size(),
                     sample.device_id_sha256.data(), 0) != 0) {
    ESP_LOGE(kTag, "device identity digest failed");
    return;
  }
  sample.monotonic_us = static_cast<uint64_t>(esp_timer_get_time());
  const ProbeWebMetricsSnapshot web_metrics =
      probe_web_metrics_snapshot(sample.monotonic_us);
  sample.free_internal_heap = heap_caps_get_free_size(kInternalHeapCaps);
  sample.largest_internal_block =
      heap_caps_get_largest_free_block(kInternalHeapCaps);
  sample.allocation_failures =
      g_allocation_failures.load(std::memory_order_relaxed);
  sample.metrics_encode_failures =
      g_resource_sample_encode_failures.load(std::memory_order_relaxed);
  sample.https_established = https_snapshot.established;
  sample.https_pending_handshakes = https_snapshot.pending_handshakes;
  sample.hid_queue_failures =
      g_keyboard_probe.has_value() ? g_keyboard_probe->hid_queue_overflow_count()
                                  : 0;
  sample.network_queue_failures = web_metrics.network_queue_overflows;
  sample.display_queue_failures = 0;
  sample.hid = g_keyboard_probe.has_value() ? g_keyboard_probe->hid_latency_metrics()
                                            : HidLatencyMetrics{};
  sample.burst = web_metrics.burst;
  sample.tasks[0] = task_stack_metric("scanner", 0, xTaskGetHandle("scanner"));
  sample.tasks[1] = task_stack_metric("hid_sender", kHidSenderTaskStackBytes,
                                      xTaskGetHandle("keyboard-hid"));
#ifdef CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE
  constexpr uint32_t kNimbleHostStackBytes =
      CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE;
#else
  constexpr uint32_t kNimbleHostStackBytes = 0;
#endif
  sample.tasks[2] =
      task_stack_metric("nimble", kNimbleHostStackBytes,
                        xTaskGetHandle("nimble_host"));
  sample.tasks[3] =
      task_stack_metric("https", kHttpsServerTaskStackBytes,
                        bounded_https_server_task());
  sample.tasks[4] =
      task_stack_metric("wss", kPinnedWssClientTaskStackBytes,
                        xTaskGetHandle(kPinnedWssClientTaskName));
  sample.tasks[5] = task_stack_metric("display", 0, nullptr);
  sample.tasks[6] =
      task_stack_metric("metrics", kMetricsTaskStackBytes, g_metrics_task);

  g_resource_sample_line.fill('\0');
  const ResourceEncodeError encode_rc =
      emit_resource_sample_line(sample, "steady", g_resource_sample_line);
  if (encode_rc != ResourceEncodeError::none) {
    g_resource_sample_encode_failures.fetch_add(1, std::memory_order_relaxed);
    ESP_LOGE(kTag, "resource sample encode failed: %d", static_cast<int>(encode_rc));
    return;
  }

  std::printf("%s", g_resource_sample_line.data());
}

void metrics_task_entry(void*) {
  TickType_t next_wake = xTaskGetTickCount();
  while (true) {
    emit_resource_sample_if_identity_available();
    vTaskDelayUntil(&next_wake, kMetricsPeriodTicks);
  }
}
}  // namespace

extern "C" void ble_hid_task_start_up() {
  // Matrix scanning is owned by the firmware keyboard task, not the IDF demo.
}

extern "C" void app_main() {
  ESP_LOGI(kTag, "PHASE 0 / NOT FOR RELEASE");
  const std::span<const httpd_uri_t> web_routes = probe_web_handler_routes();
  ESP_LOGI(kTag, "web management probe routes linked: %u",
           static_cast<unsigned>(web_routes.size()));

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

  const esp_err_t register_rc =
      heap_caps_register_failed_alloc_callback(on_heap_allocation_failed);
  if (register_rc != ESP_OK) {
    ESP_LOGE(kTag, "failed to register heap allocation callback: %s",
             esp_err_to_name(register_rc));
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

  g_metrics_task = xTaskCreateStatic(
      metrics_task_entry, "metrics", kMetricsTaskStackBytes, nullptr,
      tskIDLE_PRIORITY + 1, g_metrics_task_stack.data(),
      &g_metrics_task_storage);
  if (g_metrics_task == nullptr) {
    ESP_LOGE(kTag, "metrics task initialization failed");
  }
}
