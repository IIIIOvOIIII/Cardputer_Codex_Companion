#include <cassert>
#include <string>
#include <string_view>

#include "product/hil_serial_control.hpp"

namespace {

HilMicrophoneCommand feed(HilSerialCommandParser& parser,
                          std::string_view value) {
  HilMicrophoneCommand result = HilMicrophoneCommand::none;
  for (const char byte : value) {
    const HilMicrophoneCommand next =
        parser.consume(static_cast<uint8_t>(byte));
    if (next != HilMicrophoneCommand::none) result = next;
  }
  return result;
}

}  // namespace

int main() {
  HilSerialCommandParser parser;
  assert(feed(parser, "HIL MIC START\n") ==
         HilMicrophoneCommand::start);
  assert(feed(parser, "HIL MIC STOP\r\n") ==
         HilMicrophoneCommand::stop);
  assert(feed(parser, "HIL HID START\n") ==
         HilMicrophoneCommand::hid_start);
  assert(feed(parser, "HIL HID STOP\n") ==
         HilMicrophoneCommand::hid_stop);
  assert(feed(parser, "HIL G0 CLICK\n") ==
         HilMicrophoneCommand::g0_click);
  assert(feed(parser, "HIL G0 CLICK\r\n") ==
         HilMicrophoneCommand::g0_click);
  assert(feed(parser, "HIL G0 ") ==
         HilMicrophoneCommand::none);
  assert(feed(parser, "CLICK\n") ==
         HilMicrophoneCommand::g0_click);
  assert(feed(parser, "HIL MIC START") ==
         HilMicrophoneCommand::none);
  assert(feed(parser, "\n") == HilMicrophoneCommand::start);
  assert(feed(parser, "HIL MIC START NOW\n") ==
         HilMicrophoneCommand::none);
  assert(feed(parser, "HIL G0 CLICK NOW\n") ==
         HilMicrophoneCommand::none);
  assert(feed(parser, std::string(80, 'A') + "\n") ==
         HilMicrophoneCommand::none);
  assert(feed(parser, "HIL MIC STOP\n") ==
         HilMicrophoneCommand::stop);

  HilHidBurst hid_burst;
  assert(!hid_burst.start(false));
  assert(hid_burst.start(true));
  assert(!hid_burst.start(true));
  assert(hid_burst.stop());
  assert(!hid_burst.active());
  assert(!hid_burst.stop());
  assert(hid_burst.start(true));
  for (uint16_t index = 0; index < kHilHidBurstEvents; ++index) {
    const uint64_t due =
        1'000 + static_cast<uint64_t>(index) * kHilHidBurstIntervalUs;
    const auto event = hid_burst.next(due);
    assert(event.has_value());
    assert(event->physical_key == 0);
    assert(event->pressed == (index % 2 == 0));
    assert(event->stable_at_us == due);
    if (index + 1 < kHilHidBurstEvents) {
      assert(!hid_burst.next(due + kHilHidBurstIntervalUs - 1)
                  .has_value());
    }
  }
  assert(!hid_burst.active());
  assert(hid_burst.generated() == kHilHidBurstEvents);
  assert(!hid_burst.next(2'001).has_value());

  assert(hil_microphone_event(HilMicrophoneCommand::start,
                              MicrophoneState::ready) ==
         MicrophoneEventKind::g0_click);
  assert(hil_microphone_event(HilMicrophoneCommand::start,
                              MicrophoneState::live24) ==
         MicrophoneEventKind::g0_ignored);
  assert(hil_microphone_event(HilMicrophoneCommand::stop,
                              MicrophoneState::live24) ==
         MicrophoneEventKind::g0_click);
  assert(hil_microphone_event(HilMicrophoneCommand::stop,
                              MicrophoneState::ready) ==
         MicrophoneEventKind::g0_ignored);

  const HilCommandDecision accepted = hil_command_decision(
      HilMicrophoneCommand::start, MicrophoneState::ready);
  assert(accepted.event == MicrophoneEventKind::g0_click);
  assert(accepted.accepted);

  const HilCommandDecision rejected = hil_command_decision(
      HilMicrophoneCommand::start, MicrophoneState::unavailable);
  assert(rejected.event == MicrophoneEventKind::g0_ignored);
  assert(!rejected.accepted);

  const HilCommandDecision stopped = hil_command_decision(
      HilMicrophoneCommand::stop, MicrophoneState::live16);
  assert(stopped.event == MicrophoneEventKind::g0_click);
  assert(stopped.accepted);

  const HilCommandDecision noop = hil_command_decision(
      HilMicrophoneCommand::stop, MicrophoneState::ready);
  assert(noop.event == MicrophoneEventKind::g0_ignored);
  assert(!noop.accepted);

  assert(!pet_animation_allowed(MicrophoneState::starting));
  assert(!pet_animation_allowed(MicrophoneState::live24));
  assert(!pet_animation_allowed(MicrophoneState::live16));
  assert(!pet_animation_allowed(MicrophoneState::stopping));
  assert(pet_animation_allowed(MicrophoneState::ready));
  assert(pet_animation_allowed(MicrophoneState::error));
  return 0;
}
