#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

struct HidReport {
  uint8_t modifiers = 0;
  uint8_t reserved = 0;
  std::array<uint8_t, 6> keys{};
  bool operator==(const HidReport&) const = default;
};

enum class HidError { none, too_many_keys, duplicate_key };

struct HidResult {
  HidError error;
  HidReport report;
};

std::span<const uint8_t> keyboard_report_map();
bool report_map_has_feature_report(std::span<const uint8_t> report_map);
uint8_t keyboard_report_map_key_array_slots();
bool keyboard_report_map_uses_bruce_reserved_items();

class HidEngine {
 public:
  HidResult make_report(uint8_t modifiers, std::span<const uint8_t> usages) const;
  HidReport release_all() const { return {}; }
  bool report_map_has_feature_report() const;
};

std::string hid_serial_from_device_id(std::span<const uint8_t, 16> device_id);
