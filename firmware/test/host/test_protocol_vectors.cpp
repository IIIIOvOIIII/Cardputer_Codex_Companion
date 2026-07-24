#include <cassert>

#include "phase0_protocol_vectors.hpp"
#include "probe/protocol_codec.hpp"

int main() {
  assert(protocol_vectors::source_files_verified());

  assert(protocol_vectors::pairing::source_sha256.size() == 32);
  assert(protocol_vectors::gatt::source_sha256.size() == 32);
  assert(protocol_vectors::wss::source_sha256.size() == 32);

  const auto encoded = encode_pairing_transcript(protocol_vectors::pairing::input);
  assert(encoded == protocol_vectors::pairing::canonical_transcript);

  const auto derived = derive_pairing_values(protocol_vectors::pairing::input);
  assert(derived == protocol_vectors::pairing::expected_values);

  auto mutated = protocol_vectors::pairing::input;
  mutated.device_nonce_hex[0] = static_cast<char>(mutated.device_nonce_hex[0] ^ 0x01);
  const auto mutated_transcript = encode_pairing_transcript(mutated);
  const auto mutated_derived = derive_pairing_values(mutated);
  assert(mutated_transcript != protocol_vectors::pairing::canonical_transcript);
  assert(mutated_derived != protocol_vectors::pairing::expected_values);

  return 0;
}
