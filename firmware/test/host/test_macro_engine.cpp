#include <cassert>
#include <string>
#include <vector>

#include "product/macro_engine.hpp"

struct RecordingMacroSink final : MacroSink {
  void send_hid(const HidReport& report) override {
    reports.push_back(report);
    events.push_back('H');
  }
  void delay_hid(uint16_t delay_ms) override {
    delays.push_back(delay_ms);
    events.push_back('D');
  }
  bool send_text(uint32_t operation_id, std::string_view text) override {
    last_operation = operation_id;
    last_text.assign(text);
    events.push_back('T');
    return true;
  }
  void device_action(DeviceAction action) override { last_device = action; }
  bool codex_action(CodexAction action) override {
    last_codex = action;
    return true;
  }
  std::vector<HidReport> reports;
  std::vector<uint16_t> delays;
  std::vector<char> events;
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
  assert((sink.events == std::vector<char>{'H', 'D', 'H'}));
  assert(sink.delays[0] >= 20);

  RecordingMacroSink ascii_sink;
  MacroEngine ascii_engine(ascii_sink);
  KeyAction ascii_text;
  ascii_text.kind = ActionKind::text_utf8;
  ascii_text.text = "hI!";
  assert(ascii_engine.execute(ascii_text) == MacroResult::ok);
  assert(ascii_sink.last_text.empty());
  assert(ascii_sink.reports.size() == 6);
  assert(ascii_sink.reports[0].modifiers == 0);
  assert(ascii_sink.reports[0].keys[0] == 0x0b);
  assert(ascii_sink.reports[1] == HidReport{});
  assert(ascii_sink.reports[2].modifiers == 0x02);
  assert(ascii_sink.reports[2].keys[0] == 0x0c);
  assert(ascii_sink.reports[3] == HidReport{});
  assert(ascii_sink.reports[4].modifiers == 0x02);
  assert(ascii_sink.reports[4].keys[0] == 0x1e);
  assert(ascii_sink.reports[5] == HidReport{});
  assert((ascii_sink.events ==
          std::vector<char>{'H', 'D', 'H', 'D', 'H', 'D',
                            'H', 'D', 'H', 'D', 'H', 'D'}));

  RecordingMacroSink symbol_sink;
  MacroEngine symbol_engine(symbol_sink);
  KeyAction symbols;
  symbols.kind = ActionKind::text_utf8;
  symbols.text = "'@^";
  assert(symbol_engine.execute(symbols) == MacroResult::ok);
  assert(symbol_sink.reports[0].modifiers == 0);
  assert(symbol_sink.reports[0].keys[0] == 0x34);
  assert(symbol_sink.reports[2].modifiers == 0x02);
  assert(symbol_sink.reports[2].keys[0] == 0x1f);
  assert(symbol_sink.reports[4].modifiers == 0x02);
  assert(symbol_sink.reports[4].keys[0] == 0x23);

  RecordingMacroSink unicode_sink;
  MacroEngine unicode_engine(unicode_sink);
  KeyAction text;
  text.kind = ActionKind::text_utf8;
  text.text = "你好，Codex";
  assert(unicode_engine.execute(text) == MacroResult::ok);
  assert(unicode_sink.last_text == "你好，Codex");
  assert(unicode_sink.last_operation != 0);
  assert(unicode_sink.reports.empty());

  text.text.assign(1025, 'x');
  assert(unicode_engine.execute(text) == MacroResult::invalid);
  assert(unicode_sink.reports.back() == HidReport{});

  KeyAction sequence;
  sequence.kind = ActionKind::input_sequence;
  sequence.sequence.resize(2);
  sequence.sequence[0].kind = ActionKind::hid_chord;
  sequence.sequence[0].usages[0] = 0x07;
  sequence.sequence[0].usage_count = 1;
  sequence.sequence[1].kind = ActionKind::text_utf8;
  sequence.sequence[1].text = "继续";
  assert(engine.execute(sequence) == MacroResult::ok);
  assert(sink.last_text == "继续");
  assert(sink.reports.back() == HidReport{});
  return 0;
}
