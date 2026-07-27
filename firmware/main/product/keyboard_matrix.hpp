#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "product/product_types.hpp"

inline constexpr uint32_t kKeyboardDebounceMs = 20;
inline constexpr uint32_t kKeyboardScannerTaskStackBytes = 3328;

struct MatrixKeyEvent {
  uint8_t physical_key = 0;
  bool pressed = false;
  uint64_t stable_at_ms = 0;
};

constexpr uint8_t matrix_key_id(uint8_t selector, uint8_t input) {
  const uint8_t column =
      static_cast<uint8_t>(input * 2 + (selector > 3 ? 0 : 1));
  const uint8_t local_row =
      static_cast<uint8_t>(selector > 3 ? selector - 4 : selector);
  const uint8_t row = static_cast<uint8_t>(3 - local_row);
  return static_cast<uint8_t>(row * 14 + column);
}

class KeyboardDebouncer {
 public:
  std::size_t update(const std::array<uint8_t, 8>& raw, uint64_t now_ms,
                     std::span<MatrixKeyEvent> output);
  [[nodiscard]] const std::array<bool, kPhysicalKeyCount>& stable() const {
    return stable_;
  }

 private:
  std::array<bool, kPhysicalKeyCount> raw_{};
  std::array<bool, kPhysicalKeyCount> stable_{};
  std::array<uint64_t, kPhysicalKeyCount> changed_at_ms_{};
};

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

using MatrixEventHandler = void (*)(const MatrixKeyEvent&);
esp_err_t keyboard_matrix_start(MatrixEventHandler handler);
[[nodiscard]] TaskHandle_t keyboard_matrix_task();
#endif
