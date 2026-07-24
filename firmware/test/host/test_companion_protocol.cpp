#include <cassert>
#include <string>

#include "product/companion_protocol.hpp"

int main() {
  CompanionProtocol protocol;
  const std::string snapshot =
      R"({"type":"snapshot","sequence":7,"session_id":"s1","title":"agent-loop","cwd":"/tmp/Cardputer","state":"WAITING","approvals":2,"inputs":1})";
  assert(protocol.apply(snapshot, 1000) == CompanionMessageResult::snapshot);
  assert(protocol.snapshot().sequence == 7);
  assert(protocol.snapshot().title == "agent-loop");
  assert(protocol.snapshot().approvals == 2);
  assert(!protocol.stale(10999));
  assert(protocol.stale(11000));

  assert(parse_codex_action("interrupt") == CodexAction::interrupt);
  assert(parse_codex_action("approve") == CodexAction::approve);
  assert(parse_codex_action("shell") == CodexAction::none);
  assert(protocol.apply(R"({"type":"snapshot","sequence":9})", 2000) ==
         CompanionMessageResult::resync_required);
  return 0;
}
