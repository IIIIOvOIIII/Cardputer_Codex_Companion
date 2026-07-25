#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

inline constexpr uint64_t kPreviousPinGraceMs = 300000;

enum class PinAuthorization : uint8_t {
  denied,
  current,
  previous_companion_action,
};

class PinRotationState {
 public:
  explicit PinRotationState(std::string_view current_pin = {});

  void set_current(std::string_view pin, uint32_t revision);
  void restore(uint32_t revision);
  bool rotate(std::string_view old_pin, std::string_view new_pin,
              uint64_t now_ms);
  PinAuthorization authorize(std::string_view candidate,
                             bool is_companion_action,
                             uint64_t now_ms);
  [[nodiscard]] uint32_t revision() const { return revision_; }
  [[nodiscard]] std::optional<std::string_view> next_pairing() const;

 private:
  std::string current_;
  std::string previous_;
  uint32_t revision_ = 0;
  uint64_t previous_expires_ms_ = 0;
};

bool pin_value_is_valid(std::string_view pin);
bool constant_time_pin_equal(std::string_view lhs, std::string_view rhs);

