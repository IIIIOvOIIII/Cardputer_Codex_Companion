#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "probe/hid_engine.hpp"

#ifdef ESP_PLATFORM
#include "esp_hidd.h"
#endif

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
  void synthetic_10k_source_events(std::span<const StableKeyEvent> events);
  void abort_macro();
  void on_mode_changed();
  void on_ble_disconnected();
  void on_scanner_fault();
  void on_controlled_reboot();

 private:
  void send_report_for_usages(std::span<const uint8_t> usages);
  void release_state();
  void send_report(const HidReport& report);

  HidEngine engine_;
  std::vector<uint8_t> active_usages_;
#ifdef ESP_PLATFORM
  EspHidReportSink esp_report_sink_;
#endif
  KeyboardReportSink* report_sink_;
};
