#include <cassert>

#include "phase0_protocol_vectors.hpp"
#include "probe/pinned_wss_transport.hpp"

int main() {
  auto changed = protocol_vectors::wss::spki_sha256;
  changed[31] = 2;
  assert(spki_pin_matches(protocol_vectors::wss::spki_sha256,
                         protocol_vectors::wss::spki_sha256));
  assert(!spki_pin_matches(protocol_vectors::wss::spki_sha256, changed));

  const auto encoded = encode_wss_auth(protocol_vectors::wss::input);
  assert(encoded == protocol_vectors::wss::canonical_message);
  assert(verify_wss_signature(
      protocol_vectors::wss::input,
      protocol_vectors::wss::signature,
      protocol_vectors::wss::device_public_key));

  auto mutated = protocol_vectors::wss::input;
  mutated.exporter_hex[0] = (mutated.exporter_hex[0] == '0') ? '1' : '0';
  assert(!verify_wss_signature(mutated,
                               protocol_vectors::wss::signature,
                               protocol_vectors::wss::device_public_key));

  return 0;
}
