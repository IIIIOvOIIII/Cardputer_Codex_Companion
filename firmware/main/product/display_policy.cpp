#include "product/display_policy.hpp"

uint8_t display_body_text_size(UiPage page) {
  return page == UiPage::onboarding ? 1 : 2;
}

PetFrameRenderMode pet_frame_render_mode(
    MicrophoneState,
    bool pet_chrome_changed,
    bool animation_due) {
  return pet_chrome_changed || animation_due
             ? PetFrameRenderMode::animated_frame
             : PetFrameRenderMode::none;
}
