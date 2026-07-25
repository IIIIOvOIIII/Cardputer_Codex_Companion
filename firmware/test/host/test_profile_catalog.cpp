#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "product/profile_catalog.hpp"
#include "product/profile_codec.hpp"

struct MemoryCatalogBackend final : ProfileCatalogBackend {
  std::vector<uint8_t> bytes =
      std::vector<uint8_t>(kProfileCatalogBankBOffset +
                               kProfileCatalogBankBytes,
                           0xff);
  std::size_t maximum_read = 0;
  std::size_t maximum_write = 0;
  bool fail_after_header = false;
  std::size_t writes = 0;

  bool read(std::size_t offset, std::span<uint8_t> output) override {
    maximum_read = std::max(maximum_read, output.size());
    if (offset > bytes.size() || output.size() > bytes.size() - offset) {
      return false;
    }
    std::copy_n(bytes.begin() + offset, output.size(), output.begin());
    return true;
  }

  bool erase(std::size_t offset, std::size_t length) override {
    if (offset > bytes.size() || length > bytes.size() - offset) return false;
    std::fill_n(bytes.begin() + offset, length, 0xff);
    return true;
  }

  bool write(
      std::size_t offset,
      std::span<const uint8_t> input
  ) override {
    maximum_write = std::max(maximum_write, input.size());
    if (fail_after_header && writes++ > 0) return false;
    if (offset > bytes.size() || input.size() > bytes.size() - offset) {
      return false;
    }
    for (std::size_t index = 0; index < input.size(); ++index) {
      bytes[offset + index] &= input[index];
    }
    return true;
  }
};

std::string encoded(Profile profile) {
  std::string json;
  assert(encode_profile(profile, json) == ProfileCodecResult::ok);
  return json;
}

int main() {
  MemoryCatalogBackend backend;
  Profile legacy = safe_profile();
  const std::string legacy_json = encoded(legacy);
  ProfileCatalogStore store(backend);
  assert(store.load(legacy_json) == ProfileCatalogLoadResult::migrated);
  std::array<ProfileSummary, 5> summaries{};
  std::size_t count = 0;
  assert(store.list(summaries, &count) == ProfileCatalogResult::ok);
  assert(count == 2);
  assert(std::string(summaries[0].id.data()) == "SAFE");
  assert(summaries[0].builtin);
  assert(std::string(summaries[1].name.data()) == "IMPORTED");
  assert(!summaries[1].builtin);
  assert(backend.maximum_read <= 4096);
  assert(backend.maximum_write <= 4096);

  Profile imported;
  const std::string imported_id(summaries[1].id.data());
  assert(store.read(imported_id, imported) == ProfileCatalogResult::ok);
  assert(imported.name == "IMPORTED");

  std::array<char, 9> created{};
  assert(store.create("SAFE", "ONE", &created) == ProfileCatalogResult::ok);
  Profile one;
  assert(store.read(created.data(), one) == ProfileCatalogResult::ok);
  assert(one.name == "ONE");
  const uint32_t prior_sequence = store.sequence();
  one.bindings[0].action.kind = ActionKind::text_utf8;
  one.bindings[0].action.text = "hello";
  assert(store.publish(created.data(), one, one.revision) ==
         ProfileCatalogResult::ok);
  assert(store.sequence() == prior_sequence + 1);
  assert(store.read(created.data(), one) == ProfileCatalogResult::ok);
  assert(one.revision == 2);
  assert(one.bindings[0].action.text == "hello");

  std::array<char, 9> second{};
  std::array<char, 9> third{};
  assert(store.create(std::nullopt, "TWO", &second) ==
         ProfileCatalogResult::ok);
  assert(store.create(std::nullopt, "THREE", &third) ==
         ProfileCatalogResult::ok);
  std::array<char, 9> fifth{};
  assert(store.create(std::nullopt, "TOO MANY", &fifth) ==
         ProfileCatalogResult::capacity);
  assert(store.remove("SAFE") == ProfileCatalogResult::builtin_profile);
  assert(store.activate(created.data()) == ProfileCatalogResult::ok);
  assert(store.remove(created.data()) == ProfileCatalogResult::active_profile);

  MemoryCatalogBackend interrupted = backend;
  ProfileCatalogStore prior(interrupted);
  assert(prior.load(std::nullopt) == ProfileCatalogLoadResult::loaded);
  const uint32_t stable_sequence = prior.sequence();
  interrupted.fail_after_header = true;
  interrupted.writes = 0;
  Profile changed;
  assert(prior.read(second.data(), changed) == ProfileCatalogResult::ok);
  assert(prior.publish(second.data(), changed, changed.revision) ==
         ProfileCatalogResult::storage_error);
  interrupted.fail_after_header = false;
  ProfileCatalogStore recovered(interrupted);
  assert(recovered.load(std::nullopt) == ProfileCatalogLoadResult::loaded);
  assert(recovered.sequence() == stable_sequence);

  MemoryCatalogBackend corrupt = backend;
  corrupt.bytes[kProfileCatalogBankAOffset] ^= 0xff;
  corrupt.bytes[kProfileCatalogBankBOffset] ^= 0xff;
  ProfileCatalogStore invalid(corrupt);
  assert(invalid.load(std::nullopt) == ProfileCatalogLoadResult::empty);

  MemoryCatalogBackend crc_corrupt = backend;
  const std::size_t payload_byte =
      store.active_bank_offset() + kProfileCatalogHeaderBytes;
  crc_corrupt.bytes[payload_byte] ^= 1;
  ProfileCatalogStore crc_recovery(crc_corrupt);
  assert(crc_recovery.load(std::nullopt) == ProfileCatalogLoadResult::loaded);
  assert(crc_recovery.sequence() < store.sequence());

  return 0;
}
