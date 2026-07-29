#pragma once

#include <cstdint>

#include "product/device_settings.hpp"

enum class G0DualActionResult : uint8_t {
  microphone_only,
  chord_then_microphone,
  chord_failed_microphone_toggled,
};

class G0DualActionSink {
 public:
  virtual ~G0DualActionSink() = default;
  virtual bool execute_chord(uint8_t modifiers, uint8_t usage) = 0;
  virtual void toggle_microphone() = 0;
};

G0DualActionResult execute_g0_dual_action(
    const DeviceSettings& settings,
    G0DualActionSink& sink);
