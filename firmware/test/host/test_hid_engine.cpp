#include <array>
#include <algorithm>
#include <cassert>

#include "probe/hid_engine.hpp"

int main() {
  HidEngine hid;

  const std::array<uint8_t, 2> keys{0x06, 0x4c};
  const auto chord = hid.make_report(0x08, keys);
  assert(chord.error == HidError::none);
  assert(chord.report.modifiers == 0x08);
  assert(chord.report.keys[0] == 0x06);
  assert(chord.report.keys[1] == 0x4c);
  assert(hid.release_all() == HidReport{});

  const std::array<uint8_t, 7> overflow{4, 5, 6, 7, 8, 9, 10};
  assert(hid.make_report(0, overflow).error == HidError::too_many_keys);
  assert(hid.make_report(0, overflow).report == HidReport{});

  const std::array<uint8_t, 6> duplicate{4, 4, 5, 6, 7, 8};
  const auto duplicate_result = hid.make_report(0, duplicate);
  assert(duplicate_result.error == HidError::duplicate_key);
  assert(duplicate_result.report == HidReport{});

  const std::array<uint8_t, 16> device_id{
      0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
      0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
  assert(hid_serial_from_device_id(device_id) == "CEIRCEIRCEIRCEIRCEIRCEIRCE");

  assert(!hid.report_map_has_feature_report());
  assert(!keyboard_report_map().empty());
  const std::array<uint8_t, 2> feature_item{0xB1, 0x01};
  assert(report_map_has_feature_report(feature_item));
  const auto report_map = keyboard_report_map();
  assert(std::find(report_map.begin(), report_map.end(), 0x7e) == report_map.end());

  return 0;
}
