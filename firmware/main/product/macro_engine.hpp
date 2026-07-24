#pragma once

#include <cstdint>
#include <string_view>

#include "probe/hid_engine.hpp"
#include "product/profile.hpp"

class MacroSink {
 public:
  virtual ~MacroSink() = default;
  virtual void send_hid(const HidReport& report) = 0;
  virtual bool send_text(uint32_t operation_id, std::string_view text) = 0;
  virtual void device_action(DeviceAction action) = 0;
  virtual bool codex_action(CodexAction action) = 0;
};

enum class MacroResult : uint8_t {
  ok,
  invalid,
  unavailable,
};

class MacroEngine {
 public:
  explicit MacroEngine(MacroSink& sink) : sink_(sink) {}
  MacroResult execute(const KeyAction& action);

 private:
  MacroResult execute_leaf(ActionKind kind, uint8_t modifiers,
                           const std::array<uint8_t, 6>& usages,
                           uint8_t usage_count, std::string_view text,
                           DeviceAction device, CodexAction codex);
  void release_all();
  MacroSink& sink_;
  uint32_t next_operation_id_ = 1;
};
