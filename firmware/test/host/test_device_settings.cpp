#include <array>
#include <cassert>
#include <span>

#include "product/device_settings.hpp"

namespace {
uint32_t crc32(std::span<const uint8_t> bytes) {
  uint32_t crc = 0xffffffffu;
  for (const uint8_t byte : bytes) {
    crc ^= byte;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1) ^ (0xedb88320u & mask);
    }
  }
  return crc ^ 0xffffffffu;
}

void refresh_crc(DeviceSettingsRecord* record) {
  const uint32_t value = crc32(std::span(*record).first(8));
  (*record)[8] = static_cast<uint8_t>(value);
  (*record)[9] = static_cast<uint8_t>(value >> 8);
  (*record)[10] = static_cast<uint8_t>(value >> 16);
  (*record)[11] = static_cast<uint8_t>(value >> 24);
}
}  // namespace

struct MemorySettingsBackend final : DeviceSettingsBackend {
  std::array<uint8_t, kDeviceSettingsRecordBytes> bytes{};
  bool has_value = false;
  bool fail_commit = false;

  bool load(std::span<uint8_t> output) override {
    if (!has_value || output.size() != bytes.size()) return false;
    std::copy(bytes.begin(), bytes.end(), output.begin());
    return true;
  }

  bool commit(std::span<const uint8_t> input) override {
    if (fail_commit || input.size() != bytes.size()) return false;
    std::copy(input.begin(), input.end(), bytes.begin());
    has_value = true;
    return true;
  }
};

int main() {
  static_assert(kDeviceSettingsStorageKey.size() <= 15);
  assert(kDeviceSettingsStorageKey == "display_cfg");

  const DeviceSettings defaults;
  assert(defaults.brightness == Brightness::percent_75);
  assert(defaults.return_to_pet == ReturnToPet::seconds_30);
  assert(defaults.pet_frame_rate == PetFrameRate::fps_2_5);
  assert(!defaults.g0_chord_enabled);
  assert(defaults.g0_chord_modifiers == 0);
  assert(defaults.g0_chord_usage == 0);
  assert(device_brightness(defaults) == 191);
  assert(device_return_timeout_ms(defaults) == 30000);
  assert(device_pet_frame_interval_ms(defaults) == 400);

  DeviceSettings changed{
      .schema_version = 1,
      .brightness = Brightness::percent_25,
      .return_to_pet = ReturnToPet::seconds_60,
      .pet_frame_rate = PetFrameRate::fps_3,
      .g0_chord_enabled = true,
      .g0_chord_modifiers = 0x05,
      .g0_chord_usage = 0x19,
  };
  const auto encoded = encode_device_settings(changed);
  assert(encoded[6] == 0x19);
  assert(encoded[7] == 0x85);
  DeviceSettings decoded;
  assert(decode_device_settings(encoded, &decoded));
  assert(decoded == changed);

  DeviceSettings retained = changed;
  retained.g0_chord_enabled = false;
  const auto retained_encoded = encode_device_settings(retained);
  assert(retained_encoded[6] == 0x19);
  assert(retained_encoded[7] == 0x05);
  assert(decode_device_settings(retained_encoded, &decoded));
  assert(decoded == retained);

  DeviceSettings legacy = changed;
  legacy.g0_chord_enabled = false;
  legacy.g0_chord_modifiers = 0;
  legacy.g0_chord_usage = 0;
  const auto legacy_encoded = encode_device_settings(legacy);
  assert(legacy_encoded[6] == 0);
  assert(legacy_encoded[7] == 0);
  assert(decode_device_settings(legacy_encoded, &decoded));
  assert(!decoded.g0_chord_enabled);

  auto bad_schema = encoded;
  bad_schema[2] = 2;
  assert(!decode_device_settings(bad_schema, &decoded));
  auto bad_enum = encoded;
  bad_enum[3] = 9;
  assert(!decode_device_settings(bad_enum, &decoded));
  auto bad_crc = encoded;
  bad_crc.back() ^= 1;
  assert(!decode_device_settings(bad_crc, &decoded));
  auto reserved_bits = encoded;
  reserved_bits[7] |= 0x10;
  refresh_crc(&reserved_bits);
  assert(!decode_device_settings(reserved_bits, &decoded));

  MemorySettingsBackend backend;
  DeviceSettingsStore store(backend);
  assert(store.load() == DeviceSettingsLoadResult::defaults);
  assert(store.apply(changed) == DeviceSettingsResult::ok);
  assert(store.current() == changed);
  backend.fail_commit = true;
  DeviceSettings rejected = changed;
  rejected.brightness = Brightness::percent_100;
  assert(store.apply(rejected) == DeviceSettingsResult::storage_error);
  assert(store.current() == changed);

  backend.fail_commit = false;
  DeviceSettings invalid = changed;
  invalid.g0_chord_modifiers = 0x10;
  assert(store.apply(invalid) == DeviceSettingsResult::invalid);
  invalid = changed;
  invalid.g0_chord_usage = 0x03;
  assert(store.apply(invalid) == DeviceSettingsResult::invalid);
  invalid.g0_chord_usage = 0x66;
  assert(store.apply(invalid) == DeviceSettingsResult::invalid);
  invalid = changed;
  invalid.g0_chord_enabled = false;
  invalid.g0_chord_usage = 0;
  assert(store.apply(invalid) == DeviceSettingsResult::ok);
  return 0;
}
