#include "product/hil_serial_control.hpp"

#include <string_view>

namespace {

constexpr std::string_view kStartCommand = "HIL MIC START";
constexpr std::string_view kStopCommand = "HIL MIC STOP";

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
                                 : HilMicrophoneCommand::none;
  reset();
  return command;
}

void HilSerialCommandParser::reset() {
  length_ = 0;
  overflow_ = false;
}

MicrophoneEventKind hil_microphone_event(HilMicrophoneCommand command,
                                         MicrophoneState state) {
  if (command == HilMicrophoneCommand::start &&
      state == MicrophoneState::ready) {
    return MicrophoneEventKind::g0_click;
  }
  if (command == HilMicrophoneCommand::stop &&
      (state == MicrophoneState::starting ||
       state == MicrophoneState::live24 ||
       state == MicrophoneState::live16 ||
       state == MicrophoneState::stopping)) {
    return MicrophoneEventKind::g0_click;
  }
  return MicrophoneEventKind::g0_ignored;
}

bool pet_animation_allowed(MicrophoneState state) {
  return state != MicrophoneState::starting &&
         state != MicrophoneState::live24 &&
         state != MicrophoneState::live16 &&
         state != MicrophoneState::stopping;
}
