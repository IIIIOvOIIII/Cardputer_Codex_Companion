#include "probe/keyboard_probe.hpp"

#include <algorithm>

namespace {
constexpr uint8_t kKeyboardReportMapIndex = 0;
constexpr uint8_t kKeyboardReportId = 1;
constexpr size_t kMaxActiveUsages = 6;
constexpr char kTag[] = "keyboard-probe";
}

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_hidd.h"

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
    ESP_LOGW(kTag, "failed to send keyboard report");
  }
}
#endif

KeyboardProbe::KeyboardProbe(KeyboardReportSink& report_sink)
    : engine_(), active_usages_{}, report_sink_(&report_sink) {}

#ifdef ESP_PLATFORM
KeyboardProbe::KeyboardProbe(esp_hidd_dev_t* hid_device)
    : engine_(),
      active_usages_(),
      esp_report_sink_(hid_device),
      report_sink_(&esp_report_sink_) {}
#endif

void KeyboardProbe::send_report(const HidReport& report) {
  if (report_sink_ == nullptr) {
    return;
  }
  report_sink_->send_report(report);
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
  if (event.pressed) {
    if (std::find(active_usages_.begin(), active_usages_.end(), event.physical_key) ==
        active_usages_.end()) {
      active_usages_.push_back(event.physical_key);
    }
  } else {
    active_usages_.erase(
        std::remove(active_usages_.begin(), active_usages_.end(), event.physical_key),
        active_usages_.end());
  }

  if (active_usages_.size() > kMaxActiveUsages) {
    release_state();
    return;
  }

  send_report_for_usages(active_usages_);
}

void KeyboardProbe::synthetic_10k_source_events(
    std::span<const StableKeyEvent> events) {
  for (const StableKeyEvent& event : events) {
    enqueue_stable_key_event(event);
  }
}

void KeyboardProbe::release_state() {
  active_usages_.clear();
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
