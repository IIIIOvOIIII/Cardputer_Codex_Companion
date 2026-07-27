#include "product/audio_capture.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace {

size_t frame_samples(AudioSampleRate rate) {
  return audio_frame_samples(rate);
}

uint32_t sample_rate_hz(AudioSampleRate rate) {
  switch (rate) {
    case AudioSampleRate::hz24000:
      return 24000;
    case AudioSampleRate::hz16000:
      return 16000;
  }
  return 0;
}

uint32_t saturating_increment(uint32_t value) {
  return value == std::numeric_limits<uint32_t>::max() ? value : value + 1;
}

}  // namespace

ProductMicHardwareConfig product_mic_hardware_config(uint32_t rate_hz) {
  return {
      .sample_rate_hz = rate_hz,
      .data_pin = 46,
      .clock_pin = 43,
      .right_channel = true,
      .over_sampling = 1,
      .magnification = 16,
      .dma_buffer_count = 4,
      .dma_buffer_length = 128,
      .task_priority = 6,
      .task_pinned_core = 1,
  };
}

void DoubleBufferedCaptureState::reset() {
  pending_ = 0;
  next_index_ = 0;
  completion_armed_ = false;
}

bool DoubleBufferedCaptureState::queue() {
  if (pending_ >= 2) {
    return false;
  }
  ++pending_;
  completion_armed_ = false;
  return true;
}

bool DoubleBufferedCaptureState::take_completed(
    uint8_t recording_count, uint8_t* completed_index) {
  if (completed_index == nullptr || pending_ == 0) {
    return false;
  }
  if (recording_count >= pending_) {
    completion_armed_ = true;
    return false;
  }
  if (!completion_armed_) {
    return false;
  }
  *completed_index = next_index_;
  next_index_ ^= 1U;
  --pending_;
  return true;
}

uint32_t audio_capture_read_timeout_ms(uint32_t rate_hz) {
  return rate_hz == 16000 || rate_hz == 24000 ? 100U : 0U;
}

PdmAudioCapture::PdmAudioCapture(AudioCaptureBackend& backend)
    : backend_(&backend) {}

PdmAudioCapture::PdmAudioCapture(
    std::unique_ptr<AudioCaptureBackend> backend)
    : owned_backend_(std::move(backend)), backend_(owned_backend_.get()) {}

PdmAudioCapture::~PdmAudioCapture() {
  stop();
}

AudioCaptureResult PdmAudioCapture::start(AudioCaptureConfig config) {
  if (running_) {
    return AudioCaptureResult::already_started;
  }
  const size_t samples = frame_samples(config.rate);
  const uint32_t rate_hz = sample_rate_hz(config.rate);
  if (samples == 0 || rate_hz == 0 || backend_ == nullptr) {
    return AudioCaptureResult::invalid_rate;
  }
  if (backend_->speaker_running() && !backend_->stop_speaker()) {
    return AudioCaptureResult::speaker_active;
  }
  if (backend_->speaker_running()) {
    return AudioCaptureResult::speaker_active;
  }
  AudioCaptureResult result =
      backend_->prepare(rate_hz, samples);
  if (result != AudioCaptureResult::ok) {
    return result;
  }
  result = backend_->enable();
  if (result != AudioCaptureResult::ok) {
    return result;
  }
  active_frame_samples_ = samples;
  running_ = true;
  return AudioCaptureResult::ok;
}

AudioCaptureResult PdmAudioCapture::read_frame(
    std::span<int16_t> samples) {
  if (!running_) {
    return AudioCaptureResult::not_started;
  }
  if (samples.size() != active_frame_samples_) {
    return AudioCaptureResult::invalid_frame_size;
  }
  const AudioCaptureResult result = backend_->read(samples);
  if (result == AudioCaptureResult::overrun) {
    overrun_count_ = saturating_increment(overrun_count_);
    return result;
  }
  if (result != AudioCaptureResult::ok) {
    return result;
  }
  return AudioCaptureResult::ok;
}

AudioCaptureResult PdmAudioCapture::stop() {
  if (!running_) {
    return AudioCaptureResult::ok;
  }
  running_ = false;
  active_frame_samples_ = 0;
  return backend_ == nullptr ? AudioCaptureResult::backend_error
                             : backend_->disable();
}

#ifdef ESP_PLATFORM

#include "M5Unified.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kAudioCaptureTag[] = "audio-capture";

class M5UnifiedCaptureBackend final : public AudioCaptureBackend {
 public:
  ~M5UnifiedCaptureBackend() override { disable(); }

  bool speaker_running() const override {
    return M5.Speaker.isRunning();
  }

  bool stop_speaker() override {
    M5.Speaker.end();
    return !M5.Speaker.isRunning();
  }

  AudioCaptureResult prepare(uint32_t rate_hz,
                             size_t frame_samples) override {
    if (rate_hz != 16000 && rate_hz != 24000) {
      return AudioCaptureResult::invalid_rate;
    }
    if (frame_samples != 448 && frame_samples != 456) {
      return AudioCaptureResult::invalid_frame_size;
    }
    if (enabled_) {
      ESP_LOGE(kAudioCaptureTag,
               "M5.Mic reconfigure requested while enabled");
      return AudioCaptureResult::backend_error;
    }
    if (M5.Mic.isRunning()) {
      M5.Mic.end();
    }

    configured_rate_hz_ = rate_hz;
    configured_frame_samples_ = frame_samples;
    queue_state_.reset();
    return AudioCaptureResult::ok;
  }

  AudioCaptureResult enable() override {
    if (configured_rate_hz_ == 0 || configured_frame_samples_ == 0) {
      ESP_LOGE(kAudioCaptureTag,
               "M5.Mic enable requested without configuration");
      return AudioCaptureResult::backend_error;
    }

    const ProductMicHardwareConfig hardware =
        product_mic_hardware_config(configured_rate_hz_);
    auto config = M5.Mic.config();
    config.pin_data_in = hardware.data_pin;
    config.pin_ws = hardware.clock_pin;
    config.pin_bck = I2S_PIN_NO_CHANGE;
    config.pin_mck = I2S_PIN_NO_CHANGE;
    config.input_channel =
        hardware.right_channel
            ? m5::input_channel_t::input_only_right
            : m5::input_channel_t::input_only_left;
    config.over_sampling = hardware.over_sampling;
    config.magnification = hardware.magnification;
    config.sample_rate = hardware.sample_rate_hz;
    config.dma_buf_count = hardware.dma_buffer_count;
    config.dma_buf_len = hardware.dma_buffer_length;
    config.task_priority = hardware.task_priority;
    config.task_pinned_core = hardware.task_pinned_core;
    M5.Mic.config(config);

    if (!M5.Mic.begin()) {
      ESP_LOGE(kAudioCaptureTag, "M5.Mic begin failed");
      return AudioCaptureResult::backend_error;
    }
    enabled_ = true;
    queue_state_.reset();
    for (uint8_t index = 0; index < frame_buffers_.size(); ++index) {
      if (!queue_buffer(index)) {
        ESP_LOGE(kAudioCaptureTag,
                 "M5.Mic initial record queue failed: index=%u",
                 static_cast<unsigned>(index));
        disable();
        return AudioCaptureResult::backend_error;
      }
    }
    return AudioCaptureResult::ok;
  }

  AudioCaptureResult read(std::span<int16_t> samples) override {
    if (!enabled_ || !M5.Mic.isRunning()) {
      return AudioCaptureResult::not_started;
    }
    if (samples.size() != configured_frame_samples_) {
      return AudioCaptureResult::invalid_frame_size;
    }

    uint8_t completed_index = 0;
    const TickType_t timeout_ticks = pdMS_TO_TICKS(
        audio_capture_read_timeout_ms(configured_rate_hz_));
    const TickType_t start_ticks = xTaskGetTickCount();
    while (!queue_state_.take_completed(
        static_cast<uint8_t>(M5.Mic.isRecording()),
        &completed_index)) {
      if (!M5.Mic.isRunning()) {
        ESP_LOGE(kAudioCaptureTag,
                 "M5.Mic stopped while waiting for a frame");
        return AudioCaptureResult::backend_error;
      }
      if (xTaskGetTickCount() - start_ticks >= timeout_ticks) {
        return AudioCaptureResult::timeout;
      }
      vTaskDelay(1);
    }

    std::copy_n(frame_buffers_[completed_index].begin(),
                configured_frame_samples_, samples.begin());
    if (!queue_buffer(completed_index)) {
      ESP_LOGE(kAudioCaptureTag,
               "M5.Mic record requeue failed: index=%u",
               static_cast<unsigned>(completed_index));
      return AudioCaptureResult::backend_error;
    }
    return AudioCaptureResult::ok;
  }

  AudioCaptureResult disable() override {
    if (!enabled_) {
      queue_state_.reset();
      return AudioCaptureResult::ok;
    }
    enabled_ = false;
    const TickType_t drain_start = xTaskGetTickCount();
    const TickType_t drain_timeout =
        pdMS_TO_TICKS(2 * audio_capture_read_timeout_ms(
                           configured_rate_hz_));
    while (M5.Mic.isRecording() != 0 &&
           xTaskGetTickCount() - drain_start < drain_timeout) {
      vTaskDelay(1);
    }
    M5.Mic.end();
    queue_state_.reset();
    return M5.Mic.isRunning()
               ? AudioCaptureResult::backend_error
               : AudioCaptureResult::ok;
  }

 private:
  bool queue_buffer(uint8_t index) {
    if (index >= frame_buffers_.size() ||
        queue_state_.pending() >= frame_buffers_.size()) {
      return false;
    }
    if (!M5.Mic.record(
            frame_buffers_[index].data(),
            configured_frame_samples_,
            configured_rate_hz_, false)) {
      return false;
    }
    return queue_state_.queue();
  }

  static constexpr size_t kMaximumFrameSamples = 456;
  std::array<std::array<int16_t, kMaximumFrameSamples>, 2>
      frame_buffers_{};
  DoubleBufferedCaptureState queue_state_{};
  uint32_t configured_rate_hz_ = 0;
  size_t configured_frame_samples_ = 0;
  bool enabled_ = false;
};

}  // namespace

std::unique_ptr<IAudioCapture> make_product_audio_capture() {
  return std::make_unique<PdmAudioCapture>(
      std::make_unique<M5UnifiedCaptureBackend>());
}

#else

std::unique_ptr<IAudioCapture> make_product_audio_capture() {
  return nullptr;
}

#endif
