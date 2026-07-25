#include "product/pin_rotation.hpp"

#include <algorithm>

bool pin_value_is_valid(std::string_view pin) {
  return pin.size() == 8 &&
         std::all_of(pin.begin(), pin.end(),
                     [](char value) { return value >= '0' && value <= '9'; });
}

bool constant_time_pin_equal(std::string_view lhs, std::string_view rhs) {
  uint8_t different = static_cast<uint8_t>(lhs.size() ^ rhs.size());
  for (std::size_t index = 0; index < 8; ++index) {
    const uint8_t left =
        index < lhs.size() ? static_cast<uint8_t>(lhs[index]) : 0;
    const uint8_t right =
        index < rhs.size() ? static_cast<uint8_t>(rhs[index]) : 0;
    different |= left ^ right;
  }
  return different == 0;
}

PinRotationState::PinRotationState(std::string_view current_pin) {
  if (pin_value_is_valid(current_pin)) current_ = current_pin;
}

void PinRotationState::set_current(
    std::string_view pin,
    uint32_t revision
) {
  if (!pin_value_is_valid(pin)) return;
  current_ = pin;
  previous_.clear();
  previous_expires_ms_ = 0;
  revision_ = revision;
}

void PinRotationState::restore(uint32_t revision) {
  revision_ = revision;
}

bool PinRotationState::rotate(
    std::string_view old_pin,
    std::string_view new_pin,
    uint64_t now_ms
) {
  if (!pin_value_is_valid(new_pin) ||
      !constant_time_pin_equal(old_pin, current_) ||
      constant_time_pin_equal(old_pin, new_pin)) {
    return false;
  }
  previous_ = current_;
  current_ = new_pin;
  previous_expires_ms_ = now_ms + kPreviousPinGraceMs;
  ++revision_;
  return true;
}

PinAuthorization PinRotationState::authorize(
    std::string_view candidate,
    bool is_companion_action,
    uint64_t now_ms
) {
  if (constant_time_pin_equal(candidate, current_)) {
    previous_.clear();
    previous_expires_ms_ = 0;
    return PinAuthorization::current;
  }
  if (is_companion_action && !previous_.empty() &&
      now_ms < previous_expires_ms_ &&
      constant_time_pin_equal(candidate, previous_)) {
    return PinAuthorization::previous_companion_action;
  }
  if (now_ms >= previous_expires_ms_) {
    previous_.clear();
    previous_expires_ms_ = 0;
  }
  return PinAuthorization::denied;
}

std::optional<std::string_view> PinRotationState::next_pairing() const {
  if (previous_.empty()) return std::nullopt;
  return current_;
}

