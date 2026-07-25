#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "product/profile.hpp"
#include "product/pet_bundle.hpp"

inline constexpr uint64_t kCompanionStaleAfterMs = 30000;

enum class CodexLimitScope : uint8_t { codex, spark };
enum class CodexLimitWindow : uint8_t { five_hours, weekly };

struct CodexLimitUsage {
  CodexLimitScope scope = CodexLimitScope::codex;
  CodexLimitWindow window = CodexLimitWindow::five_hours;
  uint8_t used_percent = 0;

  bool operator==(const CodexLimitUsage&) const = default;
};

struct CompanionSnapshot {
  uint64_t sequence = 0;
  std::string session_id;
  std::string title;
  std::string cwd;
  std::string state;
  uint8_t approvals = 0;
  uint8_t inputs = 0;
  std::string pet_id;
  std::string pet_digest;
  PetState pet_state = PetState::idle;
  std::string model;
  std::string thinking_level;
  std::optional<bool> fast;
  std::array<CodexLimitUsage, 4> limits{};
  uint8_t limit_count = 0;
};

enum class CompanionMessageResult : uint8_t {
  snapshot,
  ignored,
  invalid,
  resync_required,
};

class CompanionProtocol {
 public:
  CompanionMessageResult apply(std::string_view json, uint64_t now_ms);
  void heartbeat(uint64_t now_ms);
  [[nodiscard]] const CompanionSnapshot& snapshot() const { return snapshot_; }
  [[nodiscard]] bool stale(uint64_t now_ms) const;

 private:
  CompanionSnapshot snapshot_{};
  uint64_t updated_at_ms_ = 0;
  bool has_snapshot_ = false;
};

CodexAction parse_codex_action(std::string_view value);
std::string_view codex_action_name(CodexAction action);
PetState effective_pet_state(bool companion_stale, PetState snapshot_state);
