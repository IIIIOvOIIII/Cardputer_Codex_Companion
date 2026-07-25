#include <cassert>
#include <string>

#include "product/companion_protocol.hpp"

int main() {
  CompanionProtocol protocol;
  const std::string snapshot =
      R"({"type":"snapshot","sequence":7,"session_id":"s1","title":"agent-loop","cwd":"/tmp/Cardputer","state":"WAITING","approvals":2,"inputs":1,"pet_id":"rocky","pet_digest":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","pet_state":"waiting"})";
  assert(protocol.apply(snapshot, 1000) == CompanionMessageResult::snapshot);
  assert(protocol.snapshot().sequence == 7);
  assert(protocol.snapshot().title == "agent-loop");
  assert(protocol.snapshot().approvals == 2);
  assert(protocol.snapshot().pet_id == "rocky");
  assert(protocol.snapshot().pet_state == PetState::waiting);
  assert(protocol.snapshot().pet_digest.size() == 64);
  assert(effective_pet_state(false, PetState::working) == PetState::working);
  assert(effective_pet_state(true, PetState::working) == PetState::waiting);
  assert(!protocol.stale(30999));
  assert(protocol.stale(31000));

  assert(parse_codex_action("interrupt") == CodexAction::interrupt);
  assert(parse_codex_action("approve") == CodexAction::approve);
  assert(parse_codex_action("shell") == CodexAction::none);
  assert(protocol.apply(R"({"type":"snapshot","sequence":9})", 2000) ==
         CompanionMessageResult::resync_required);
  const std::string restarted =
      R"({"type":"snapshot","sequence":1,"session_id":"s2","title":"restarted","cwd":"/tmp/Cardputer","state":"idle","approvals":0,"inputs":0})";
  assert(protocol.apply(restarted, 31000) == CompanionMessageResult::snapshot);
  assert(protocol.snapshot().sequence == 1);
  assert(protocol.snapshot().title == "restarted");
  protocol.heartbeat(42000);
  assert(!protocol.stale(71999));
  assert(protocol.stale(72000));
  return 0;
}
