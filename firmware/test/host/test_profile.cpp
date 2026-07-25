#include <cassert>
#include <string>

#include "product/profile.hpp"
#include "product/profile_codec.hpp"
#include "product/profile_store.hpp"

struct MemoryProfileBackend final : ProfileStoreBackend {
  bool read(uint8_t slot, ProfileBlob& output) override {
    if (!present[slot]) return false;
    output = slots[slot];
    return true;
  }
  bool write(uint8_t slot, const ProfileBlob& input) override {
    slots[slot] = input;
    present[slot] = true;
    return true;
  }
  ProfileBlob slots[2]{};
  bool present[2]{};
};

int main() {
  static_assert(sizeof(Profile) <= 24 * 1024,
                "Profile must leave enough internal RAM for BLE, Wi-Fi, and TLS");

  Profile safe = safe_profile();
  assert(safe.name == "SAFE");
  assert(safe.revision == 1);
  assert(validate_profile(safe) == ProfileError::none);

  Profile invalid = safe;
  invalid.bindings[0].action.kind = ActionKind::text_utf8;
  invalid.bindings[0].action.text.assign(1025, 'x');
  assert(validate_profile(invalid) == ProfileError::text_too_long);

  Profile sequence_a = safe;
  sequence_a.bindings[0].action.kind = ActionKind::input_sequence;
  sequence_a.bindings[0].action.sequence.resize(1);
  sequence_a.bindings[0].action.sequence[0].kind = ActionKind::hid_chord;
  sequence_a.bindings[0].action.sequence[0].usage_count = 1;
  sequence_a.bindings[0].action.sequence[0].usages[0] = 0x06;
  Profile sequence_b = sequence_a;
  sequence_b.bindings[0].action.sequence[0].usages[0] = 0x07;
  assert(profile_crc32(sequence_a) != profile_crc32(sequence_b));

  Profile oversized_sequence = safe;
  oversized_sequence.bindings[0].action.kind = ActionKind::input_sequence;
  oversized_sequence.bindings[0].action.sequence.resize(
      kMaxSequenceSteps + 1);
  assert(validate_profile(oversized_sequence) == ProfileError::too_many_steps);

  MemoryProfileBackend backend;
  ProfileStore store(backend);
  Profile loaded;
  assert(store.load(loaded) == ProfileLoadResult::safe_fallback);
  assert(loaded.name == "SAFE");

  Profile custom = safe;
  custom.name = "CODEX";
  custom.revision = 2;
  assert(store.publish(custom, 1) == ProfilePublishResult::ok);
  assert(store.publish(custom, 1) == ProfilePublishResult::revision_conflict);
  assert(store.load(loaded) == ProfileLoadResult::stored);
  assert(loaded.name == "CODEX");

  backend.slots[1].crc32 ^= 1;
  assert(store.load(loaded) == ProfileLoadResult::safe_fallback);

  std::string encoded;
  assert(encode_profile(safe, encoded) == ProfileCodecResult::ok);
  std::size_t null_count = 0;
  for (std::size_t at = 0;
       (at = encoded.find("null", at)) != std::string::npos; at += 4) {
    ++null_count;
  }
  assert(null_count == kProfileBindingCount);

  Profile actions = safe;
  actions.name = "ACTIONS";
  actions.revision = 9;
  actions.bindings[0].action.kind = ActionKind::hid_chord;
  actions.bindings[0].action.modifiers = 4;
  actions.bindings[0].action.usage_count = 1;
  actions.bindings[0].action.usages[0] = 25;
  actions.bindings[1].action.kind = ActionKind::text_utf8;
  actions.bindings[1].action.text = "中文";
  actions.bindings[2].action.kind = ActionKind::device_action;
  actions.bindings[2].action.device = DeviceAction::next_profile;
  actions.bindings[3].action.kind = ActionKind::codex_action;
  actions.bindings[3].action.codex = CodexAction::interrupt;
  actions.bindings[4].action.kind = ActionKind::disabled;
  actions.bindings[5] = sequence_a.bindings[0];
  assert(encode_profile(actions, encoded) == ProfileCodecResult::ok);
  Profile decoded;
  assert(decode_profile(encoded, decoded) == ProfileCodecResult::ok);
  assert(decoded.name == "ACTIONS");
  assert(decoded.revision == 9);
  assert(decoded.bindings[0].action.kind == ActionKind::hid_chord);
  assert(decoded.bindings[1].action.text == "中文");
  assert(decoded.bindings[2].action.device == DeviceAction::next_profile);
  assert(decoded.bindings[3].action.codex == CodexAction::interrupt);
  assert(decoded.bindings[4].action.kind == ActionKind::disabled);
  assert(decoded.bindings[5].action.sequence.size() == 1);

  assert(decode_profile(
             R"({"name":"BAD","revision":1,"bindings":[]})", decoded) ==
         ProfileCodecResult::malformed);
  assert(decode_profile(
             R"({"name":"BAD","revision":1,"bindings":[{"kind":"text_utf8","text":")" +
                 std::string(1025, 'x') +
                 R"("}]})",
             decoded) == ProfileCodecResult::malformed);
  assert(decode_profile(
             R"({"name":"BAD","revision":1,"bindings":[{"kind":"input_sequence","sequence":[{"kind":"input_sequence"}]}]})",
             decoded) == ProfileCodecResult::malformed);

  Profile over_wire = safe;
  for (std::size_t index = 0; index < 20; ++index) {
    over_wire.bindings[index].action.kind = ActionKind::text_utf8;
    over_wire.bindings[index].action.text.assign(1000, 'x');
  }
  assert(validate_profile(over_wire) == ProfileError::none);
  assert(encode_profile(over_wire, encoded) == ProfileCodecResult::too_large);
  return 0;
}
