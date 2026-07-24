#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "product/product_types.hpp"

inline constexpr std::size_t kProfileLayerCount = 4;
inline constexpr std::size_t kProfileBindingCount =
    kProfileLayerCount * kPhysicalKeyCount;
inline constexpr std::size_t kMaxTextUtf8Bytes = 1024;
inline constexpr std::size_t kMaxSequenceSteps = 16;
inline constexpr uint32_t kMaxSequenceDelayMs = 10000;

enum class ActionKind : uint8_t {
  passthrough,
  hid_chord,
  text_utf8,
  input_sequence,
  device_action,
  codex_action,
  disabled,
};

enum class DeviceAction : uint8_t {
  none,
  toggle_mode,
  next_profile,
  previous_profile,
  open_pairing,
  reconnect_wifi,
};

enum class CodexAction : uint8_t {
  none,
  select_next_session,
  select_previous_session,
  new_session,
  interrupt,
  approve,
  reject,
  provide_input,
};

struct SequenceStep {
  ActionKind kind = ActionKind::disabled;
  uint8_t modifiers = 0;
  std::array<uint8_t, 6> usages{};
  uint8_t usage_count = 0;
  uint32_t delay_ms = 0;
  std::string text;
};

struct KeyAction {
  ActionKind kind = ActionKind::passthrough;
  uint8_t modifiers = 0;
  std::array<uint8_t, 6> usages{};
  uint8_t usage_count = 0;
  std::string text;
  std::array<SequenceStep, kMaxSequenceSteps> sequence{};
  uint8_t sequence_count = 0;
  DeviceAction device = DeviceAction::none;
  CodexAction codex = CodexAction::none;
};

struct KeyBinding {
  KeyAction action;
};

struct Profile {
  std::string name;
  uint32_t revision = 1;
  std::array<KeyBinding, kProfileBindingCount> bindings{};
};

enum class ProfileError : uint8_t {
  none,
  invalid_name,
  invalid_revision,
  text_too_long,
  too_many_usages,
  too_many_steps,
  delay_too_long,
  nested_sequence,
};

Profile safe_profile();
ProfileError validate_profile(const Profile& profile);
uint32_t profile_crc32(const Profile& profile);
