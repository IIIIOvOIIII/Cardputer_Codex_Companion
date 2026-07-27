#include "product/display_policy.hpp"

namespace {

bool microphone_holds_pet_frame(MicrophoneState state) {
  return state == MicrophoneState::starting ||
         state == MicrophoneState::live24 ||
         state == MicrophoneState::live16 ||
         state == MicrophoneState::stopping;
}

}  // namespace

PetFrameRenderMode pet_frame_render_mode(
    MicrophoneState microphone_state,
    bool pet_chrome_changed,
    bool animation_due) {
  if (microphone_holds_pet_frame(microphone_state)) {
    return pet_chrome_changed
               ? PetFrameRenderMode::static_frame
               : PetFrameRenderMode::none;
  }
  return pet_chrome_changed || animation_due
             ? PetFrameRenderMode::animated_frame
             : PetFrameRenderMode::none;
}
