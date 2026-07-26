#include <array>
#include <cassert>
#include <cstdint>
#include <span>

#include "product/audio_capture.hpp"

namespace {

class FakeCaptureBackend final : public AudioCaptureBackend {
 public:
  bool speaker_running() const override { return speaker_running_; }

  bool stop_speaker() override {
    ++speaker_stop_calls;
    speaker_running_ = false;
    return true;
  }

  AudioCaptureResult prepare(uint32_t rate_hz,
                             size_t maximum_samples) override {
    ++prepare_calls;
    prepared_rate_hz = rate_hz;
    prepared_maximum_samples = maximum_samples;
    return prepare_result;
  }

  AudioCaptureResult enable() override {
    ++enable_calls;
    return enable_result;
  }

  AudioCaptureResult read(std::span<int16_t> samples) override {
    ++read_calls;
    for (size_t index = 0; index < samples.size(); ++index) {
      samples[index] = static_cast<int16_t>(index);
    }
    return read_result;
  }

  AudioCaptureResult disable() override {
    ++disable_calls;
    return disable_result;
  }

  bool speaker_running_ = true;
  int speaker_stop_calls = 0;
  int prepare_calls = 0;
  int enable_calls = 0;
  int read_calls = 0;
  int disable_calls = 0;
  uint32_t prepared_rate_hz = 0;
  size_t prepared_maximum_samples = 0;
  AudioCaptureResult prepare_result = AudioCaptureResult::ok;
  AudioCaptureResult enable_result = AudioCaptureResult::ok;
  AudioCaptureResult read_result = AudioCaptureResult::ok;
  AudioCaptureResult disable_result = AudioCaptureResult::ok;
};

}  // namespace

int main() {
  FakeCaptureBackend backend;
  PdmAudioCapture capture(backend);

  std::array<int16_t, 240> frame24{};
  assert(capture.read_frame(frame24) == AudioCaptureResult::not_started);
  assert(backend.read_calls == 0);

  assert(capture.start({.rate = AudioSampleRate::hz24000}) ==
         AudioCaptureResult::ok);
  assert(capture.running());
  assert(backend.speaker_stop_calls == 1);
  assert(!backend.speaker_running());
  assert(backend.prepare_calls == 1);
  assert(backend.prepared_rate_hz == 24000);
  assert(backend.prepared_maximum_samples == 240);
  assert(backend.enable_calls == 1);

  std::array<int16_t, 160> wrong_frame{};
  assert(capture.read_frame(wrong_frame) ==
         AudioCaptureResult::invalid_frame_size);
  assert(backend.read_calls == 0);

  assert(capture.read_frame(frame24) == AudioCaptureResult::ok);
  assert(frame24.front() == 0);
  assert(frame24.back() == 239);
  assert(backend.read_calls == 1);
  assert(backend.prepare_calls == 1);

  assert(capture.read_frame(frame24) == AudioCaptureResult::ok);
  assert(backend.read_calls == 2);
  assert(backend.prepare_calls == 1);
  assert(capture.start({.rate = AudioSampleRate::hz24000}) ==
         AudioCaptureResult::already_started);
  assert(backend.prepare_calls == 1);

  backend.read_result = AudioCaptureResult::overrun;
  assert(capture.read_frame(frame24) == AudioCaptureResult::overrun);
  assert(capture.overrun_count() == 1);
  backend.read_result = AudioCaptureResult::ok;
  assert(capture.read_frame(frame24) == AudioCaptureResult::ok);
  assert(capture.overrun_count() == 1);

  assert(capture.stop() == AudioCaptureResult::ok);
  assert(!capture.running());
  assert(backend.disable_calls == 1);
  assert(capture.stop() == AudioCaptureResult::ok);
  assert(backend.disable_calls == 1);

  backend.speaker_running_ = false;
  assert(capture.start({.rate = AudioSampleRate::hz16000}) ==
         AudioCaptureResult::ok);
  assert(backend.speaker_stop_calls == 1);
  assert(backend.prepare_calls == 2);
  assert(backend.prepared_rate_hz == 16000);
  assert(backend.prepared_maximum_samples == 240);
  assert(capture.read_frame(wrong_frame) == AudioCaptureResult::ok);

  assert(capture.stop() == AudioCaptureResult::ok);

  FakeCaptureBackend failed_backend;
  failed_backend.prepare_result = AudioCaptureResult::backend_error;
  PdmAudioCapture failed_capture(failed_backend);
  assert(failed_capture.start({.rate = AudioSampleRate::hz24000}) ==
         AudioCaptureResult::backend_error);
  assert(!failed_capture.running());
  assert(failed_backend.enable_calls == 0);

  FakeCaptureBackend invalid_backend;
  PdmAudioCapture invalid_capture(invalid_backend);
  assert(invalid_capture.start(
             {.rate = static_cast<AudioSampleRate>(3)}) ==
         AudioCaptureResult::invalid_rate);
  assert(invalid_backend.speaker_stop_calls == 0);
  return 0;
}
