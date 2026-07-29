#include "product/g0_dual_action.hpp"

G0DualActionResult execute_g0_dual_action(
    const DeviceSettings& settings,
    G0DualActionSink& sink) {
  if (!settings.g0_chord_enabled) {
    sink.toggle_microphone();
    return G0DualActionResult::microphone_only;
  }

  const bool chord_sent = sink.execute_chord(
      settings.g0_chord_modifiers, settings.g0_chord_usage);
  sink.toggle_microphone();
  return chord_sent
             ? G0DualActionResult::chord_then_microphone
             : G0DualActionResult::chord_failed_microphone_toggled;
}
