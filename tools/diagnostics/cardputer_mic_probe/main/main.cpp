#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

#include "M5Unified.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "mic-probe";
constexpr uint32_t kSampleRate = 16000;
constexpr std::size_t kSamples = 512;

std::array<int16_t, kSamples> g_samples{};

void probe_task(void*) {
  auto config = M5.config();
  config.clear_display = false;
  M5.begin(config);
  M5.Speaker.end();

  const gpio_config_t mic_data_gpio = {
      .pin_bit_mask = 1ULL << GPIO_NUM_46,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&mic_data_gpio));

  auto mic_config = M5.Mic.config();
  mic_config.pin_data_in = GPIO_NUM_46;
  mic_config.pin_ws = GPIO_NUM_43;
  mic_config.pin_bck = I2S_PIN_NO_CHANGE;
  mic_config.input_channel = m5::input_channel_t::input_only_right;
  mic_config.over_sampling = 1;
  mic_config.magnification = 16;
  mic_config.sample_rate = kSampleRate;
  M5.Mic.config(mic_config);

  ESP_LOGI(kTag, "board=%u m5_mic data=46 clock=43 rate=%lu",
           static_cast<unsigned>(M5.getBoard()),
           static_cast<unsigned long>(kSampleRate));
  if (!M5.Mic.begin()) {
    ESP_LOGE(kTag, "M5.Mic begin failed");
    vTaskDelete(nullptr);
  }

  for (uint32_t frame = 0;; ++frame) {
    if (!M5.Mic.record(g_samples.data(), g_samples.size(), kSampleRate,
                       false)) {
      ESP_LOGE(kTag, "M5.Mic record failed");
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    while (M5.Mic.isRecording() != 0) {
      vTaskDelay(1);
    }

    const auto [minimum, maximum] =
        std::minmax_element(g_samples.begin(), g_samples.end());
    uint32_t peak = 0;
    uint64_t absolute_sum = 0;
    uint32_t changes = 0;
    for (std::size_t index = 0; index < g_samples.size(); ++index) {
      const int32_t value = g_samples[index];
      const uint32_t magnitude =
          static_cast<uint32_t>(value < 0 ? -value : value);
      peak = std::max(peak, magnitude);
      absolute_sum += magnitude;
      if (index != 0 && g_samples[index] != g_samples[index - 1]) {
        ++changes;
      }
    }
    if (frame % 20 == 0) {
      ESP_LOGI(
          kTag,
          "frame=%lu min=%d max=%d peak=%lu mean=%lu changes=%lu first=%d",
          static_cast<unsigned long>(frame), static_cast<int>(*minimum),
          static_cast<int>(*maximum), static_cast<unsigned long>(peak),
          static_cast<unsigned long>(absolute_sum / g_samples.size()),
          static_cast<unsigned long>(changes),
          static_cast<int>(g_samples.front()));
    }
  }
}

}  // namespace

extern "C" void app_main() {
  xTaskCreatePinnedToCore(probe_task, "mic-probe", 8192, nullptr, 2, nullptr,
                          1);
}
