#include "probe/hid_engine.hpp"

#include <array>

namespace {
constexpr std::array<size_t, 4> kHidItemDataSizeBytes{0, 1, 2, 4};
constexpr std::array<uint8_t, 32> kBase32Alphabet{
    'A', 'B', 'C', 'D', 'E', 'F',
    'G', 'H', 'I', 'J', 'K', 'L',
    'M', 'N', 'O', 'P', 'Q', 'R',
    'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', '2', '3', '4', '5',
    '6', '7',
};

constexpr auto kKeyboardReportMap = std::to_array<uint8_t>({
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,        //   Usage Minimum (0xE0)
    0x29, 0xE7,        //   Usage Maximum (0xE7)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,No Null Position)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x05,        //   Usage Maximum (Kana)
    0x91, 0x02,        //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x03,        //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,        //   Usage Minimum (0x00)
    0x29, 0x65,        //   Usage Maximum (0x65)
    0x81, 0x00,        //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              // End Collection
});

constexpr size_t kBase32OutputSize = 26;

bool contains_feature_report(std::span<const uint8_t> report_map) {
  constexpr uint8_t kLongItemPrefix = 0xFE;
  constexpr uint8_t kTypeMain = 0x0;
  constexpr uint8_t kTagFeature = 0x0B;

  for (size_t offset = 0; offset < report_map.size();) {
    const uint8_t prefix = report_map[offset++];

    if (prefix == kLongItemPrefix) {
      if (offset + 1 >= report_map.size()) {
        return false;
      }
      const uint8_t long_item_data_len = report_map[offset++];
      ++offset;
      if (offset + long_item_data_len > report_map.size()) {
        return false;
      }
      offset += long_item_data_len;
      continue;
    }

    const uint8_t data_len = kHidItemDataSizeBytes[prefix & 0x03];
    const uint8_t item_type = (prefix >> 2) & 0x03;
    const uint8_t item_tag = (prefix >> 4) & 0x0F;

    if (item_type == kTypeMain && item_tag == kTagFeature) {
      return true;
    }
    if (offset + data_len > report_map.size()) {
      return false;
    }
    offset += data_len;
  }
  return false;
}
}  // namespace

std::span<const uint8_t> keyboard_report_map() {
  return kKeyboardReportMap;
}

bool report_map_has_feature_report(std::span<const uint8_t> report_map) {
  return contains_feature_report(report_map);
}

HidResult HidEngine::make_report(uint8_t modifiers,
                                 std::span<const uint8_t> usages) const {
  if (usages.size() > HidReport{}.keys.size()) {
    return {HidError::too_many_keys, HidReport{}};
  }

  HidReport report;
  report.modifiers = modifiers;

  size_t write_index = 0;
  for (const auto usage : usages) {
    for (size_t i = 0; i < write_index; ++i) {
      if (report.keys[i] == usage) {
        return {HidError::duplicate_key, HidReport{}};
      }
    }
    report.keys[write_index++] = usage;
  }

  return {HidError::none, report};
}

bool HidEngine::report_map_has_feature_report() const {
  return ::report_map_has_feature_report(keyboard_report_map());
}

std::string hid_serial_from_device_id(std::span<const uint8_t, 16> device_id) {
  std::string encoded;
  encoded.reserve(kBase32OutputSize);

  uint32_t buffer = 0;
  int bits_in_buffer = 0;

  for (const uint8_t byte : device_id) {
    buffer = (buffer << 8) | byte;
    bits_in_buffer += 8;

    while (bits_in_buffer >= 5) {
      const auto index = static_cast<uint8_t>((buffer >> (bits_in_buffer - 5)) & 0x1F);
      encoded.push_back(static_cast<char>(kBase32Alphabet[index]));
      bits_in_buffer -= 5;
    }
  }

  if (bits_in_buffer > 0) {
    const auto index = static_cast<uint8_t>((buffer << (5 - bits_in_buffer)) & 0x1F);
    encoded.push_back(static_cast<char>(kBase32Alphabet[index]));
  }

  return encoded;
}
