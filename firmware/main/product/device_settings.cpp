#include "product/device_settings.hpp"

#include <algorithm>

namespace {
constexpr uint8_t kMagic0 = 'D';
constexpr uint8_t kMagic1 = 'S';

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

uint32_t get_u32(const uint8_t* value) {
  return static_cast<uint32_t>(value[0]) |
         (static_cast<uint32_t>(value[1]) << 8) |
         (static_cast<uint32_t>(value[2]) << 16) |
         (static_cast<uint32_t>(value[3]) << 24);
}

void put_u32(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
  output[2] = static_cast<uint8_t>(value >> 16);
  output[3] = static_cast<uint8_t>(value >> 24);
}

bool valid(const DeviceSettings& settings) {
  return settings.schema_version == 1 &&
         settings.brightness <= Brightness::percent_100 &&
         settings.return_to_pet <= ReturnToPet::seconds_60 &&
         settings.pet_frame_rate <= PetFrameRate::fps_3 &&
         device_g0_chord_is_valid(settings);
}
}  // namespace

uint8_t device_brightness(const DeviceSettings& settings) {
  constexpr std::array<uint8_t, 4> values{64, 128, 191, 255};
  return values[static_cast<std::size_t>(settings.brightness)];
}

uint32_t device_return_timeout_ms(const DeviceSettings& settings) {
  constexpr std::array<uint32_t, 4> values{0, 15000, 30000, 60000};
  return values[static_cast<std::size_t>(settings.return_to_pet)];
}

uint32_t device_pet_frame_interval_ms(const DeviceSettings& settings) {
  constexpr std::array<uint32_t, 3> values{500, 400, 333};
  return values[static_cast<std::size_t>(settings.pet_frame_rate)];
}

bool device_g0_chord_is_valid(const DeviceSettings& settings) {
  const bool usage_valid =
      settings.g0_chord_usage == 0 ||
      (settings.g0_chord_usage >= kG0ChordUsageMinimum &&
       settings.g0_chord_usage <= kG0ChordUsageMaximum);
  return settings.g0_chord_modifiers <= kG0ChordModifierMask &&
         usage_valid &&
         (!settings.g0_chord_enabled || settings.g0_chord_usage != 0);
}

DeviceSettingsRecord encode_device_settings(
    const DeviceSettings& settings
) {
  DeviceSettingsRecord record{};
  record[0] = kMagic0;
  record[1] = kMagic1;
  record[2] = settings.schema_version;
  record[3] = static_cast<uint8_t>(settings.brightness);
  record[4] = static_cast<uint8_t>(settings.return_to_pet);
  record[5] = static_cast<uint8_t>(settings.pet_frame_rate);
  record[6] = settings.g0_chord_usage;
  record[7] = static_cast<uint8_t>(
      settings.g0_chord_modifiers |
      (settings.g0_chord_enabled ? kG0ChordEnabledMask : 0));
  put_u32(record.data() + 8, crc32(std::span(record).first(8)));
  return record;
}

bool decode_device_settings(
    std::span<const uint8_t> record,
    DeviceSettings* output
) {
  if (output == nullptr || record.size() != kDeviceSettingsRecordBytes ||
      record[0] != kMagic0 || record[1] != kMagic1 ||
      get_u32(record.data() + 8) != crc32(record.first(8))) {
    return false;
  }
  constexpr uint8_t kReservedMask =
      static_cast<uint8_t>(~(kG0ChordEnabledMask |
                             kG0ChordModifierMask));
  if ((record[7] & kReservedMask) != 0) return false;
  DeviceSettings candidate{
      .schema_version = record[2],
      .brightness = static_cast<Brightness>(record[3]),
      .return_to_pet = static_cast<ReturnToPet>(record[4]),
      .pet_frame_rate = static_cast<PetFrameRate>(record[5]),
      .g0_chord_enabled =
          (record[7] & kG0ChordEnabledMask) != 0,
      .g0_chord_modifiers =
          static_cast<uint8_t>(record[7] & kG0ChordModifierMask),
      .g0_chord_usage = record[6],
  };
  if (!valid(candidate)) return false;
  *output = candidate;
  return true;
}

DeviceSettingsLoadResult DeviceSettingsStore::load() {
  DeviceSettingsRecord record{};
  DeviceSettings loaded;
  if (!backend_.load(record) || !decode_device_settings(record, &loaded)) {
    current_ = {};
    return DeviceSettingsLoadResult::defaults;
  }
  current_ = loaded;
  return DeviceSettingsLoadResult::loaded;
}

DeviceSettingsResult DeviceSettingsStore::apply(
    const DeviceSettings& settings
) {
  if (!valid(settings)) return DeviceSettingsResult::invalid;
  const DeviceSettingsRecord record = encode_device_settings(settings);
  if (!backend_.commit(record)) return DeviceSettingsResult::storage_error;
  current_ = settings;
  return DeviceSettingsResult::ok;
}
