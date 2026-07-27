#include "product/microphone_state.hpp"

bool MicrophoneStateMachine::capture_active_or_pending() const {
  return state_ == MicrophoneState::starting ||
         state_ == MicrophoneState::live24 ||
         state_ == MicrophoneState::live16 ||
         state_ == MicrophoneState::stopping;
}

MicrophoneTransition MicrophoneStateMachine::result(
    MicrophoneCommand command, bool discontinuity) const {
  return {
      .state = state_,
      .command = command,
      .discontinuity = discontinuity,
  };
}

MicrophoneTransition MicrophoneStateMachine::apply(MicrophoneEvent event) {
  switch (event.kind) {
    case MicrophoneEventKind::sink_ready:
      sink_ready_ = true;
      if (state_ == MicrophoneState::unavailable) {
        state_ = MicrophoneState::ready;
      }
      return result();

    case MicrophoneEventKind::sink_lost: {
      sink_ready_ = false;
      const bool must_stop = capture_active_or_pending();
      state_ = MicrophoneState::unavailable;
      consecutive_bad_windows_ = 0;
      target_rate_ = preferred_rate_;
      return result(must_stop ? MicrophoneCommand::stop_capture
                              : MicrophoneCommand::none);
    }

    case MicrophoneEventKind::g0_click:
      if (state_ == MicrophoneState::ready && sink_ready_) {
        state_ = MicrophoneState::starting;
        target_rate_ = preferred_rate_;
        consecutive_bad_windows_ = 0;
        return result(
            target_rate_ == AudioSampleRate::hz24000
                ? MicrophoneCommand::start_capture_24k
                : MicrophoneCommand::start_capture_16k);
      }
      if (state_ == MicrophoneState::live24 ||
          state_ == MicrophoneState::live16) {
        state_ = MicrophoneState::stopping;
        consecutive_bad_windows_ = 0;
        return result(MicrophoneCommand::stop_capture);
      }
      return result();

    case MicrophoneEventKind::g0_ignored:
      return result();

    case MicrophoneEventKind::capture_started:
      if (state_ != MicrophoneState::starting) {
        return result();
      }
      if (!sink_ready_) {
        state_ = MicrophoneState::unavailable;
        return result(MicrophoneCommand::stop_capture);
      }
      state_ = target_rate_ == AudioSampleRate::hz24000
                   ? MicrophoneState::live24
                   : MicrophoneState::live16;
      consecutive_bad_windows_ = 0;
      return result();

    case MicrophoneEventKind::capture_stopped:
      if (state_ == MicrophoneState::stopping) {
        state_ = sink_ready_ ? MicrophoneState::ready
                             : MicrophoneState::unavailable;
      }
      return result();

    case MicrophoneEventKind::loss_window_good:
      if (state_ == MicrophoneState::live24 ||
          state_ == MicrophoneState::live16) {
        consecutive_bad_windows_ = 0;
      }
      return result();

    case MicrophoneEventKind::loss_window_bad:
      if (state_ != MicrophoneState::live24 &&
          state_ != MicrophoneState::live16) {
        return result();
      }
      if (consecutive_bad_windows_ < 2) {
        ++consecutive_bad_windows_;
      }
      if (consecutive_bad_windows_ < 2) {
        return result();
      }
      consecutive_bad_windows_ = 0;
      if (state_ == MicrophoneState::live24) {
        state_ = MicrophoneState::starting;
        target_rate_ = AudioSampleRate::hz16000;
        return result(MicrophoneCommand::restart_capture_16k, true);
      }
      state_ = MicrophoneState::starting;
      target_rate_ = AudioSampleRate::hz16000;
      return result(MicrophoneCommand::restart_capture_16k, true);

    case MicrophoneEventKind::fatal_error: {
      const bool must_stop = capture_active_or_pending();
      state_ = MicrophoneState::error;
      consecutive_bad_windows_ = 0;
      return result(must_stop ? MicrophoneCommand::stop_capture
                              : MicrophoneCommand::none);
    }

    case MicrophoneEventKind::reset: {
      const bool must_stop = capture_active_or_pending();
      state_ = MicrophoneState::unavailable;
      target_rate_ = preferred_rate_;
      sink_ready_ = false;
      consecutive_bad_windows_ = 0;
      return result(must_stop ? MicrophoneCommand::stop_capture
                              : MicrophoneCommand::none);
    }
  }
  return result();
}
