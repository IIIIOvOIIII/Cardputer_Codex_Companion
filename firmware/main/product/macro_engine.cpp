#include "product/macro_engine.hpp"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

void MacroEngine::release_all() {
  sink_.send_hid(HidReport{});
}

MacroResult MacroEngine::execute_leaf(
    ActionKind kind, uint8_t modifiers,
    const std::array<uint8_t, 6>& usages, uint8_t usage_count,
    std::string_view text, DeviceAction device, CodexAction codex) {
  switch (kind) {
    case ActionKind::passthrough:
    case ActionKind::disabled:
      return MacroResult::ok;
    case ActionKind::hid_chord: {
      if (usage_count > usages.size()) {
        release_all();
        return MacroResult::invalid;
      }
      HidReport report{.modifiers = modifiers};
      for (uint8_t index = 0; index < usage_count; ++index) {
        report.keys[index] = usages[index];
      }
      sink_.send_hid(report);
      release_all();
      return MacroResult::ok;
    }
    case ActionKind::text_utf8:
      if (text.empty() || text.size() > kMaxTextUtf8Bytes) {
        release_all();
        return MacroResult::invalid;
      }
      if (!sink_.send_text(next_operation_id_++, text)) {
        release_all();
        return MacroResult::unavailable;
      }
      return MacroResult::ok;
    case ActionKind::device_action:
      sink_.device_action(device);
      return MacroResult::ok;
    case ActionKind::codex_action:
      return sink_.codex_action(codex) ? MacroResult::ok
                                      : MacroResult::unavailable;
    case ActionKind::input_sequence:
      return MacroResult::invalid;
  }
  return MacroResult::invalid;
}

MacroResult MacroEngine::execute(const KeyAction& action) {
  if (action.kind != ActionKind::input_sequence) {
    return execute_leaf(action.kind, action.modifiers, action.usages,
                        action.usage_count, action.text, action.device,
                        action.codex);
  }
  if (action.sequence_count > kMaxSequenceSteps) {
    release_all();
    return MacroResult::invalid;
  }
  uint32_t total_delay = 0;
  for (uint8_t index = 0; index < action.sequence_count; ++index) {
    const SequenceStep& step = action.sequence[index];
    if (step.kind == ActionKind::input_sequence ||
        step.delay_ms > kMaxSequenceDelayMs - total_delay) {
      release_all();
      return MacroResult::invalid;
    }
    total_delay += step.delay_ms;
#ifdef ESP_PLATFORM
    if (step.delay_ms != 0) {
      vTaskDelay(pdMS_TO_TICKS(step.delay_ms));
    }
#endif
    const MacroResult result =
        execute_leaf(step.kind, step.modifiers, step.usages,
                     step.usage_count, step.text, DeviceAction::none,
                     CodexAction::none);
    if (result != MacroResult::ok) {
      release_all();
      return result;
    }
  }
  release_all();
  return MacroResult::ok;
}
