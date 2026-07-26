#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <array>

#include "probe/resource_metrics.hpp"
#include "probe/hid_engine.hpp"
#include "probe/web_guard.hpp"

#ifdef ESP_PLATFORM
#include "esp_hidd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#endif

inline constexpr uint32_t kHidSenderTaskStackBytes = 2048;

class KeyboardReportSink {
 public:
  virtual ~KeyboardReportSink() = default;
  virtual void send_report(const HidReport& report) = 0;
};

struct StableKeyEvent {
  uint8_t physical_key = 0;
  bool pressed = false;
  uint64_t stable_at_us = 0;
};

struct HidRuntimeSummary {
  uint32_t generated = 0;
  uint32_t queued = 0;
  uint32_t queue_failures = 0;
  uint32_t p95_upper_bound_us = 0;
};

#ifdef ESP_PLATFORM
class EspHidReportSink final : public KeyboardReportSink {
 public:
  explicit EspHidReportSink(esp_hidd_dev_t* hid_device = nullptr);
  void send_report(const HidReport& report) override;

 private:
  esp_hidd_dev_t* hid_device_;
};
#endif

class KeyboardProbe {
 public:
  explicit KeyboardProbe(KeyboardReportSink& report_sink);
#ifdef ESP_PLATFORM
  explicit KeyboardProbe(esp_hidd_dev_t* hid_device);
#endif

  void enqueue_stable_key_event(const StableKeyEvent& event);
  void send_complete_report(const HidReport& report);
  void release_all();
  void synthetic_10k_source_events(std::span<const StableKeyEvent> events);
  void abort_macro();
  void on_mode_changed();
  void on_ble_disconnected();
  void on_scanner_fault();
  void on_controlled_reboot();
  void observe_product_hid_event(int64_t stable_at_us,
                                 int64_t queued_at_us,
                                 bool queued_ok);
  void set_web_pairing_physical_sink(WebPairingPhysicalSink* sink);
  void on_physical_web_pairing_window(std::string_view eight_digit_code,
                                      uint64_t now_ms);
  void on_physical_web_pairing_confirmation(bool accepted, uint64_t now_ms);
  [[nodiscard]] HidLatencyMetrics hid_latency_metrics() const;
  [[nodiscard]] uint32_t hid_p95_upper_bound_us() const;
  [[nodiscard]] HidRuntimeSummary hid_runtime_summary() const;
  [[nodiscard]] uint32_t hid_queue_overflow_count() const;
#ifdef ESP_PLATFORM
  [[nodiscard]] TaskHandle_t hid_sender_task() const;
#endif

 private:
  void emit_stable_key_event(const StableKeyEvent& event);
#ifdef ESP_PLATFORM
  static void hid_sender_entry(void* argument);
  void hid_sender_loop();
  void begin_sender_queue_and_task();
#endif
  void send_report_for_usages(std::span<const uint8_t> usages);
  void release_state();
  void send_report(const HidReport& report);

  static constexpr size_t kMaxActiveUsages = 6;

  HidEngine engine_;
  std::array<uint8_t, kMaxActiveUsages> active_usages_{};
  uint8_t active_usage_count_ = 0;
#ifdef ESP_PLATFORM
  EspHidReportSink esp_report_sink_;
#endif
  KeyboardReportSink* report_sink_;
  WebPairingPhysicalSink* web_pairing_sink_ = nullptr;
  HidLatencyMetrics hid_latency_metrics_{};
#ifdef ESP_PLATFORM
  mutable portMUX_TYPE hid_metrics_lock_ = portMUX_INITIALIZER_UNLOCKED;
  QueueHandle_t hid_queue_ = nullptr;
  StaticQueue_t hid_queue_storage_{};
  std::array<uint8_t, sizeof(StableKeyEvent) * kHidQueueDepth> hid_queue_buffer_{};
  StaticTask_t hid_sender_task_storage_{};
  std::array<StackType_t, kHidSenderTaskStackBytes> hid_sender_stack_{};
  TaskHandle_t hid_sender_task_ = nullptr;
  uint32_t hid_queue_overflow_count_ = 0;
#endif
};
