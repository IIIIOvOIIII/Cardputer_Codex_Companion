#include "product/profile_codec.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <vector>

#include "cJSON.h"

namespace {
void reset_profile(Profile& profile) {
  profile.name.clear();
  profile.revision = 1;
  for (KeyBinding& binding : profile.bindings) {
    binding.action = KeyAction{};
  }
}

const char* action_name(ActionKind kind) {
  switch (kind) {
    case ActionKind::passthrough: return "passthrough";
    case ActionKind::hid_chord: return "hid_chord";
    case ActionKind::text_utf8: return "text_utf8";
    case ActionKind::input_sequence: return "input_sequence";
    case ActionKind::device_action: return "device_action";
    case ActionKind::codex_action: return "codex_action";
    case ActionKind::disabled: return "disabled";
  }
  return "disabled";
}

bool parse_action(const char* value, ActionKind* output) {
  if (value == nullptr || output == nullptr) return false;
  constexpr std::array values{
      std::pair{"passthrough", ActionKind::passthrough},
      std::pair{"hid_chord", ActionKind::hid_chord},
      std::pair{"text_utf8", ActionKind::text_utf8},
      std::pair{"input_sequence", ActionKind::input_sequence},
      std::pair{"device_action", ActionKind::device_action},
      std::pair{"codex_action", ActionKind::codex_action},
      std::pair{"disabled", ActionKind::disabled},
  };
  for (const auto& [name, kind] : values) {
    if (std::strcmp(value, name) == 0) {
      *output = kind;
      return true;
    }
  }
  return false;
}

const char* device_action_name(DeviceAction action) {
  switch (action) {
    case DeviceAction::toggle_mode: return "toggle_mode";
    case DeviceAction::next_profile: return "next_profile";
    case DeviceAction::previous_profile: return "previous_profile";
    case DeviceAction::open_pairing: return "open_pairing";
    case DeviceAction::reconnect_wifi: return "reconnect_wifi";
    case DeviceAction::none: return "none";
  }
  return "none";
}

DeviceAction parse_device_action(const char* value) {
  if (value == nullptr) return DeviceAction::none;
  constexpr std::array values{
      std::pair{"toggle_mode", DeviceAction::toggle_mode},
      std::pair{"next_profile", DeviceAction::next_profile},
      std::pair{"previous_profile", DeviceAction::previous_profile},
      std::pair{"open_pairing", DeviceAction::open_pairing},
      std::pair{"reconnect_wifi", DeviceAction::reconnect_wifi},
  };
  for (const auto& [name, action] : values) {
    if (std::strcmp(value, name) == 0) return action;
  }
  return DeviceAction::none;
}

const char* codex_action_name_local(CodexAction action) {
  switch (action) {
    case CodexAction::select_next_session: return "select_next";
    case CodexAction::select_previous_session: return "select_previous";
    case CodexAction::new_session: return "new";
    case CodexAction::interrupt: return "interrupt";
    case CodexAction::approve: return "approve";
    case CodexAction::reject: return "reject";
    case CodexAction::provide_input: return "provide_input";
    case CodexAction::none: return "none";
  }
  return "none";
}

CodexAction parse_codex_action_local(const char* value) {
  if (value == nullptr) return CodexAction::none;
  constexpr std::array values{
      std::pair{"select_next", CodexAction::select_next_session},
      std::pair{"select_previous", CodexAction::select_previous_session},
      std::pair{"new", CodexAction::new_session},
      std::pair{"interrupt", CodexAction::interrupt},
      std::pair{"approve", CodexAction::approve},
      std::pair{"reject", CodexAction::reject},
      std::pair{"provide_input", CodexAction::provide_input},
  };
  for (const auto& [name, action] : values) {
    if (std::strcmp(value, name) == 0) return action;
  }
  return CodexAction::none;
}

class JsonEncoder {
 public:
  explicit JsonEncoder(std::string* output) : output_(output) {}

  void literal(std::string_view value) {
    size_ += value.size();
    if (output_ != nullptr) output_->append(value);
  }

  void number(uint32_t value) {
    std::array<char, 10> buffer{};
    const auto result =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    literal(std::string_view(buffer.data(), result.ptr - buffer.data()));
  }

  void string(std::string_view value) {
    literal("\"");
    for (const unsigned char byte : value) {
      switch (byte) {
        case '"': literal("\\\""); break;
        case '\\': literal("\\\\"); break;
        case '\b': literal("\\b"); break;
        case '\f': literal("\\f"); break;
        case '\n': literal("\\n"); break;
        case '\r': literal("\\r"); break;
        case '\t': literal("\\t"); break;
        default:
          if (byte < 0x20) {
            constexpr char digits[] = "0123456789abcdef";
            std::array<char, 6> escaped{
                '\\', 'u', '0', '0', digits[byte >> 4], digits[byte & 0x0f]};
            literal(std::string_view(escaped.data(), escaped.size()));
          } else {
            const char value_byte = static_cast<char>(byte);
            literal(std::string_view(&value_byte, 1));
          }
      }
    }
    literal("\"");
  }

  std::size_t size() const { return size_; }

 private:
  std::string* output_;
  std::size_t size_ = 0;
};

void encode_action(JsonEncoder& encoder, ActionKind kind, uint8_t modifiers,
                   const std::array<uint8_t, 6>& usage_values,
                   uint8_t usage_count, std::string_view text,
                   const std::vector<SequenceStep>* sequence,
                   DeviceAction device, CodexAction codex,
                   uint32_t delay_ms = 0) {
  encoder.literal("{\"kind\":");
  encoder.string(action_name(kind));
  if (modifiers != 0) {
    encoder.literal(",\"modifiers\":");
    encoder.number(modifiers);
  }
  if (usage_count != 0) {
    encoder.literal(",\"usages\":[");
    for (uint8_t index = 0; index < usage_count; ++index) {
      if (index != 0) encoder.literal(",");
      encoder.number(usage_values[index]);
    }
    encoder.literal("]");
  }
  if (!text.empty()) {
    encoder.literal(",\"text\":");
    encoder.string(text);
  }
  if (kind == ActionKind::device_action) {
    encoder.literal(",\"device\":");
    encoder.string(device_action_name(device));
  }
  if (kind == ActionKind::codex_action) {
    encoder.literal(",\"codex\":");
    encoder.string(codex_action_name_local(codex));
  }
  if (kind == ActionKind::input_sequence && sequence != nullptr) {
    encoder.literal(",\"sequence\":[");
    for (std::size_t index = 0; index < sequence->size(); ++index) {
      if (index != 0) encoder.literal(",");
      const SequenceStep& step = (*sequence)[index];
      encode_action(
          encoder,
          step.kind, step.modifiers, step.usages, step.usage_count, step.text,
          nullptr, DeviceAction::none, CodexAction::none, step.delay_ms);
    }
    encoder.literal("]");
  }
  if (delay_ms != 0) {
    encoder.literal(",\"delay_ms\":");
    encoder.number(delay_ms);
  }
  encoder.literal("}");
}

bool parse_leaf(const cJSON* item, ActionKind& kind, uint8_t& modifiers,
                std::array<uint8_t, 6>& usages, uint8_t& usage_count,
                std::string& text, DeviceAction* device,
                CodexAction* codex) {
  if (!cJSON_IsObject(item)) return false;
  const cJSON* kind_json = cJSON_GetObjectItemCaseSensitive(item, "kind");
  if (!cJSON_IsString(kind_json) ||
      !parse_action(kind_json->valuestring, &kind)) {
    return false;
  }
  modifiers = 0;
  usage_count = 0;
  usages = {};
  text.clear();
  const cJSON* modifiers_json =
      cJSON_GetObjectItemCaseSensitive(item, "modifiers");
  if (modifiers_json != nullptr) {
    if (!cJSON_IsNumber(modifiers_json) || modifiers_json->valueint < 0 ||
        modifiers_json->valueint > 255) {
      return false;
    }
    modifiers = static_cast<uint8_t>(modifiers_json->valueint);
  }
  const cJSON* usages_json = cJSON_GetObjectItemCaseSensitive(item, "usages");
  if (usages_json != nullptr) {
    if (!cJSON_IsArray(usages_json) ||
        cJSON_GetArraySize(usages_json) > 6) {
      return false;
    }
    usage_count = static_cast<uint8_t>(cJSON_GetArraySize(usages_json));
    for (uint8_t index = 0; index < usage_count; ++index) {
      const cJSON* value = cJSON_GetArrayItem(usages_json, index);
      if (!cJSON_IsNumber(value) || value->valueint < 0 ||
          value->valueint > 255) {
        return false;
      }
      usages[index] = static_cast<uint8_t>(value->valueint);
    }
  }
  const cJSON* text_json = cJSON_GetObjectItemCaseSensitive(item, "text");
  if (text_json != nullptr) {
    if (!cJSON_IsString(text_json)) return false;
    text = text_json->valuestring;
  }
  if (device != nullptr) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(item, "device");
    *device = parse_device_action(
        cJSON_IsString(value) ? value->valuestring : nullptr);
  }
  if (codex != nullptr) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(item, "codex");
    *codex = parse_codex_action_local(
        cJSON_IsString(value) ? value->valuestring : nullptr);
  }
  return true;
}
}  // namespace

ProfileCodecResult encode_profile(const Profile& profile,
                                  std::string& output) {
  output.clear();
  if (validate_profile(profile) != ProfileError::none) {
    return ProfileCodecResult::invalid;
  }

  const auto encode = [&profile](JsonEncoder& encoder) {
    encoder.literal("{\"name\":");
    encoder.string(profile.name);
    encoder.literal(",\"revision\":");
    encoder.number(profile.revision);
    encoder.literal(",\"bindings\":[");
    for (std::size_t index = 0; index < profile.bindings.size(); ++index) {
      if (index != 0) encoder.literal(",");
      const KeyAction& action = profile.bindings[index].action;
      if (action.kind == ActionKind::passthrough) {
        encoder.literal("null");
        continue;
      }
      encode_action(
          encoder, action.kind, action.modifiers, action.usages,
          action.usage_count, action.text, &action.sequence, action.device,
          action.codex);
    }
    encoder.literal("]}");
  };

  JsonEncoder counter(nullptr);
  encode(counter);
  if (counter.size() > kProfileJsonMaximumBytes) {
    return ProfileCodecResult::too_large;
  }

  try {
    output.clear();
    output.reserve(counter.size());
    JsonEncoder writer(&output);
    encode(writer);
  } catch (const std::bad_alloc&) {
    output.clear();
    return ProfileCodecResult::allocation_error;
  }
  return ProfileCodecResult::ok;
}

ProfileCodecResult decode_profile(std::string_view json, Profile& output) {
  if (json.empty() || json.size() > kProfileJsonMaximumBytes) {
    return json.size() > kProfileJsonMaximumBytes
               ? ProfileCodecResult::too_large
               : ProfileCodecResult::malformed;
  }
  cJSON* root = cJSON_ParseWithLength(json.data(), json.size());
  if (root == nullptr) return ProfileCodecResult::malformed;
  const cJSON* name = cJSON_GetObjectItemCaseSensitive(root, "name");
  const cJSON* revision = cJSON_GetObjectItemCaseSensitive(root, "revision");
  const cJSON* bindings = cJSON_GetObjectItemCaseSensitive(root, "bindings");
  const bool revision_valid =
      cJSON_IsNumber(revision) && std::isfinite(revision->valuedouble) &&
      revision->valuedouble >= 1 &&
      revision->valuedouble <= std::numeric_limits<uint32_t>::max() &&
      std::floor(revision->valuedouble) == revision->valuedouble;
  if (!cJSON_IsString(name) || !revision_valid ||
      !cJSON_IsArray(bindings) ||
      cJSON_GetArraySize(bindings) != kProfileBindingCount) {
    cJSON_Delete(root);
    return ProfileCodecResult::malformed;
  }
  reset_profile(output);
  output.name = name->valuestring;
  output.revision = static_cast<uint32_t>(revision->valuedouble);
  bool valid = true;
  for (std::size_t index = 0; valid && index < output.bindings.size();
       ++index) {
    const cJSON* item =
        cJSON_GetArrayItem(bindings, static_cast<int>(index));
    KeyAction& action = output.bindings[index].action;
    if (cJSON_IsNull(item)) {
      action = {};
      continue;
    }
    valid = parse_leaf(item, action.kind, action.modifiers, action.usages,
                       action.usage_count, action.text, &action.device,
                       &action.codex);
    if (!valid || action.kind != ActionKind::input_sequence) continue;
    const cJSON* steps = cJSON_GetObjectItemCaseSensitive(item, "sequence");
    if (!cJSON_IsArray(steps) ||
        cJSON_GetArraySize(steps) >
            static_cast<int>(kMaxSequenceSteps)) {
      valid = false;
      continue;
    }
    action.sequence.resize(cJSON_GetArraySize(steps));
    for (std::size_t step_index = 0;
         valid && step_index < action.sequence.size(); ++step_index) {
      const cJSON* step_json =
          cJSON_GetArrayItem(steps, static_cast<int>(step_index));
      SequenceStep& step = action.sequence[step_index];
      valid = parse_leaf(step_json, step.kind, step.modifiers, step.usages,
                         step.usage_count, step.text, nullptr, nullptr);
      const cJSON* delay =
          cJSON_GetObjectItemCaseSensitive(step_json, "delay_ms");
      if (valid && delay != nullptr) {
        valid = cJSON_IsNumber(delay) && delay->valuedouble >= 0 &&
                delay->valuedouble <= kMaxSequenceDelayMs &&
                std::floor(delay->valuedouble) == delay->valuedouble;
        if (valid) {
          step.delay_ms = static_cast<uint32_t>(delay->valuedouble);
        }
      }
    }
  }
  cJSON_Delete(root);
  if (!valid || validate_profile(output) != ProfileError::none) {
    reset_profile(output);
    return ProfileCodecResult::malformed;
  }
  return ProfileCodecResult::ok;
}
