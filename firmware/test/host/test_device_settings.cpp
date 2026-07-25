#include <array>
#include <cassert>
#include <span>

#include "product/device_settings.hpp"

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
  const DeviceSettings defaults;
  assert(defaults.brightness == Brightness::percent_75);
  assert(defaults.return_to_pet == ReturnToPet::seconds_30);
  assert(defaults.pet_frame_rate == PetFrameRate::fps_2_5);
  assert(device_brightness(defaults) == 191);
  assert(device_return_timeout_ms(defaults) == 30000);
  assert(device_pet_frame_interval_ms(defaults) == 400);

  DeviceSettings changed{
      .schema_version = 1,
      .brightness = Brightness::percent_25,
      .return_to_pet = ReturnToPet::seconds_60,
      .pet_frame_rate = PetFrameRate::fps_3,
  };
  const auto encoded = encode_device_settings(changed);
  DeviceSettings decoded;
  assert(decode_device_settings(encoded, &decoded));
  assert(decoded == changed);

  auto bad_schema = encoded;
  bad_schema[2] = 2;
  assert(!decode_device_settings(bad_schema, &decoded));
  auto bad_enum = encoded;
  bad_enum[3] = 9;
  assert(!decode_device_settings(bad_enum, &decoded));
  auto bad_crc = encoded;
  bad_crc.back() ^= 1;
  assert(!decode_device_settings(bad_crc, &decoded));

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
  return 0;
}
