#include "product/hil_serial_control.hpp"

#include <string_view>

namespace {

constexpr std::string_view kStartCommand = "HIL MIC START";
constexpr std::string_view kStopCommand = "HIL MIC STOP";
constexpr std::string_view kHidStartCommand = "HIL HID START";
constexpr std::string_view kHidStopCommand = "HIL HID STOP";

}  // namespace

HilMicrophoneCommand HilSerialCommandParser::consume(uint8_t byte) {
  if (byte != '\n') {
    if (length_ < buffer_.size()) {
      buffer_[length_++] = static_cast<char>(byte);
    } else {
      overflow_ = true;
    }
    return HilMicrophoneCommand::none;
  }

  if (overflow_) {
    reset();
    return HilMicrophoneCommand::none;
  }
  if (length_ != 0 && buffer_[length_ - 1] == '\r') {
    --length_;
  }
  const std::string_view line(buffer_.data(), length_);
  const HilMicrophoneCommand command =
      line == kStartCommand
          ? HilMicrophoneCommand::start
          : line == kStopCommand ? HilMicrophoneCommand::stop
          : line == kHidStartCommand ? HilMicrophoneCommand::hid_start
          : line == kHidStopCommand ? HilMicrophoneCommand::hid_stop
                                 : HilMicrophoneCommand::none;
  reset();
  return command;
}

void HilSerialCommandParser::reset() {
  length_ = 0;
  overflow_ = false;
}

bool HilHidBurst::start(bool hid_ready) {
  if (!hid_ready || active_) return false;
  generated_ = 0;
  next_due_us_ = 0;
  active_ = true;
  return true;
}

bool HilHidBurst::stop() {
  const bool was_active = active_;
  active_ = false;
  next_due_us_ = 0;
  return was_active;
}

std::optional<StableKeyEvent> HilHidBurst::next(uint64_t stable_at_us) {
  if (!active_ ||
      (next_due_us_ != 0 && stable_at_us < next_due_us_)) {
    return std::nullopt;
  }
  const StableKeyEvent event{
      .physical_key = 0,
      .pressed = generated_ % 2 == 0,
      .stable_at_us = stable_at_us,
  };
  next_due_us_ = stable_at_us + kHilHidBurstIntervalUs;
  ++generated_;
  if (generated_ == kHilHidBurstEvents) active_ = false;
  return event;
}

MicrophoneEventKind hil_microphone_event(HilMicrophoneCommand command,
                                         MicrophoneState state) {
  return hil_command_decision(command, state).event;
}

HilCommandDecision hil_command_decision(HilMicrophoneCommand command,
                                        MicrophoneState state) {
  if (command == HilMicrophoneCommand::start &&
      state == MicrophoneState::ready) {
    return {
        .event = MicrophoneEventKind::g0_click,
        .accepted = true,
    };
  }
  if (command == HilMicrophoneCommand::stop &&
      (state == MicrophoneState::starting ||
       state == MicrophoneState::live24 ||
       state == MicrophoneState::live16 ||
       state == MicrophoneState::stopping)) {
    return {
        .event = MicrophoneEventKind::g0_click,
        .accepted = true,
    };
  }
  return {};
}

bool pet_animation_allowed(MicrophoneState state) {
  return state != MicrophoneState::starting &&
         state != MicrophoneState::live24 &&
         state != MicrophoneState::live16 &&
         state != MicrophoneState::stopping;
}
