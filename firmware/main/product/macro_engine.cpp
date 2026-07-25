#include "product/macro_engine.hpp"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace {
constexpr uint16_t kHidPressDurationMs = 30;
constexpr uint16_t kHidReleaseDurationMs = 10;
constexpr uint8_t kLeftShiftModifier = 0x02;

bool ascii_hid_report(char character, HidReport* report) {
  if (report == nullptr) return false;
  *report = HidReport{};

  if (character >= 'a' && character <= 'z') {
    report->keys[0] = static_cast<uint8_t>(0x04 + character - 'a');
    return true;
  }
  if (character >= 'A' && character <= 'Z') {
    report->modifiers = kLeftShiftModifier;
    report->keys[0] = static_cast<uint8_t>(0x04 + character - 'A');
    return true;
  }
  if (character >= '1' && character <= '9') {
    report->keys[0] = static_cast<uint8_t>(0x1e + character - '1');
    return true;
  }
  if (character == '0') {
    report->keys[0] = 0x27;
    return true;
  }

  switch (character) {
    case '\n':
    case '\r':
      report->keys[0] = 0x28;
      break;
    case '\b':
      report->keys[0] = 0x2a;
      break;
    case '\t':
      report->keys[0] = 0x2b;
      break;
    case ' ':
      report->keys[0] = 0x2c;
      break;
    case '-':
    case '_':
      report->keys[0] = 0x2d;
      break;
    case '=':
    case '+':
      report->keys[0] = 0x2e;
      break;
    case '[':
    case '{':
      report->keys[0] = 0x2f;
      break;
    case ']':
    case '}':
      report->keys[0] = 0x30;
      break;
    case '\\':
    case '|':
      report->keys[0] = 0x31;
      break;
    case ';':
    case ':':
      report->keys[0] = 0x33;
      break;
    case '\'':
    case '"':
      report->keys[0] = 0x34;
      break;
    case '`':
    case '~':
      report->keys[0] = 0x35;
      break;
    case ',':
    case '<':
      report->keys[0] = 0x36;
      break;
    case '.':
    case '>':
      report->keys[0] = 0x37;
      break;
    case '/':
    case '?':
      report->keys[0] = 0x38;
      break;
    case '!':
      report->keys[0] = 0x1e;
      break;
    case '@':
      report->keys[0] = 0x1f;
      break;
    case '#':
      report->keys[0] = 0x20;
      break;
    case '$':
      report->keys[0] = 0x21;
      break;
    case '%':
      report->keys[0] = 0x22;
      break;
    case '^':
      report->keys[0] = 0x23;
      break;
    case '&':
      report->keys[0] = 0x24;
      break;
    case '*':
      report->keys[0] = 0x25;
      break;
    case '(':
      report->keys[0] = 0x26;
      break;
    case ')':
      report->keys[0] = 0x27;
      break;
    default:
      return false;
  }

  switch (character) {
    case '_':
    case '+':
    case '{':
    case '}':
    case '|':
    case ':':
    case '"':
    case '~':
    case '<':
    case '>':
    case '?':
    case '!':
    case '@':
    case '#':
    case '$':
    case '%':
    case '^':
    case '&':
    case '*':
    case '(':
    case ')':
      report->modifiers = kLeftShiftModifier;
      break;
    default:
      break;
  }
  return true;
}

bool is_hid_text(std::string_view text) {
  for (const char character : text) {
    HidReport report;
    if (!ascii_hid_report(character, &report)) return false;
  }
  return true;
}
}  // namespace

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
      sink_.delay_hid(kHidPressDurationMs);
      release_all();
      return MacroResult::ok;
    }
    case ActionKind::text_utf8:
      if (text.empty() || text.size() > kMaxTextUtf8Bytes) {
        release_all();
        return MacroResult::invalid;
      }
      if (is_hid_text(text)) {
        for (const char character : text) {
          HidReport report;
          ascii_hid_report(character, &report);
          sink_.send_hid(report);
          sink_.delay_hid(kHidPressDurationMs);
          release_all();
          sink_.delay_hid(kHidReleaseDurationMs);
        }
        return MacroResult::ok;
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
  if (action.sequence.size() > kMaxSequenceSteps) {
    release_all();
    return MacroResult::invalid;
  }
  uint32_t total_delay = 0;
  for (const SequenceStep& step : action.sequence) {
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
