#include <cassert>
#include <optional>
#include <string>

#include "product/companion_protocol.hpp"

int main() {
  CompanionProtocol protocol;
  const std::string snapshot =
      R"({"type":"snapshot","sequence":7,"session_id":"s1","title":"agent-loop","cwd":"/tmp/Cardputer","state":"WAITING","approvals":2,"inputs":1,"pet_id":"rocky","pet_digest":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","pet_state":"waiting","model":"gpt-5.6","thinking_level":"high","fast":true,"limits":[{"scope":"codex","window":"5h","used_percent":38},{"scope":"spark","window":"weekly","used_percent":22}]})";
  assert(protocol.apply(snapshot, 1000) == CompanionMessageResult::snapshot);
  assert(protocol.snapshot().sequence == 7);
  assert(protocol.snapshot().title == "agent-loop");
  assert(protocol.snapshot().approvals == 2);
  assert(protocol.snapshot().pet_id == "rocky");
  assert(protocol.snapshot().pet_state == PetState::waiting);
  assert(protocol.snapshot().pet_digest.size() == 64);
  assert(protocol.snapshot().model == "gpt-5.6");
  assert(protocol.snapshot().thinking_level == "high");
  assert(protocol.snapshot().fast == std::optional<bool>{true});
  assert(protocol.snapshot().limit_count == 2);
  assert(protocol.snapshot().limits[0].scope == CodexLimitScope::codex);
  assert(protocol.snapshot().limits[0].window ==
         CodexLimitWindow::five_hours);
  assert(protocol.snapshot().limits[0].used_percent == 38);
  assert(protocol.snapshot().limits[1].scope == CodexLimitScope::spark);
  assert(protocol.snapshot().limits[1].window == CodexLimitWindow::weekly);
  assert(protocol.snapshot().limits[1].used_percent == 22);
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
  const std::string malformed =
      R"({"type":"snapshot","sequence":2,"session_id":"s2","title":"base-valid","limits":"not-an-array"})";
  assert(protocol.apply(malformed, 32000) == CompanionMessageResult::snapshot);
  assert(protocol.snapshot().title == "base-valid");
  assert(protocol.snapshot().limit_count == 0);
  const std::string bounded =
      R"({"type":"snapshot","sequence":3,"session_id":"s2","limits":[{"scope":"codex","window":"5h","used_percent":0},{"scope":"codex","window":"weekly","used_percent":100},{"scope":"spark","window":"5h","used_percent":101},{"scope":"spark","window":"weekly","used_percent":22},{"scope":"codex","window":"5h","used_percent":33}]})";
  assert(protocol.apply(bounded, 33000) == CompanionMessageResult::snapshot);
  assert(protocol.snapshot().limit_count == 3);
  assert(protocol.snapshot().limits[0].used_percent == 0);
  assert(protocol.snapshot().limits[1].used_percent == 100);
  assert(protocol.snapshot().limits[2].used_percent == 22);
  protocol.heartbeat(42000);
  assert(!protocol.stale(71999));
  assert(protocol.stale(72000));
  return 0;
}
