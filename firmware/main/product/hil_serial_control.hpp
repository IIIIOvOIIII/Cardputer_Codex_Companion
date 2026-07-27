#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "product/microphone_state.hpp"
#include "probe/keyboard_probe.hpp"

enum class HilMicrophoneCommand : uint8_t {
  none,
  start,
  stop,
  hid_start,
  hid_stop,
};

inline constexpr uint16_t kHilHidBurstEvents = 1000;
inline constexpr uint32_t kHilHidBurstIntervalUs = 500'000;

struct HilCommandDecision {
  MicrophoneEventKind event = MicrophoneEventKind::g0_ignored;
  bool accepted = false;
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

class HilHidBurst {
 public:
  bool start(bool hid_ready);
  bool stop();
  std::optional<StableKeyEvent> next(uint64_t stable_at_us);
  [[nodiscard]] bool active() const { return active_; }
  [[nodiscard]] uint16_t generated() const { return generated_; }

 private:
  bool active_ = false;
  uint16_t generated_ = 0;
  uint64_t next_due_us_ = 0;
};

MicrophoneEventKind hil_microphone_event(HilMicrophoneCommand command,
                                         MicrophoneState state);

HilCommandDecision hil_command_decision(HilMicrophoneCommand command,
                                        MicrophoneState state);

bool pet_animation_allowed(MicrophoneState state);
