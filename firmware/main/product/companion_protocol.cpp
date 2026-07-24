#include "product/companion_protocol.hpp"

#include <algorithm>
#include <charconv>
#include <optional>

namespace {
std::optional<std::string_view> string_field(std::string_view json,
                                             std::string_view name) {
  const std::string needle = "\"" + std::string(name) + "\":\"";
  const std::size_t start = json.find(needle);
  if (start == std::string_view::npos) return std::nullopt;
  const std::size_t value_start = start + needle.size();
  const std::size_t end = json.find('"', value_start);
  if (end == std::string_view::npos) return std::nullopt;
  return json.substr(value_start, end - value_start);
}

std::optional<uint64_t> integer_field(std::string_view json,
                                      std::string_view name) {
  const std::string needle = "\"" + std::string(name) + "\":";
  const std::size_t start = json.find(needle);
  if (start == std::string_view::npos) return std::nullopt;
  const char* begin = json.data() + start + needle.size();
  const char* end = json.data() + json.size();
  uint64_t value = 0;
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{}) return std::nullopt;
  return value;
}

std::string clipped(std::optional<std::string_view> value,
                    std::size_t maximum) {
  return value.has_value()
             ? std::string(value->substr(0, std::min(value->size(), maximum)))
             : std::string();
}
}  // namespace

CompanionMessageResult CompanionProtocol::apply(std::string_view json,
                                                uint64_t now_ms) {
  if (string_field(json, "type") != std::optional<std::string_view>{"snapshot"}) {
    return CompanionMessageResult::ignored;
  }
  const auto sequence = integer_field(json, "sequence");
  if (!sequence.has_value()) return CompanionMessageResult::invalid;
  if (has_snapshot_ && *sequence != snapshot_.sequence + 1 &&
      !stale(now_ms)) {
    return CompanionMessageResult::resync_required;
  }
  snapshot_.sequence = *sequence;
  snapshot_.session_id = clipped(string_field(json, "session_id"), 64);
  snapshot_.title = clipped(string_field(json, "title"), 64);
  snapshot_.cwd = clipped(string_field(json, "cwd"), 96);
  snapshot_.state = clipped(string_field(json, "state"), 24);
  snapshot_.approvals = static_cast<uint8_t>(
      std::min<uint64_t>(integer_field(json, "approvals").value_or(0), 255));
  snapshot_.inputs = static_cast<uint8_t>(
      std::min<uint64_t>(integer_field(json, "inputs").value_or(0), 255));
  has_snapshot_ = true;
  updated_at_ms_ = now_ms;
  return CompanionMessageResult::snapshot;
}

bool CompanionProtocol::stale(uint64_t now_ms) const {
  return !has_snapshot_ || now_ms - updated_at_ms_ >= kCompanionStaleAfterMs;
}

CodexAction parse_codex_action(std::string_view value) {
  if (value == "select_next") return CodexAction::select_next_session;
  if (value == "select_previous") return CodexAction::select_previous_session;
  if (value == "new") return CodexAction::new_session;
  if (value == "interrupt") return CodexAction::interrupt;
  if (value == "approve") return CodexAction::approve;
  if (value == "reject") return CodexAction::reject;
  if (value == "provide_input") return CodexAction::provide_input;
  return CodexAction::none;
}

std::string_view codex_action_name(CodexAction action) {
  switch (action) {
    case CodexAction::select_next_session: return "select_next";
    case CodexAction::select_previous_session: return "select_previous";
    case CodexAction::new_session: return "new";
    case CodexAction::interrupt: return "interrupt";
    case CodexAction::approve: return "approve";
    case CodexAction::reject: return "reject";
    case CodexAction::provide_input: return "provide_input";
    case CodexAction::none: return "none";
  }
  return "none";
}
