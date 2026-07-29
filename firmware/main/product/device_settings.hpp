#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

enum class Brightness : uint8_t {
  percent_25,
  percent_50,
  percent_75,
  percent_100,
};

enum class ReturnToPet : uint8_t {
  disabled,
  seconds_15,
  seconds_30,
  seconds_60,
};

enum class PetFrameRate : uint8_t {
  fps_2,
  fps_2_5,
  fps_3,
};

struct DeviceSettings {
  uint8_t schema_version = 1;
  Brightness brightness = Brightness::percent_75;
  ReturnToPet return_to_pet = ReturnToPet::seconds_30;
  PetFrameRate pet_frame_rate = PetFrameRate::fps_2_5;
  bool g0_chord_enabled = false;
  uint8_t g0_chord_modifiers = 0;
  uint8_t g0_chord_usage = 0;

  bool operator==(const DeviceSettings&) const = default;
};

inline constexpr uint8_t kG0ChordEnabledMask = 0x80;
inline constexpr uint8_t kG0ChordModifierMask = 0x0f;
inline constexpr uint8_t kG0ChordUsageMinimum = 0x04;
inline constexpr uint8_t kG0ChordUsageMaximum = 0x65;
inline constexpr std::size_t kDeviceSettingsRecordBytes = 12;
inline constexpr std::string_view kDeviceSettingsStorageKey = "display_cfg";
static_assert(kDeviceSettingsStorageKey.size() <= 15);
using DeviceSettingsRecord =
    std::array<uint8_t, kDeviceSettingsRecordBytes>;

uint8_t device_brightness(const DeviceSettings& settings);
uint32_t device_return_timeout_ms(const DeviceSettings& settings);
uint32_t device_pet_frame_interval_ms(const DeviceSettings& settings);
bool device_g0_chord_is_valid(const DeviceSettings& settings);
DeviceSettingsRecord encode_device_settings(const DeviceSettings& settings);
bool decode_device_settings(
    std::span<const uint8_t> record,
    DeviceSettings* output
);

class DeviceSettingsBackend {
 public:
  virtual ~DeviceSettingsBackend() = default;
  virtual bool load(std::span<uint8_t> output) = 0;
  virtual bool commit(std::span<const uint8_t> input) = 0;
};

enum class DeviceSettingsLoadResult : uint8_t { loaded, defaults };
enum class DeviceSettingsResult : uint8_t { ok, invalid, storage_error };

class DeviceSettingsStore {
 public:
  explicit DeviceSettingsStore(DeviceSettingsBackend& backend)
      : backend_(backend) {}

  DeviceSettingsLoadResult load();
  DeviceSettingsResult apply(const DeviceSettings& settings);
  [[nodiscard]] const DeviceSettings& current() const { return current_; }

 private:
  DeviceSettingsBackend& backend_;
  DeviceSettings current_{};
};
