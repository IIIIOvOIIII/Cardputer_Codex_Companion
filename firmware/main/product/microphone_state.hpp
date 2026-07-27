#pragma once

#include <cstdint>

#include "product/audio_protocol.hpp"

enum class MicrophoneState : uint8_t {
  unavailable,
  ready,
  starting,
  live24,
  live16,
  stopping,
  error,
};

enum class MicrophoneEventKind : uint8_t {
  sink_ready,
  sink_lost,
  g0_click,
  g0_ignored,
  capture_started,
  capture_stopped,
  loss_window_good,
  loss_window_bad,
  fatal_error,
  reset,
};

struct MicrophoneEvent {
  MicrophoneEventKind kind = MicrophoneEventKind::reset;
};

enum class MicrophoneCommand : uint8_t {
  none,
  start_capture_24k,
  start_capture_16k,
  restart_capture_16k,
  stop_capture,
};

struct MicrophoneTransition {
  MicrophoneState state = MicrophoneState::unavailable;
  MicrophoneCommand command = MicrophoneCommand::none;
  bool discontinuity = false;
};

class MicrophoneStateMachine {
 public:
  explicit MicrophoneStateMachine(
      AudioSampleRate preferred_rate = AudioSampleRate::hz16000)
      : preferred_rate_(preferred_rate),
        target_rate_(preferred_rate) {}

  [[nodiscard]] MicrophoneState state() const { return state_; }
  MicrophoneTransition apply(MicrophoneEvent event);

 private:
  [[nodiscard]] bool capture_active_or_pending() const;
  [[nodiscard]] MicrophoneTransition result(
      MicrophoneCommand command = MicrophoneCommand::none,
      bool discontinuity = false) const;

  MicrophoneState state_ = MicrophoneState::unavailable;
  AudioSampleRate preferred_rate_ = AudioSampleRate::hz16000;
  AudioSampleRate target_rate_ = AudioSampleRate::hz16000;
  bool sink_ready_ = false;
  uint8_t consecutive_bad_windows_ = 0;
};
