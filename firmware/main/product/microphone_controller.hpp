#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "product/audio_capture.hpp"
#include "product/ble_audio_transport.hpp"
#include "product/microphone_state.hpp"

enum class MicrophoneButtonEvent : uint8_t {
  click,
  ignored,
};

[[nodiscard]] MicrophoneButtonEvent microphone_button_event(
    uint32_t held_ms);

struct MicrophoneSnapshot {
  MicrophoneState state = MicrophoneState::unavailable;
  AudioSampleRate active_rate = AudioSampleRate::hz24000;
  uint16_t next_sequence = 0;
  uint32_t captured_frames = 0;
  uint32_t source_overruns = 0;
  uint32_t transport_drops = 0;
  uint32_t fallback_count = 0;
};

class MicrophoneController {
 public:
  MicrophoneController(IAudioCapture& capture,
                       IBleAudioTransport& transport,
                       uint16_t initial_sequence = 0,
                       AudioSampleRate preferred_rate =
                           AudioSampleRate::hz16000);

  void on_sink_ready(bool ready);
  void on_g0_click();
  void on_g0_ignored();
  void on_loss_window(bool good);
  [[nodiscard]] bool run_once();
  void stop_for_disconnect();
  [[nodiscard]] MicrophoneSnapshot snapshot() const;

 private:
  void apply(MicrophoneEventKind event);
  void execute(MicrophoneTransition transition);
  void fail();
  void publish_state();

  IAudioCapture& capture_;
  IBleAudioTransport& transport_;
  MicrophoneStateMachine state_machine_{};
  std::array<int16_t, 456> pcm_{};
  std::array<uint8_t, kAudioPayloadBytes24k> encoded_{};
  std::array<uint8_t, kAudioPacketBytes24k> packet_{};
  std::atomic<MicrophoneState> published_state_{
      MicrophoneState::unavailable};
  std::atomic<AudioSampleRate> active_rate_{
      AudioSampleRate::hz16000};
  std::atomic<uint16_t> next_sequence_{0};
  std::atomic<uint32_t> captured_frames_{0};
  std::atomic<uint32_t> source_overruns_{0};
  std::atomic<uint32_t> transport_drops_{0};
  std::atomic<uint32_t> fallback_count_{0};
  bool start_pending_ = false;
  bool discontinuity_pending_ = false;
};
