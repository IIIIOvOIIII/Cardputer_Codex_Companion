#include <cassert>
#include <string>
#include <vector>

#include "product/g0_dual_action.hpp"

class RecordingG0Sink final : public G0DualActionSink {
 public:
  bool chord_result = true;
  std::vector<std::string> events;

  bool execute_chord(uint8_t modifiers, uint8_t usage) override {
    events.push_back(
        "chord:" + std::to_string(modifiers) + ":" +
        std::to_string(usage));
    return chord_result;
  }

  void toggle_microphone() override {
    events.emplace_back("mic");
  }
};

int main() {
  DeviceSettings disabled;
  RecordingG0Sink microphone_only;
  assert(execute_g0_dual_action(disabled, microphone_only) ==
         G0DualActionResult::microphone_only);
  assert(microphone_only.events ==
         std::vector<std::string>{"mic"});

  DeviceSettings enabled{
      .g0_chord_enabled = true,
      .g0_chord_modifiers = 0x05,
      .g0_chord_usage = 0x19,
  };
  RecordingG0Sink chord_then_microphone;
  assert(execute_g0_dual_action(enabled, chord_then_microphone) ==
         G0DualActionResult::chord_then_microphone);
  assert(chord_then_microphone.events ==
         (std::vector<std::string>{"chord:5:25", "mic"}));

  RecordingG0Sink failed_chord;
  failed_chord.chord_result = false;
  assert(execute_g0_dual_action(enabled, failed_chord) ==
         G0DualActionResult::chord_failed_microphone_toggled);
  assert(failed_chord.events ==
         (std::vector<std::string>{"chord:5:25", "mic"}));
  return 0;
}
