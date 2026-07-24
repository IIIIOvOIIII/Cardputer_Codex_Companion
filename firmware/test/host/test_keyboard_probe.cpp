#include <array>
#include <cassert>
#include <string>
#include <vector>

#include "probe/keyboard_probe.hpp"

struct RecordingSink final : KeyboardReportSink {
  void send_report(const HidReport& report) override {
    reports.push_back(report);
  }

  std::vector<HidReport> reports;
};

struct RecordingPhysicalPairingSink final : WebPairingPhysicalSink {
  void open_pairing_window(std::string_view eight_digit_code,
                           uint64_t now_ms) override {
    code.assign(eight_digit_code);
    opened_at_ms = now_ms;
  }

  void confirm_pairing(bool accepted, uint64_t now_ms) override {
    confirmation = accepted;
    confirmed_at_ms = now_ms;
  }

  std::string code;
  uint64_t opened_at_ms = 0;
  uint64_t confirmed_at_ms = 0;
  bool confirmation = false;
};

int main() {
  RecordingSink sink;
  KeyboardProbe probe(sink);
  RecordingPhysicalPairingSink pairing;
  probe.set_web_pairing_physical_sink(&pairing);
  probe.on_physical_web_pairing_window("12345678", 1000);
  probe.on_physical_web_pairing_confirmation(true, 2000);
  assert(pairing.code == "12345678");
  assert(pairing.opened_at_ms == 1000);
  assert(pairing.confirmation);
  assert(pairing.confirmed_at_ms == 2000);

  probe.enqueue_stable_key_event(StableKeyEvent{.physical_key = 0x06, .pressed = true});
  assert(sink.reports.size() == 2);
  assert(sink.reports[0].modifiers == 0);
  assert(sink.reports[0].keys[0] == 0x06);
  assert(sink.reports[1] == HidReport{});

  sink.reports.clear();
  for (uint8_t key = 0x06; key <= 0x0B; ++key) {
    probe.enqueue_stable_key_event(StableKeyEvent{.physical_key = key, .pressed = true});
  }
  assert(sink.reports.back() == HidReport{});
  const size_t after_six = sink.reports.size();

  probe.enqueue_stable_key_event(StableKeyEvent{.physical_key = 0x0C, .pressed = true});
  assert(sink.reports.size() == after_six + 1);
  assert(sink.reports.back() == HidReport{});

  const size_t before_abort = sink.reports.size();
  probe.enqueue_stable_key_event(StableKeyEvent{.physical_key = 0x6D, .pressed = true});
  assert(sink.reports.size() == before_abort + 2);
  probe.abort_macro();
  assert(sink.reports.back() == HidReport{});
  probe.on_mode_changed();
  assert(sink.reports.back() == HidReport{});
  probe.on_ble_disconnected();
  assert(sink.reports.back() == HidReport{});
  probe.on_scanner_fault();
  assert(sink.reports.back() == HidReport{});
  probe.on_controlled_reboot();
  assert(sink.reports.back() == HidReport{});

  sink.reports.clear();
  std::vector<StableKeyEvent> synthetic_events;
  synthetic_events.reserve(10000);
  for (uint16_t i = 0; i < 10000; ++i) {
    synthetic_events.push_back(StableKeyEvent{
        .physical_key = static_cast<uint8_t>(0x40 + (i % 6)),
        .pressed = true});
  }
  probe.synthetic_10k_source_events(synthetic_events);
  assert(sink.reports.size() == 10000 * 2);
  assert(sink.reports.back() == HidReport{});

  return 0;
}
