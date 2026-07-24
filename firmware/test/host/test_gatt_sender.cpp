#include <cassert>

#include "phase0_protocol_vectors.hpp"
#include "probe/protocol_codec.hpp"

int main() {
  GattSender sender(protocol_vectors::gatt::auth_key);
  assert(!sender.has_connection());

  const auto no_connection = sender.encode(protocol_vectors::gatt::message);
  assert(no_connection.status == GattFrame::Status::no_connection);
  assert(no_connection.authenticated_bytes.empty());

  sender.begin_connection(protocol_vectors::gatt::connection_id);
  const auto frame = sender.encode(protocol_vectors::gatt::message);
  assert(frame.status == GattFrame::Status::ok);
  assert(frame.authenticated_bytes == protocol_vectors::gatt::authenticated_bytes);
  assert(frame.tag == protocol_vectors::gatt::tag);
  assert(frame.counter == protocol_vectors::gatt::initial_counter);
  assert(sender.next_counter() == protocol_vectors::gatt::counter_after_first_frame);

  auto mutated = protocol_vectors::gatt::message;
  mutated.counter += 2;
  const auto malformed = sender.encode(mutated);
  assert(malformed.status == GattFrame::Status::counter_mismatch);

  sender.end_connection();
  assert(!sender.has_connection());
  assert(sender.next_counter() == 0);
  return 0;
}
