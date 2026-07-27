#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

#include "product/audio_protocol.hpp"
#include "product/microphone_controller.hpp"

namespace {

class FakeCapture final : public IAudioCapture {
 public:
  AudioCaptureResult start(AudioCaptureConfig config) override {
    ++start_calls;
    last_rate = config.rate;
    if (start_result == AudioCaptureResult::ok) {
      active = true;
    }
    return start_result;
  }

  AudioCaptureResult read_frame(std::span<int16_t> samples) override {
    ++read_calls;
    last_read_size = samples.size();
    if (read_result != AudioCaptureResult::ok) {
      if (read_result == AudioCaptureResult::overrun) {
        ++overruns;
      }
      return read_result;
    }
    for (size_t index = 0; index < samples.size(); ++index) {
      samples[index] = static_cast<int16_t>(
          static_cast<int32_t>(index) * 31 - 2048);
    }
    return AudioCaptureResult::ok;
  }

  AudioCaptureResult stop() override {
    ++stop_calls;
    active = false;
    return AudioCaptureResult::ok;
  }

  bool running() const override { return active; }
  uint32_t overrun_count() const override { return overruns; }

  AudioCaptureResult start_result = AudioCaptureResult::ok;
  AudioCaptureResult read_result = AudioCaptureResult::ok;
  AudioSampleRate last_rate = AudioSampleRate::hz16000;
  size_t last_read_size = 0;
  uint32_t overruns = 0;
  int start_calls = 0;
  int read_calls = 0;
  int stop_calls = 0;
  bool active = false;
};

class FakeTransport final : public IBleAudioTransport {
 public:
  bool sink_ready() const override { return ready; }

  BleAudioSendResult try_send(
      std::span<const uint8_t> frame) override {
    ++send_calls;
    packets.emplace_back(frame.begin(), frame.end());
    if (next_result != BleAudioSendResult::sent) {
      ++drops;
    }
    return next_result;
  }

  BleAudioSendResult send_status(
      std::span<const uint8_t>) override {
    return BleAudioSendResult::sent;
  }

  void clear() override {
    ++clear_calls;
    packets.clear();
  }

  uint32_t transport_drops() const override { return drops; }

  bool ready = true;
  BleAudioSendResult next_result = BleAudioSendResult::sent;
  uint32_t drops = 0;
  int send_calls = 0;
  int clear_calls = 0;
  std::vector<std::vector<uint8_t>> packets;
};

AudioFrameView decode(const std::vector<uint8_t>& packet) {
  AudioFrameView frame;
  assert(decode_audio_packet(packet, &frame) == AudioProtocolError::none);
  return frame;
}

void make_live(MicrophoneController& controller,
               FakeCapture& capture) {
  controller.on_sink_ready(true);
  controller.on_g0_click();
  assert(capture.start_calls == 1);
  assert(capture.last_rate == AudioSampleRate::hz24000);
  assert(controller.snapshot().state == MicrophoneState::live24);
}

}  // namespace

int main() {
  assert(microphone_button_event(0) == MicrophoneButtonEvent::click);
  assert(microphone_button_event(1000) == MicrophoneButtonEvent::click);
  assert(microphone_button_event(1001) == MicrophoneButtonEvent::ignored);
  assert(microphone_button_event(5000) == MicrophoneButtonEvent::ignored);

  FakeCapture unavailable_capture;
  FakeTransport unavailable_transport;
  MicrophoneController unavailable(unavailable_capture,
                                   unavailable_transport);
  unavailable.on_g0_click();
  unavailable.on_g0_ignored();
  assert(unavailable_capture.start_calls == 0);
  assert(unavailable.snapshot().state == MicrophoneState::unavailable);

  FakeCapture capture;
  FakeTransport transport;
  MicrophoneController controller(
      capture, transport, 0xFFFF, AudioSampleRate::hz24000);
  make_live(controller, capture);

  assert(controller.run_once());
  assert(capture.last_read_size == 456);
  assert(transport.send_calls == 1);
  assert(transport.packets.back().size() == kAudioPacketBytes24k);
  AudioFrameView first = decode(transport.packets.back());
  assert(first.header.sequence == 0xFFFF);
  assert(first.header.rate == AudioSampleRate::hz24000);
  assert((first.header.flags & kAudioFlagStart) != 0);

  assert(controller.run_once());
  AudioFrameView wrapped = decode(transport.packets.back());
  assert(wrapped.header.sequence == 0);
  assert((wrapped.header.flags & kAudioFlagStart) == 0);
  assert(controller.snapshot().next_sequence == 1);
  assert(controller.snapshot().captured_frames == 2);

  transport.next_result = BleAudioSendResult::dropped;
  assert(!controller.run_once());
  assert(transport.send_calls == 3);
  assert(controller.snapshot().transport_drops == 1);
  assert(controller.snapshot().captured_frames == 3);

  capture.read_result = AudioCaptureResult::overrun;
  assert(!controller.run_once());
  assert(controller.snapshot().source_overruns == 1);
  capture.read_result = AudioCaptureResult::ok;
  transport.next_result = BleAudioSendResult::sent;

  controller.on_loss_window(false);
  assert(controller.snapshot().state == MicrophoneState::live24);
  controller.on_loss_window(false);
  assert(capture.stop_calls == 1);
  assert(capture.start_calls == 2);
  assert(capture.last_rate == AudioSampleRate::hz16000);
  assert(controller.snapshot().state == MicrophoneState::live16);
  assert(controller.snapshot().fallback_count == 1);

  assert(controller.run_once());
  assert(capture.last_read_size == 448);
  assert(transport.packets.back().size() == kAudioPacketBytes16k);
  AudioFrameView fallback = decode(transport.packets.back());
  assert(fallback.header.rate == AudioSampleRate::hz16000);
  assert(fallback.header.duration_ms == 28);
  assert((fallback.header.flags & kAudioFlagDiscontinuity) != 0);
  assert((fallback.header.flags & kAudioFlagDegradedRate) != 0);

  controller.on_loss_window(false);
  controller.on_loss_window(false);
  assert(controller.snapshot().state == MicrophoneState::error);
  assert(!capture.running());
  assert(capture.stop_calls == 2);

  FakeCapture release_capture;
  FakeTransport release_transport;
  MicrophoneController release_default(
      release_capture, release_transport);
  release_default.on_sink_ready(true);
  release_default.on_g0_click();
  assert(release_capture.last_rate == AudioSampleRate::hz16000);
  assert(release_default.snapshot().state == MicrophoneState::live16);
  assert(release_default.run_once());
  const AudioFrameView release_frame =
      decode(release_transport.packets.back());
  assert(release_frame.header.rate == AudioSampleRate::hz16000);
  assert((release_frame.header.flags & kAudioFlagDegradedRate) != 0);
  release_default.on_loss_window(false);
  release_default.on_loss_window(false);
  assert(release_default.snapshot().state == MicrophoneState::error);
  assert(release_default.snapshot().fallback_count == 0);

  FakeCapture disconnected_capture;
  FakeTransport disconnected_transport;
  MicrophoneController disconnected(
      disconnected_capture, disconnected_transport, 0,
      AudioSampleRate::hz24000);
  make_live(disconnected, disconnected_capture);
  assert(disconnected.run_once());
  disconnected.stop_for_disconnect();
  assert(!disconnected_capture.running());
  assert(disconnected_capture.stop_calls == 1);
  assert(disconnected_transport.clear_calls == 1);
  assert(disconnected.snapshot().state == MicrophoneState::unavailable);
  disconnected.on_sink_ready(true);
  assert(disconnected.snapshot().state == MicrophoneState::ready);
  assert(disconnected_capture.start_calls == 1);

  FakeCapture not_ready_capture;
  FakeTransport not_ready_transport;
  MicrophoneController not_ready(
      not_ready_capture, not_ready_transport, 0,
      AudioSampleRate::hz24000);
  make_live(not_ready, not_ready_capture);
  not_ready_transport.next_result = BleAudioSendResult::not_ready;
  assert(!not_ready.run_once());
  assert(not_ready.snapshot().state == MicrophoneState::unavailable);
  assert(!not_ready_capture.running());
  assert(not_ready_transport.clear_calls == 1);

  return 0;
}
