#include <cassert>
#include <string>

#include "product/profile.hpp"
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
  sequence_a.bindings[0].action.sequence_count = 1;
  sequence_a.bindings[0].action.sequence[0].kind = ActionKind::hid_chord;
  sequence_a.bindings[0].action.sequence[0].usage_count = 1;
  sequence_a.bindings[0].action.sequence[0].usages[0] = 0x06;
  Profile sequence_b = sequence_a;
  sequence_b.bindings[0].action.sequence[0].usages[0] = 0x07;
  assert(profile_crc32(sequence_a) != profile_crc32(sequence_b));

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
  return 0;
}
