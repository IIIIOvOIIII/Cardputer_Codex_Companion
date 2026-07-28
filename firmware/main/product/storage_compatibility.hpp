#pragma once

#include <cstdint>
#include <string_view>

inline constexpr uint32_t kRequiredStorageBytes = 0x1e0000;

enum class StorageCompatibilityState : uint8_t {
  ready,
  missing,
  wrong_type,
  too_small,
};

struct StorageCompatibility {
  StorageCompatibilityState state = StorageCompatibilityState::missing;
  uint32_t size_bytes = 0;

  [[nodiscard]] constexpr bool ready() const {
    return state == StorageCompatibilityState::ready;
  }
};

constexpr StorageCompatibility evaluate_storage_compatibility(
    bool found, bool data_type, bool spiffs_subtype, uint32_t size_bytes) {
  if (!found) {
    return {
        .state = StorageCompatibilityState::missing,
        .size_bytes = 0,
    };
  }
  if (!data_type || !spiffs_subtype) {
    return {
        .state = StorageCompatibilityState::wrong_type,
        .size_bytes = size_bytes,
    };
  }
  if (size_bytes < kRequiredStorageBytes) {
    return {
        .state = StorageCompatibilityState::too_small,
        .size_bytes = size_bytes,
    };
  }
  return {
      .state = StorageCompatibilityState::ready,
      .size_bytes = size_bytes,
  };
}

constexpr std::string_view storage_compatibility_name(
    StorageCompatibilityState state) {
  switch (state) {
    case StorageCompatibilityState::ready:
      return "ready";
    case StorageCompatibilityState::missing:
      return "missing";
    case StorageCompatibilityState::wrong_type:
      return "wrong_type";
    case StorageCompatibilityState::too_small:
      return "too_small";
  }
  return "missing";
}

#ifdef ESP_PLATFORM
StorageCompatibility inspect_storage_compatibility();
#endif
