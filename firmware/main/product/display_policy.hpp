#pragma once

#include <cstdint>

#include "product/microphone_state.hpp"
#include "product/ui_model.hpp"

enum class PetFrameRenderMode : uint8_t {
  none,
  static_frame,
  animated_frame,
};

PetFrameRenderMode pet_frame_render_mode(
    MicrophoneState microphone_state,
    bool pet_chrome_changed,
    bool animation_due);

uint8_t display_body_text_size(UiPage page);
