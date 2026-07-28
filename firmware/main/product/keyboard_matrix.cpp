#include "product/keyboard_matrix.hpp"

#include <algorithm>

std::size_t KeyboardDebouncer::update(const std::array<uint8_t, 8>& raw,
                                      uint64_t now_ms,
                                      std::span<MatrixKeyEvent> output) {
  std::array<bool, kPhysicalKeyCount> observed{};
  for (uint8_t selector = 0; selector < raw.size(); ++selector) {
    for (uint8_t input = 0; input < 7; ++input) {
      if ((raw[selector] & (1u << input)) != 0) {
        observed[matrix_key_id(selector, input)] = true;
      }
    }
  }

  std::size_t written = 0;
  for (std::size_t key = 0; key < observed.size(); ++key) {
    if (observed[key] != raw_[key]) {
      raw_[key] = observed[key];
      changed_at_ms_[key] = now_ms;
    }
    if (stable_[key] == raw_[key] ||
        now_ms - changed_at_ms_[key] < kKeyboardDebounceMs) {
      continue;
    }
    stable_[key] = raw_[key];
    if (written < output.size()) {
      output[written++] = {
          .physical_key = static_cast<uint8_t>(key),
          .pressed = stable_[key],
          .stable_at_ms = now_ms,
      };
    }
  }
  return written;
}

#ifdef ESP_PLATFORM
#include <array>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr std::array<gpio_num_t, 3> kSelectorPins{
    GPIO_NUM_8, GPIO_NUM_9, GPIO_NUM_11};
constexpr std::array<gpio_num_t, 7> kInputPins{
    GPIO_NUM_13, GPIO_NUM_15, GPIO_NUM_3, GPIO_NUM_4,
    GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7};
StaticTask_t g_scanner_task_storage{};
std::array<StackType_t, kKeyboardScannerTaskStackBytes> g_scanner_stack{};
TaskHandle_t g_scanner_task = nullptr;
MatrixEventHandler g_handler = nullptr;

void initialize_matrix_pins() {
  for (gpio_num_t pin : kSelectorPins) {
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
  }
  for (gpio_num_t pin : kInputPins) {
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
  }
}

void select_row(uint8_t selector) {
  for (uint8_t bit = 0; bit < kSelectorPins.size(); ++bit) {
    gpio_set_level(kSelectorPins[bit], (selector >> bit) & 1u);
  }
}

void scanner_task(void*) {
  KeyboardDebouncer debouncer;
  std::array<MatrixKeyEvent, kPhysicalKeyCount> events{};
  TickType_t wake = xTaskGetTickCount();
  while (true) {
    std::array<uint8_t, 8> sample{};
    for (uint8_t selector = 0; selector < sample.size(); ++selector) {
      select_row(selector);
      esp_rom_delay_us(5);
      for (uint8_t input = 0; input < kInputPins.size(); ++input) {
        if (gpio_get_level(kInputPins[input]) == 0) {
          sample[selector] |= static_cast<uint8_t>(1u << input);
        }
      }
    }
    const uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1000;
    const std::size_t count = debouncer.update(sample, now_ms, events);
    for (std::size_t index = 0; index < count; ++index) {
      if (g_handler != nullptr) {
        g_handler(events[index]);
      }
    }
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
  }
}
}  // namespace

esp_err_t keyboard_matrix_start(MatrixEventHandler handler) {
  if (handler == nullptr || g_scanner_task != nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  g_handler = handler;
  initialize_matrix_pins();
  g_scanner_task = xTaskCreateStatic(
      scanner_task, "scanner", kKeyboardScannerTaskStackBytes, nullptr,
      tskIDLE_PRIORITY + 3, g_scanner_stack.data(), &g_scanner_task_storage);
  return g_scanner_task == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
}

bool keyboard_matrix_key_pressed(uint8_t physical_key) {
  if (physical_key >= kPhysicalKeyCount) return false;
  initialize_matrix_pins();
  for (uint8_t selector = 0; selector < 8; ++selector) {
    select_row(selector);
    esp_rom_delay_us(5);
    for (uint8_t input = 0; input < kInputPins.size(); ++input) {
      if (matrix_key_id(selector, input) == physical_key) {
        return gpio_get_level(kInputPins[input]) == 0;
      }
    }
  }
  return false;
}

TaskHandle_t keyboard_matrix_task() {
  return g_scanner_task;
}
#endif
