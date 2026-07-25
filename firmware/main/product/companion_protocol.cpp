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

std::optional<bool> bool_field(std::string_view json,
                               std::string_view name) {
  const std::string needle = "\"" + std::string(name) + "\":";
  const std::size_t start = json.find(needle);
  if (start == std::string_view::npos) return std::nullopt;
  const std::string_view tail = json.substr(start + needle.size());
  if (tail.starts_with("true")) return true;
  if (tail.starts_with("false")) return false;
  return std::nullopt;
}

std::string clipped(std::optional<std::string_view> value,
                    std::size_t maximum) {
  return value.has_value()
             ? std::string(value->substr(0, std::min(value->size(), maximum)))
             : std::string();
}

bool valid_digest(std::string_view value) {
  if (value.size() != 64) return false;
  return std::all_of(value.begin(), value.end(), [](char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f');
  });
}

PetState parse_pet_state(std::string_view value) {
  if (value == "working") return PetState::working;
  if (value == "waiting") return PetState::waiting;
  if (value == "review") return PetState::review;
  if (value == "failed") return PetState::failed;
  return PetState::idle;
}

std::optional<CodexLimitUsage> parse_limit(std::string_view object) {
  const auto scope_value = string_field(object, "scope");
  const auto window_value = string_field(object, "window");
  const auto percent = integer_field(object, "used_percent");
  if (!scope_value || !window_value || !percent || *percent > 100) {
    return std::nullopt;
  }
  CodexLimitUsage usage;
  if (*scope_value == "codex") {
    usage.scope = CodexLimitScope::codex;
  } else if (*scope_value == "spark") {
    usage.scope = CodexLimitScope::spark;
  } else {
    return std::nullopt;
  }
  if (*window_value == "5h") {
    usage.window = CodexLimitWindow::five_hours;
  } else if (*window_value == "weekly") {
    usage.window = CodexLimitWindow::weekly;
  } else {
    return std::nullopt;
  }
  usage.used_percent = static_cast<uint8_t>(*percent);
  return usage;
}

uint8_t parse_limits(std::string_view json,
                     std::array<CodexLimitUsage, 4>& output) {
  constexpr std::string_view needle = "\"limits\":[";
  const std::size_t found = json.find(needle);
  if (found == std::string_view::npos) return 0;
  std::size_t cursor = found + needle.size();
  uint8_t inspected = 0;
  uint8_t accepted = 0;
  while (cursor < json.size() && inspected < output.size()) {
    const std::size_t start = json.find('{', cursor);
    const std::size_t array_end = json.find(']', cursor);
    if (start == std::string_view::npos ||
        (array_end != std::string_view::npos && start > array_end)) {
      break;
    }
    bool quoted = false;
    bool escaped = false;
    unsigned depth = 0;
    std::size_t end = start;
    for (; end < json.size(); ++end) {
      const char character = json[end];
      if (quoted) {
        if (escaped) {
          escaped = false;
        } else if (character == '\\') {
          escaped = true;
        } else if (character == '"') {
          quoted = false;
        }
        continue;
      }
      if (character == '"') {
        quoted = true;
      } else if (character == '{') {
        ++depth;
      } else if (character == '}' && --depth == 0) {
        break;
      }
    }
    if (end >= json.size()) break;
    ++inspected;
    if (const auto usage = parse_limit(
            json.substr(start, end - start + 1))) {
      output[accepted++] = *usage;
    }
    cursor = end + 1;
  }
  return accepted;
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
  snapshot_.pet_id = clipped(string_field(json, "pet_id"), 64);
  const std::string digest = clipped(string_field(json, "pet_digest"), 64);
  snapshot_.pet_digest = valid_digest(digest) ? digest : "";
  snapshot_.pet_state = parse_pet_state(
      string_field(json, "pet_state").value_or("idle"));
  snapshot_.model = clipped(string_field(json, "model"), 32);
  snapshot_.thinking_level =
      clipped(string_field(json, "thinking_level"), 16);
  snapshot_.fast = bool_field(json, "fast");
  snapshot_.limits = {};
  snapshot_.limit_count = parse_limits(json, snapshot_.limits);
  has_snapshot_ = true;
  updated_at_ms_ = now_ms;
  return CompanionMessageResult::snapshot;
}

PetState effective_pet_state(bool companion_stale,
                             PetState snapshot_state) {
  return companion_stale ? PetState::waiting : snapshot_state;
}

void CompanionProtocol::heartbeat(uint64_t now_ms) {
  if (has_snapshot_) updated_at_ms_ = now_ms;
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
