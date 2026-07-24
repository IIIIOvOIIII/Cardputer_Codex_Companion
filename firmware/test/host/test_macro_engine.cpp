#include <cassert>
#include <string>
#include <vector>

#include "product/macro_engine.hpp"

struct RecordingMacroSink final : MacroSink {
  void send_hid(const HidReport& report) override { reports.push_back(report); }
  bool send_text(uint32_t operation_id, std::string_view text) override {
    last_operation = operation_id;
    last_text.assign(text);
    return true;
  }
  void device_action(DeviceAction action) override { last_device = action; }
  bool codex_action(CodexAction action) override {
    last_codex = action;
    return true;
  }
  std::vector<HidReport> reports;
  uint32_t last_operation = 0;
  std::string last_text;
  DeviceAction last_device = DeviceAction::none;
  CodexAction last_codex = CodexAction::none;
};

int main() {
  RecordingMacroSink sink;
  MacroEngine engine(sink);

  KeyAction chord;
  chord.kind = ActionKind::hid_chord;
  chord.modifiers = 0x09;
  chord.usages = {0x06, 0, 0, 0, 0, 0};
  chord.usage_count = 1;
  assert(engine.execute(chord) == MacroResult::ok);
  assert(sink.reports.size() == 2);
  assert(sink.reports[0].modifiers == 0x09);
  assert(sink.reports[0].keys[0] == 0x06);
  assert(sink.reports[1] == HidReport{});

  KeyAction text;
  text.kind = ActionKind::text_utf8;
  text.text = "你好，Codex";
  assert(engine.execute(text) == MacroResult::ok);
  assert(sink.last_text == "你好，Codex");
  assert(sink.last_operation != 0);

  text.text.assign(1025, 'x');
  assert(engine.execute(text) == MacroResult::invalid);
  assert(sink.reports.back() == HidReport{});
  return 0;
}
