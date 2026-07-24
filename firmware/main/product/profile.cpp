#include "product/profile.hpp"

#include <algorithm>
#include <span>
#include <string_view>

namespace {
uint32_t crc32_update(uint32_t crc, std::span<const uint8_t> bytes) {
  for (uint8_t byte : bytes) {
    crc ^= byte;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1) ^ (0xedb88320u & mask);
    }
  }
  return crc;
}

uint32_t crc_string(uint32_t crc, std::string_view value) {
  return crc32_update(
      crc, std::span(reinterpret_cast<const uint8_t*>(value.data()),
                     value.size()));
}

ProfileError validate_leaf(ActionKind kind, uint8_t usage_count,
                           std::string_view text) {
  if (usage_count > 6) {
    return ProfileError::too_many_usages;
  }
  if (kind == ActionKind::text_utf8 && text.size() > kMaxTextUtf8Bytes) {
    return ProfileError::text_too_long;
  }
  return ProfileError::none;
}
}  // namespace

Profile safe_profile() {
  Profile profile;
  profile.name = "SAFE";
  profile.revision = 1;
  for (auto& binding : profile.bindings) {
    binding.action.kind = ActionKind::passthrough;
  }
  return profile;
}

ProfileError validate_profile(const Profile& profile) {
  if (profile.name.empty() || profile.name.size() > 20) {
    return ProfileError::invalid_name;
  }
  if (profile.revision == 0) {
    return ProfileError::invalid_revision;
  }
  for (const KeyBinding& binding : profile.bindings) {
    const KeyAction& action = binding.action;
    if (const ProfileError leaf =
            validate_leaf(action.kind, action.usage_count, action.text);
        leaf != ProfileError::none) {
      return leaf;
    }
    if (action.sequence.size() > kMaxSequenceSteps) {
      return ProfileError::too_many_steps;
    }
    uint32_t delay = 0;
    for (const SequenceStep& step : action.sequence) {
      if (step.kind == ActionKind::input_sequence) {
        return ProfileError::nested_sequence;
      }
      if (const ProfileError leaf =
              validate_leaf(step.kind, step.usage_count, step.text);
          leaf != ProfileError::none) {
        return leaf;
      }
      if (step.delay_ms > kMaxSequenceDelayMs ||
          delay > kMaxSequenceDelayMs - step.delay_ms) {
        return ProfileError::delay_too_long;
      }
      delay += step.delay_ms;
    }
    if (delay > kMaxSequenceDelayMs) {
      return ProfileError::delay_too_long;
    }
  }
  return ProfileError::none;
}

uint32_t profile_crc32(const Profile& profile) {
  uint32_t crc = crc_string(0xffffffffu, profile.name);
  crc = crc32_update(
      crc, std::span(reinterpret_cast<const uint8_t*>(&profile.revision),
                     sizeof(profile.revision)));
  for (const KeyBinding& binding : profile.bindings) {
    const KeyAction& action = binding.action;
    const std::array<uint8_t, 5> header{
        static_cast<uint8_t>(action.kind), action.modifiers,
        action.usage_count, static_cast<uint8_t>(action.sequence.size()),
        static_cast<uint8_t>(static_cast<uint8_t>(action.device) ^
                             static_cast<uint8_t>(action.codex)),
    };
    crc = crc32_update(crc, header);
    crc = crc32_update(crc, action.usages);
    crc = crc_string(crc, action.text);
    for (const SequenceStep& step : action.sequence) {
      const std::array<uint8_t, 3> step_header{
          static_cast<uint8_t>(step.kind), step.modifiers, step.usage_count};
      crc = crc32_update(crc, step_header);
      crc = crc32_update(crc, step.usages);
      crc = crc32_update(
          crc, std::span(reinterpret_cast<const uint8_t*>(&step.delay_ms),
                         sizeof(step.delay_ms)));
      crc = crc_string(crc, step.text);
    }
  }
  return crc ^ 0xffffffffu;
}
