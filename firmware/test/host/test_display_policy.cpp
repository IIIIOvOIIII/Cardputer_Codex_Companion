#include <cassert>

#include "product/display_policy.hpp"

int main() {
  assert(pet_frame_render_mode(
             MicrophoneState::live16, true, true) ==
         PetFrameRenderMode::animated_frame);
  assert(pet_frame_render_mode(
             MicrophoneState::live16, false, true) ==
         PetFrameRenderMode::animated_frame);
  assert(pet_frame_render_mode(
             MicrophoneState::live24, false, true) ==
         PetFrameRenderMode::animated_frame);
  assert(pet_frame_render_mode(
             MicrophoneState::starting, true, false) ==
         PetFrameRenderMode::animated_frame);
  assert(pet_frame_render_mode(
             MicrophoneState::stopping, false, true) ==
         PetFrameRenderMode::animated_frame);
  assert(pet_frame_render_mode(
             MicrophoneState::live16, false, false) ==
         PetFrameRenderMode::none);
  assert(pet_frame_render_mode(
             MicrophoneState::ready, false, true) ==
         PetFrameRenderMode::animated_frame);
  assert(pet_frame_render_mode(
             MicrophoneState::ready, true, false) ==
         PetFrameRenderMode::animated_frame);
  assert(pet_frame_render_mode(
             MicrophoneState::ready, false, false) ==
         PetFrameRenderMode::none);
  assert(pet_frame_render_mode(
             MicrophoneState::error, true, false) ==
         PetFrameRenderMode::animated_frame);
}
