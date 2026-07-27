#include <cassert>

#include "product/microphone_state.hpp"

int main() {
  MicrophoneStateMachine release_default;
  auto release_transition =
      release_default.apply({.kind = MicrophoneEventKind::sink_ready});
  assert(release_transition.state == MicrophoneState::ready);
  release_transition =
      release_default.apply({.kind = MicrophoneEventKind::g0_click});
  assert(release_transition.state == MicrophoneState::starting);
  assert(release_transition.command ==
         MicrophoneCommand::start_capture_16k);
  release_transition =
      release_default.apply({.kind = MicrophoneEventKind::capture_started});
  assert(release_transition.state == MicrophoneState::live16);

  MicrophoneStateMachine state(AudioSampleRate::hz24000);
  assert(state.state() == MicrophoneState::unavailable);

  auto transition =
      state.apply({.kind = MicrophoneEventKind::sink_ready});
  assert(transition.state == MicrophoneState::ready);
  assert(transition.command == MicrophoneCommand::none);

  transition = state.apply({.kind = MicrophoneEventKind::g0_ignored});
  assert(transition.state == MicrophoneState::ready);
  assert(transition.command == MicrophoneCommand::none);

  transition = state.apply({.kind = MicrophoneEventKind::g0_click});
  assert(transition.state == MicrophoneState::starting);
  assert(transition.command == MicrophoneCommand::start_capture_24k);
  transition =
      state.apply({.kind = MicrophoneEventKind::capture_started});
  assert(transition.state == MicrophoneState::live24);

  transition = state.apply({.kind = MicrophoneEventKind::g0_click});
  assert(transition.state == MicrophoneState::stopping);
  assert(transition.command == MicrophoneCommand::stop_capture);
  transition =
      state.apply({.kind = MicrophoneEventKind::capture_stopped});
  assert(transition.state == MicrophoneState::ready);

  state.apply({.kind = MicrophoneEventKind::g0_click});
  state.apply({.kind = MicrophoneEventKind::capture_started});
  transition = state.apply({.kind = MicrophoneEventKind::sink_lost});
  assert(transition.state == MicrophoneState::unavailable);
  assert(transition.command == MicrophoneCommand::stop_capture);

  transition = state.apply({.kind = MicrophoneEventKind::sink_ready});
  assert(transition.state == MicrophoneState::ready);
  assert(transition.command == MicrophoneCommand::none);
  assert(!transition.discontinuity);

  state.apply({.kind = MicrophoneEventKind::g0_click});
  state.apply({.kind = MicrophoneEventKind::capture_started});
  transition = state.apply({.kind = MicrophoneEventKind::reset});
  assert(transition.state == MicrophoneState::unavailable);
  assert(transition.command == MicrophoneCommand::stop_capture);
  transition = state.apply({.kind = MicrophoneEventKind::sink_ready});
  assert(transition.state == MicrophoneState::ready);
  assert(transition.command == MicrophoneCommand::none);

  state.apply({.kind = MicrophoneEventKind::g0_click});
  state.apply({.kind = MicrophoneEventKind::capture_started});
  transition =
      state.apply({.kind = MicrophoneEventKind::loss_window_bad});
  assert(transition.state == MicrophoneState::live24);
  assert(transition.command == MicrophoneCommand::none);
  transition =
      state.apply({.kind = MicrophoneEventKind::loss_window_good});
  assert(transition.state == MicrophoneState::live24);
  transition =
      state.apply({.kind = MicrophoneEventKind::loss_window_bad});
  assert(transition.state == MicrophoneState::live24);
  transition =
      state.apply({.kind = MicrophoneEventKind::loss_window_bad});
  assert(transition.state == MicrophoneState::starting);
  assert(transition.command == MicrophoneCommand::restart_capture_16k);
  assert(transition.discontinuity);

  transition =
      state.apply({.kind = MicrophoneEventKind::capture_started});
  assert(transition.state == MicrophoneState::live16);
  assert(transition.command == MicrophoneCommand::none);
  transition =
      state.apply({.kind = MicrophoneEventKind::loss_window_bad});
  assert(transition.state == MicrophoneState::live16);
  transition =
      state.apply({.kind = MicrophoneEventKind::loss_window_bad});
  assert(transition.state == MicrophoneState::error);
  assert(transition.command == MicrophoneCommand::stop_capture);

  transition = state.apply({.kind = MicrophoneEventKind::g0_click});
  assert(transition.state == MicrophoneState::error);
  assert(transition.command == MicrophoneCommand::none);

  MicrophoneStateMachine unavailable;
  transition = unavailable.apply({.kind = MicrophoneEventKind::g0_click});
  assert(transition.state == MicrophoneState::unavailable);
  assert(transition.command == MicrophoneCommand::none);
  transition =
      unavailable.apply({.kind = MicrophoneEventKind::g0_ignored});
  assert(transition.state == MicrophoneState::unavailable);

  MicrophoneStateMachine starting(AudioSampleRate::hz24000);
  starting.apply({.kind = MicrophoneEventKind::sink_ready});
  starting.apply({.kind = MicrophoneEventKind::g0_click});
  transition = starting.apply({.kind = MicrophoneEventKind::sink_lost});
  assert(transition.state == MicrophoneState::unavailable);
  assert(transition.command == MicrophoneCommand::stop_capture);

  MicrophoneStateMachine fatal;
  fatal.apply({.kind = MicrophoneEventKind::sink_ready});
  transition = fatal.apply({.kind = MicrophoneEventKind::fatal_error});
  assert(transition.state == MicrophoneState::error);
  assert(transition.command == MicrophoneCommand::none);
  return 0;
}
