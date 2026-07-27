#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "product/audio_protocol.hpp"

struct AudioCaptureConfig {
  AudioSampleRate rate = AudioSampleRate::hz24000;
};

struct ProductMicHardwareConfig {
  uint32_t sample_rate_hz = 0;
  int data_pin = -1;
  int clock_pin = -1;
  bool right_channel = false;
  uint8_t over_sampling = 0;
  uint8_t magnification = 0;
  uint8_t dma_buffer_count = 0;
  size_t dma_buffer_length = 0;
  uint8_t task_priority = 0;
  uint8_t task_pinned_core = 0;
};

ProductMicHardwareConfig product_mic_hardware_config(uint32_t rate_hz);

class DoubleBufferedCaptureState {
 public:
  void reset();
  [[nodiscard]] bool queue();
  [[nodiscard]] bool take_completed(uint8_t recording_count,
                                    uint8_t* completed_index);
  [[nodiscard]] uint8_t pending() const { return pending_; }
  [[nodiscard]] uint8_t next_index() const { return next_index_; }

 private:
  uint8_t pending_ = 0;
  uint8_t next_index_ = 0;
  bool completion_armed_ = false;
};

uint32_t audio_capture_read_timeout_ms(uint32_t rate_hz);

enum class AudioCaptureResult : uint8_t {
  ok,
  not_started,
  already_started,
  invalid_rate,
  invalid_frame_size,
  speaker_active,
  timeout,
  overrun,
  backend_error,
};

class IAudioCapture {
 public:
  virtual ~IAudioCapture() = default;
  virtual AudioCaptureResult start(AudioCaptureConfig config) = 0;
  virtual AudioCaptureResult read_frame(std::span<int16_t> samples) = 0;
  virtual AudioCaptureResult stop() = 0;
  [[nodiscard]] virtual bool running() const = 0;
  [[nodiscard]] virtual uint32_t overrun_count() const = 0;
};

class AudioCaptureBackend {
 public:
  virtual ~AudioCaptureBackend() = default;
  [[nodiscard]] virtual bool speaker_running() const = 0;
  virtual bool stop_speaker() = 0;
  virtual AudioCaptureResult prepare(uint32_t rate_hz,
                                     size_t maximum_samples) = 0;
  virtual AudioCaptureResult enable() = 0;
  virtual AudioCaptureResult read(std::span<int16_t> samples) = 0;
  virtual AudioCaptureResult disable() = 0;
};

class PdmAudioCapture final : public IAudioCapture {
 public:
  explicit PdmAudioCapture(AudioCaptureBackend& backend);
  explicit PdmAudioCapture(std::unique_ptr<AudioCaptureBackend> backend);
  ~PdmAudioCapture() override;

  AudioCaptureResult start(AudioCaptureConfig config) override;
  AudioCaptureResult read_frame(std::span<int16_t> samples) override;
  AudioCaptureResult stop() override;
  [[nodiscard]] bool running() const override { return running_; }
  [[nodiscard]] uint32_t overrun_count() const override {
    return overrun_count_;
  }

 private:
  std::unique_ptr<AudioCaptureBackend> owned_backend_{};
  AudioCaptureBackend* backend_ = nullptr;
  size_t active_frame_samples_ = 0;
  uint32_t overrun_count_ = 0;
  bool running_ = false;
};

std::unique_ptr<IAudioCapture> make_product_audio_capture();
