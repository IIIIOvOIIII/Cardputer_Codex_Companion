#include "product/audio_capture.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace {

size_t frame_samples(AudioSampleRate rate) {
  switch (rate) {
    case AudioSampleRate::hz24000:
      return 240;
    case AudioSampleRate::hz16000:
      return 160;
  }
  return 0;
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
      backend_->prepare(rate_hz, kMaximumFrameSamples);
  if (result != AudioCaptureResult::ok) {
    return result;
  }
  result = backend_->enable();
  if (result != AudioCaptureResult::ok) {
    return result;
  }
  active_frame_samples_ = samples;
  next_buffer_ = 0;
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
  auto dma_frame = std::span<int16_t>(
      frame_buffers_[next_buffer_].data(), active_frame_samples_);
  const AudioCaptureResult result = backend_->read(dma_frame);
  if (result == AudioCaptureResult::overrun) {
    overrun_count_ = saturating_increment(overrun_count_);
    return result;
  }
  if (result != AudioCaptureResult::ok) {
    return result;
  }
  std::copy(dma_frame.begin(), dma_frame.end(), encoder_input_.begin());
  std::copy_n(encoder_input_.begin(), active_frame_samples_,
              samples.begin());
  next_buffer_ ^= 1U;
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

#include <atomic>

#include "M5Unified.h"
#include "driver/i2s_pdm.h"
#include "freertos/FreeRTOS.h"

namespace {

class EspPdmCaptureBackend final : public AudioCaptureBackend {
 public:
  ~EspPdmCaptureBackend() override {
    disable();
    if (channel_ != nullptr) {
      i2s_del_channel(channel_);
      channel_ = nullptr;
    }
  }

  bool speaker_running() const override {
    return M5.Speaker.isRunning();
  }

  bool stop_speaker() override {
    M5.Speaker.end();
    return !M5.Speaker.isRunning();
  }

  AudioCaptureResult prepare(uint32_t rate_hz,
                             size_t maximum_samples) override {
    if (rate_hz != 16000 && rate_hz != 24000) {
      return AudioCaptureResult::invalid_rate;
    }
    if (maximum_samples != 240) {
      return AudioCaptureResult::invalid_frame_size;
    }
    if (channel_ == nullptr) {
      i2s_chan_config_t channel_config =
          I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
      channel_config.dma_desc_num = 4;
      channel_config.dma_frame_num =
          static_cast<uint32_t>(maximum_samples);
      if (i2s_new_channel(&channel_config, nullptr, &channel_) != ESP_OK) {
        channel_ = nullptr;
        return AudioCaptureResult::backend_error;
      }
      i2s_pdm_rx_config_t config{
          .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(rate_hz),
          .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(
              I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
          .gpio_cfg =
              {
                  .clk = GPIO_NUM_43,
                  .din = GPIO_NUM_46,
                  .invert_flags = {.clk_inv = false},
              },
      };
      if (i2s_channel_init_pdm_rx_mode(channel_, &config) != ESP_OK) {
        i2s_del_channel(channel_);
        channel_ = nullptr;
        return AudioCaptureResult::backend_error;
      }
      const i2s_event_callbacks_t callbacks{
          .on_recv = nullptr,
          .on_recv_q_ovf = &EspPdmCaptureBackend::on_receive_overrun,
          .on_sent = nullptr,
          .on_send_q_ovf = nullptr,
      };
      if (i2s_channel_register_event_callback(
              channel_, &callbacks, this) != ESP_OK) {
        i2s_del_channel(channel_);
        channel_ = nullptr;
        return AudioCaptureResult::backend_error;
      }
      configured_rate_hz_ = rate_hz;
      return AudioCaptureResult::ok;
    }
    if (configured_rate_hz_ != rate_hz) {
      i2s_pdm_rx_clk_config_t clock =
          I2S_PDM_RX_CLK_DEFAULT_CONFIG(rate_hz);
      if (i2s_channel_reconfig_pdm_rx_clock(channel_, &clock) != ESP_OK) {
        return AudioCaptureResult::backend_error;
      }
      configured_rate_hz_ = rate_hz;
    }
    return AudioCaptureResult::ok;
  }

  AudioCaptureResult enable() override {
    if (channel_ == nullptr || i2s_channel_enable(channel_) != ESP_OK) {
      return AudioCaptureResult::backend_error;
    }
    enabled_ = true;
    return AudioCaptureResult::ok;
  }

  AudioCaptureResult read(std::span<int16_t> samples) override {
    if (!enabled_ || channel_ == nullptr) {
      return AudioCaptureResult::not_started;
    }
    if (pending_overruns_.exchange(0, std::memory_order_relaxed) != 0) {
      return AudioCaptureResult::overrun;
    }
    size_t bytes_read = 0;
    const size_t bytes_requested = samples.size_bytes();
    const esp_err_t result =
        i2s_channel_read(channel_, samples.data(), bytes_requested,
                         &bytes_read, pdMS_TO_TICKS(25));
    if (result == ESP_ERR_TIMEOUT) {
      return AudioCaptureResult::timeout;
    }
    if (result != ESP_OK || bytes_read != bytes_requested) {
      return AudioCaptureResult::backend_error;
    }
    return AudioCaptureResult::ok;
  }

  AudioCaptureResult disable() override {
    if (!enabled_) {
      return AudioCaptureResult::ok;
    }
    enabled_ = false;
    return channel_ != nullptr && i2s_channel_disable(channel_) == ESP_OK
               ? AudioCaptureResult::ok
               : AudioCaptureResult::backend_error;
  }

 private:
  static bool IRAM_ATTR on_receive_overrun(
      i2s_chan_handle_t, i2s_event_data_t*, void* user_data) {
    auto* self = static_cast<EspPdmCaptureBackend*>(user_data);
    uint32_t current =
        self->pending_overruns_.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<uint32_t>::max() &&
           !self->pending_overruns_.compare_exchange_weak(
               current, current + 1, std::memory_order_relaxed)) {
    }
    return false;
  }

  i2s_chan_handle_t channel_ = nullptr;
  uint32_t configured_rate_hz_ = 0;
  std::atomic<uint32_t> pending_overruns_{0};
  bool enabled_ = false;
};

}  // namespace

std::unique_ptr<IAudioCapture> make_product_audio_capture() {
  return std::make_unique<PdmAudioCapture>(
      std::make_unique<EspPdmCaptureBackend>());
}

#else

std::unique_ptr<IAudioCapture> make_product_audio_capture() {
  return nullptr;
}

#endif
