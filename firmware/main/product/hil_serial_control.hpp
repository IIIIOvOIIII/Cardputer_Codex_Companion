#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "product/microphone_state.hpp"

enum class HilMicrophoneCommand : uint8_t {
  none,
  start,
  stop,
};

class HilSerialCommandParser {
 public:
  HilMicrophoneCommand consume(uint8_t byte);
  void reset();

 private:
  static constexpr std::size_t kCapacity = 32;
  std::array<char, kCapacity> buffer_{};
  std::size_t length_ = 0;
  bool overflow_ = false;
};

MicrophoneEventKind hil_microphone_event(HilMicrophoneCommand command,
                                         MicrophoneState state);

bool pet_animation_allowed(MicrophoneState state);
