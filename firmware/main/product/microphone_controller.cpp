#include "product/microphone_controller.hpp"

#include <algorithm>
#include <limits>
#include <span>

#include "product/audio_protocol.hpp"
#include "product/ima_adpcm.hpp"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif

namespace {

uint32_t saturating_increment(uint32_t value) {
  return value == std::numeric_limits<uint32_t>::max() ? value : value + 1;
}

#ifdef ESP_PLATFORM
constexpr char kMicrophoneTag[] = "microphone";
#endif

}  // namespace

MicrophoneButtonEvent microphone_button_event(uint32_t held_ms) {
  return held_ms <= 1000 ? MicrophoneButtonEvent::click
                         : MicrophoneButtonEvent::ignored;
}

MicrophoneController::MicrophoneController(
    IAudioCapture& capture, IBleAudioTransport& transport,
    uint16_t initial_sequence, AudioSampleRate preferred_rate)
    : capture_(capture), transport_(transport),
      state_machine_(preferred_rate),
      active_rate_(preferred_rate),
      next_sequence_(initial_sequence) {}

void MicrophoneController::publish_state() {
  published_state_.store(state_machine_.state());
}

void MicrophoneController::execute(MicrophoneTransition transition) {
  switch (transition.command) {
    case MicrophoneCommand::none:
      return;

    case MicrophoneCommand::start_capture_24k:
    case MicrophoneCommand::start_capture_16k:
    case MicrophoneCommand::restart_capture_16k: {
      const bool fallback =
          transition.command == MicrophoneCommand::restart_capture_16k;
      if (fallback) {
        if (capture_.running()) {
          capture_.stop();
        }
        fallback_count_.store(
            saturating_increment(fallback_count_.load()));
        discontinuity_pending_ = true;
      }
      const AudioSampleRate rate =
          transition.command == MicrophoneCommand::start_capture_24k
              ? AudioSampleRate::hz24000
              : AudioSampleRate::hz16000;
      failure_.store(MicrophoneFailure::none);
      no_signal_frame_count_ = 0;
      const AudioCaptureResult result = capture_.start({.rate = rate});
      if (result != AudioCaptureResult::ok) {
#ifdef ESP_PLATFORM
        ESP_LOGE(kMicrophoneTag, "capture start failed: result=%u",
                 static_cast<unsigned>(result));
#endif
        fail();
        return;
      }
      active_rate_.store(rate);
      const MicrophoneTransition started = state_machine_.apply(
          {.kind = MicrophoneEventKind::capture_started});
      publish_state();
      execute(started);
      return;
    }

    case MicrophoneCommand::stop_capture: {
      if (capture_.running()) {
        capture_.stop();
      }
      transport_.clear();
      if (transition.state == MicrophoneState::stopping) {
        const MicrophoneTransition stopped = state_machine_.apply(
            {.kind = MicrophoneEventKind::capture_stopped});
        publish_state();
        execute(stopped);
      }
      return;
    }
  }
}

void MicrophoneController::apply(MicrophoneEventKind event) {
  const MicrophoneTransition transition =
      state_machine_.apply({.kind = event});
  if (transition.discontinuity) {
    discontinuity_pending_ = true;
  }
  publish_state();
  execute(transition);
}

void MicrophoneController::on_sink_ready(bool ready) {
  if (!ready) {
    stop_for_disconnect();
    return;
  }
  apply(MicrophoneEventKind::sink_ready);
}

void MicrophoneController::on_g0_click() {
  const MicrophoneState before = state_machine_.state();
  if (before == MicrophoneState::ready) {
    start_pending_ = true;
    discontinuity_pending_ = false;
  }
  apply(MicrophoneEventKind::g0_click);
  const MicrophoneState after = state_machine_.state();
  if (after != MicrophoneState::live24 &&
      after != MicrophoneState::live16 &&
      after != MicrophoneState::starting) {
    start_pending_ = false;
    discontinuity_pending_ = false;
  }
}

void MicrophoneController::on_g0_ignored() {
  apply(MicrophoneEventKind::g0_ignored);
}

void MicrophoneController::on_loss_window(bool good) {
  apply(good ? MicrophoneEventKind::loss_window_good
             : MicrophoneEventKind::loss_window_bad);
}

void MicrophoneController::fail(MicrophoneFailure failure) {
  failure_.store(failure);
  const MicrophoneTransition transition =
      state_machine_.apply({.kind = MicrophoneEventKind::fatal_error});
  publish_state();
  execute(transition);
  start_pending_ = false;
  discontinuity_pending_ = false;
  transport_.clear();
}

bool MicrophoneController::run_once() {
  const MicrophoneState state = state_machine_.state();
  if (state != MicrophoneState::live24 &&
      state != MicrophoneState::live16) {
    return false;
  }

  const AudioSampleRate rate =
      state == MicrophoneState::live24
          ? AudioSampleRate::hz24000
          : AudioSampleRate::hz16000;
  const size_t sample_count = audio_frame_samples(rate);
  const AudioCaptureResult capture_result =
      capture_.read_frame(std::span<int16_t>(pcm_.data(), sample_count));
  source_overruns_.store(capture_.overrun_count());
  if (capture_result != AudioCaptureResult::ok) {
    if (capture_result != AudioCaptureResult::timeout &&
        capture_result != AudioCaptureResult::overrun) {
#ifdef ESP_PLATFORM
      ESP_LOGE(kMicrophoneTag, "capture read failed: result=%u",
               static_cast<unsigned>(capture_result));
#endif
      fail();
    }
    return false;
  }

  uint32_t frame_peak = 0;
  uint64_t frame_magnitude_sum = 0;
  for (size_t index = 0; index < sample_count; ++index) {
    const int32_t signed_value = pcm_[index];
    const uint32_t magnitude = static_cast<uint32_t>(
        signed_value < 0 ? -signed_value : signed_value);
    frame_peak = std::max(frame_peak, magnitude);
    frame_magnitude_sum += magnitude;
  }
  pcm_peak_.store(frame_peak);
  pcm_mean_abs_.store(static_cast<uint32_t>(
      frame_magnitude_sum / sample_count));
  const bool constant_low_level =
      frame_peak <= 16 &&
      std::all_of(
          pcm_.begin(), pcm_.begin() + sample_count,
          [first = pcm_[0]](int16_t sample) {
            return sample == first;
          });
  if (constant_low_level) {
    if (no_signal_frame_count_ < kMicrophoneNoSignalFrameLimit) {
      ++no_signal_frame_count_;
    }
    if (no_signal_frame_count_ >= kMicrophoneNoSignalFrameLimit) {
#ifdef ESP_PLATFORM
      ESP_LOGE(kMicrophoneTag,
               "M5.Mic produced constant low-level PCM");
#endif
      fail(MicrophoneFailure::no_signal);
      return false;
    }
  } else {
    no_signal_frame_count_ = 0;
  }

  const size_t payload_size =
      rate == AudioSampleRate::hz24000
          ? kAudioPayloadBytes24k
          : kAudioPayloadBytes16k;
  size_t encoded_size = 0;
  if (ima_adpcm_encode_block(
          std::span<const int16_t>(pcm_.data(), sample_count),
          std::span<uint8_t>(encoded_.data(), payload_size),
          &encoded_size) != ImaAdpcmError::none ||
      encoded_size != payload_size) {
#ifdef ESP_PLATFORM
    ESP_LOGE(kMicrophoneTag,
             "ADPCM encode failed: expected=%u encoded=%u",
             static_cast<unsigned>(payload_size),
             static_cast<unsigned>(encoded_size));
#endif
    fail();
    return false;
  }

  uint8_t flags = 0;
  if (start_pending_) flags |= kAudioFlagStart;
  if (discontinuity_pending_) flags |= kAudioFlagDiscontinuity;
  if (rate == AudioSampleRate::hz16000) flags |= kAudioFlagDegradedRate;
  size_t packet_size = 0;
  const uint16_t sequence = next_sequence_.load();
  if (encode_audio_packet(
          {
              .version = kAudioProtocolVersion,
              .flags = flags,
              .sequence = sequence,
              .rate = rate,
              .duration_ms = audio_frame_duration_ms(rate),
              .payload_length = static_cast<uint16_t>(payload_size),
          },
          std::span<const uint8_t>(encoded_.data(), encoded_size),
          packet_, &packet_size) != AudioProtocolError::none) {
#ifdef ESP_PLATFORM
    ESP_LOGE(kMicrophoneTag, "audio packet encode failed");
#endif
    fail();
    return false;
  }

  captured_frames_.store(
      saturating_increment(captured_frames_.load()));
  next_sequence_.store(static_cast<uint16_t>(sequence + 1U));
  const BleAudioSendResult send_result =
      transport_.try_send(
          std::span<const uint8_t>(packet_.data(), packet_size));
  transport_drops_.store(transport_.transport_drops());
  if (send_result == BleAudioSendResult::sent) {
    start_pending_ = false;
    discontinuity_pending_ = false;
    return true;
  }
  if (send_result == BleAudioSendResult::not_ready) {
    stop_for_disconnect();
  } else if (send_result == BleAudioSendResult::fatal_failure) {
#ifdef ESP_PLATFORM
    ESP_LOGE(kMicrophoneTag, "BLE audio transport fatal failure");
#endif
    fail();
  }
  return false;
}

void MicrophoneController::stop_for_disconnect() {
  const MicrophoneTransition transition =
      state_machine_.apply({.kind = MicrophoneEventKind::sink_lost});
  publish_state();
  execute(transition);
  start_pending_ = false;
  discontinuity_pending_ = false;
  no_signal_frame_count_ = 0;
  failure_.store(MicrophoneFailure::none);
}

MicrophoneSnapshot MicrophoneController::snapshot() const {
  return {
      .state = published_state_.load(),
      .active_rate = active_rate_.load(),
      .next_sequence = next_sequence_.load(),
      .captured_frames = captured_frames_.load(),
      .source_overruns = source_overruns_.load(),
      .transport_drops = transport_drops_.load(),
      .fallback_count = fallback_count_.load(),
      .pcm_peak = pcm_peak_.load(),
      .pcm_mean_abs = pcm_mean_abs_.load(),
      .failure = failure_.load(),
  };
}
