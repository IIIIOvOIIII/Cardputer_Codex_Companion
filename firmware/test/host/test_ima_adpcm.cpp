#include <array>
#include <cassert>
#include <cstdint>
#include <span>

#include "product/ima_adpcm.hpp"

namespace {

uint64_t fnv1a64(std::span<const uint8_t> bytes) {
  uint64_t hash = 0xcbf29ce484222325ULL;
  for (const uint8_t byte : bytes) {
    hash ^= byte;
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

}  // namespace

int main() {
  std::array<int16_t, 456> silence{};
  std::array<uint8_t, 232> encoded24{};
  size_t written = 0;
  assert(ima_adpcm_encode_block(silence, encoded24, &written) ==
         ImaAdpcmError::none);
  assert(written == 232);
  for (const uint8_t byte : encoded24) {
    assert(byte == 0);
  }
  assert(fnv1a64(encoded24) == 0x60963a2f93896b45ULL);

  std::array<int16_t, 448> positive_ramp{};
  std::array<int16_t, 448> negative_ramp{};
  for (size_t index = 0; index < positive_ramp.size(); ++index) {
    positive_ramp[index] = static_cast<int16_t>(index * 100);
    negative_ramp[index] = static_cast<int16_t>(-static_cast<int>(index * 100));
  }
  std::array<uint8_t, 228> encoded16{};
  assert(ima_adpcm_encode_block(positive_ramp, encoded16, &written) ==
         ImaAdpcmError::none);
  assert(written == 228);

  assert(ima_adpcm_encode_block(negative_ramp, encoded16, &written) ==
         ImaAdpcmError::none);
  assert(written == 228);

  std::array<int16_t, 456> clipping_edges{};
  for (size_t index = 0; index < clipping_edges.size(); ++index) {
    clipping_edges[index] =
        index % 2 == 0 ? INT16_MAX : INT16_MIN;
  }
  assert(ima_adpcm_encode_block(clipping_edges, encoded24, &written) ==
         ImaAdpcmError::none);
  assert(written == 232);
  assert(fnv1a64(encoded24) == 0x90b9ef9a23367b27ULL);

  std::array<uint8_t, 227> undersized{};
  assert(ima_adpcm_encode_block(positive_ramp, undersized, &written) ==
         ImaAdpcmError::output_too_small);
  assert(written == 0);

  std::array<int16_t, 447> invalid_samples{};
  assert(ima_adpcm_encode_block(invalid_samples, encoded16, &written) ==
         ImaAdpcmError::invalid_sample_count);
  assert(written == 0);

  assert(ima_adpcm_encode_block(
             positive_ramp, encoded16,
             static_cast<size_t*>(nullptr)) == ImaAdpcmError::null_output);
  return 0;
}
