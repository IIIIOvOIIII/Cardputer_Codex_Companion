#include "probe/keyboard_probe.hpp"

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "esp_log.h"
#include "esp_hidd.h"
#include "esp_timer.h"
#endif

namespace {
constexpr uint8_t kKeyboardReportMapIndex = 0;
constexpr uint8_t kKeyboardReportId = 1;
constexpr char kTag[] = "keyboard-probe";
}  // namespace

#ifdef ESP_PLATFORM
EspHidReportSink::EspHidReportSink(esp_hidd_dev_t* hid_device)
    : hid_device_(hid_device) {}

void EspHidReportSink::send_report(const HidReport& report) {
  if (!hid_device_) {
    return;
  }

  const auto status = esp_hidd_dev_input_set(
      hid_device_,
      kKeyboardReportMapIndex,
      kKeyboardReportId,
      const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(&report)),
      sizeof(report));
  if (status != ESP_OK) {
    ESP_LOGW(kTag, "failed to send keyboard report: %s (%d)",
             esp_err_to_name(status), static_cast<int>(status));
  } else if (report.modifiers != 0 || report.keys[0] != 0 ||
             report.keys[1] != 0 || report.keys[2] != 0 ||
             report.keys[3] != 0 || report.keys[4] != 0 ||
             report.keys[5] != 0) {
    ESP_LOGI(kTag,
             "sent keyboard report mod=0x%02x keys=%02x,%02x,%02x,%02x,%02x,%02x",
             static_cast<unsigned>(report.modifiers),
             static_cast<unsigned>(report.keys[0]),
             static_cast<unsigned>(report.keys[1]),
             static_cast<unsigned>(report.keys[2]),
             static_cast<unsigned>(report.keys[3]),
             static_cast<unsigned>(report.keys[4]),
             static_cast<unsigned>(report.keys[5]));
  }
}
#endif

KeyboardProbe::KeyboardProbe(KeyboardReportSink& report_sink)
    : engine_(), active_usages_{}, report_sink_(&report_sink) {
#ifdef ESP_PLATFORM
  begin_sender_queue_and_task();
#endif
}

#ifdef ESP_PLATFORM
KeyboardProbe::KeyboardProbe(esp_hidd_dev_t* hid_device)
    : engine_(),
      active_usages_(),
      esp_report_sink_(hid_device),
      report_sink_(&esp_report_sink_) {
  begin_sender_queue_and_task();
}
#endif

void KeyboardProbe::send_report(const HidReport& report) {
  if (report_sink_ == nullptr) {
    return;
  }
  report_sink_->send_report(report);
}

void KeyboardProbe::emit_stable_key_event(const StableKeyEvent& event) {
  if (event.pressed) {
    bool present = false;
    for (size_t index = 0; index < active_usage_count_; ++index) {
      if (active_usages_[index] == event.physical_key) {
        present = true;
        break;
      }
    }
    if (!present) {
      if (active_usage_count_ >= active_usages_.size()) {
        release_state();
        return;
      }
      active_usages_[active_usage_count_++] = event.physical_key;
    }
  } else {
    size_t write_index = 0;
    for (size_t index = 0; index < active_usage_count_; ++index) {
      if (active_usages_[index] != event.physical_key) {
        active_usages_[write_index++] = active_usages_[index];
      }
    }
    active_usage_count_ = static_cast<uint8_t>(write_index);
  }

  send_report_for_usages(std::span<const uint8_t>(active_usages_.data(),
                                                  active_usage_count_));
}

void KeyboardProbe::send_report_for_usages(std::span<const uint8_t> usages) {
  const HidResult result = engine_.make_report(0, usages);
  if (result.error != HidError::none) {
    release_state();
    return;
  }

  send_report(result.report);
  send_report(engine_.release_all());
}

void KeyboardProbe::enqueue_stable_key_event(const StableKeyEvent& event) {
#ifdef ESP_PLATFORM
  if (hid_queue_ == nullptr) {
    portENTER_CRITICAL(&hid_metrics_lock_);
    hid_latency_metrics_.observe(event.stable_at_us, event.stable_at_us, false);
    ++hid_queue_overflow_count_;
    portEXIT_CRITICAL(&hid_metrics_lock_);
    return;
  }

  const int64_t queued_at_us = static_cast<int64_t>(esp_timer_get_time());
  const bool queued_ok = xQueueSend(hid_queue_, &event, 0) == pdTRUE;
  portENTER_CRITICAL(&hid_metrics_lock_);
  hid_latency_metrics_.observe(event.stable_at_us, queued_at_us, queued_ok);
  if (!queued_ok) {
    ++hid_queue_overflow_count_;
  }
  portEXIT_CRITICAL(&hid_metrics_lock_);
#else
  emit_stable_key_event(event);
  hid_latency_metrics_.observe(event.stable_at_us, event.stable_at_us, true);
#endif
}

void KeyboardProbe::send_complete_report(const HidReport& report) {
  send_report(report);
}

void KeyboardProbe::release_all() {
  release_state();
}

void KeyboardProbe::synthetic_10k_source_events(
    std::span<const StableKeyEvent> events) {
  for (const StableKeyEvent& event : events) {
    enqueue_stable_key_event(event);
  }
}

void KeyboardProbe::release_state() {
  active_usage_count_ = 0;
  send_report(engine_.release_all());
}

void KeyboardProbe::abort_macro() {
  release_state();
}

void KeyboardProbe::on_mode_changed() {
  release_state();
}

void KeyboardProbe::on_ble_disconnected() {
  release_state();
}

void KeyboardProbe::on_scanner_fault() {
  release_state();
}

void KeyboardProbe::on_controlled_reboot() {
  release_state();
}

void KeyboardProbe::set_web_pairing_physical_sink(
    WebPairingPhysicalSink* sink) {
  web_pairing_sink_ = sink;
}

void KeyboardProbe::on_physical_web_pairing_window(
    std::string_view eight_digit_code, uint64_t now_ms) {
  if (web_pairing_sink_ != nullptr) {
    web_pairing_sink_->open_pairing_window(eight_digit_code, now_ms);
  }
}

void KeyboardProbe::on_physical_web_pairing_confirmation(bool accepted,
                                                        uint64_t now_ms) {
  if (web_pairing_sink_ != nullptr) {
    web_pairing_sink_->confirm_pairing(accepted, now_ms);
  }
}

HidLatencyMetrics KeyboardProbe::hid_latency_metrics() const {
#ifdef ESP_PLATFORM
  portENTER_CRITICAL(&hid_metrics_lock_);
  const HidLatencyMetrics snapshot = hid_latency_metrics_;
  portEXIT_CRITICAL(&hid_metrics_lock_);
  return snapshot;
#else
  return hid_latency_metrics_;
#endif
}

uint32_t KeyboardProbe::hid_queue_overflow_count() const {
#ifdef ESP_PLATFORM
  portENTER_CRITICAL(&hid_metrics_lock_);
  const uint32_t snapshot = hid_queue_overflow_count_;
  portEXIT_CRITICAL(&hid_metrics_lock_);
  return snapshot;
#else
  return 0;
#endif
}

#ifdef ESP_PLATFORM
TaskHandle_t KeyboardProbe::hid_sender_task() const {
  return hid_sender_task_;
}

void KeyboardProbe::begin_sender_queue_and_task() {
  hid_queue_ = xQueueCreateStatic(
      kHidQueueDepth, sizeof(StableKeyEvent), hid_queue_buffer_.data(),
      &hid_queue_storage_);
  if (hid_queue_ == nullptr) {
    return;
  }

  hid_sender_task_ = xTaskCreateStatic(
      hid_sender_entry, "keyboard-hid", kHidSenderTaskStackBytes, this,
      tskIDLE_PRIORITY + 1, hid_sender_stack_.data(),
      &hid_sender_task_storage_);
}

void KeyboardProbe::hid_sender_entry(void* argument) {
  static_cast<KeyboardProbe*>(argument)->hid_sender_loop();
}

void KeyboardProbe::hid_sender_loop() {
  StableKeyEvent event;
  while (true) {
    if (xQueueReceive(hid_queue_, &event, portMAX_DELAY) == pdTRUE) {
      emit_stable_key_event(event);
    }
  }
}
#endif
