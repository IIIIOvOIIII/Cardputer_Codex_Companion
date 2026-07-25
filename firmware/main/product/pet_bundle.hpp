#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

inline constexpr std::size_t kPetFrameWidth = 96;
inline constexpr std::size_t kPetFrameHeight = 104;
inline constexpr std::size_t kPetFramePixels =
    kPetFrameWidth * kPetFrameHeight;
inline constexpr std::size_t kPetBundleMaximumBytes = 820 * 1024;

enum class PetState : uint8_t {
  idle = 0,
  working = 1,
  waiting = 2,
  review = 3,
  failed = 4,
};

enum class PetFrameEncoding : uint8_t {
  raw_rgb565 = 0,
  rle_rgb565 = 1,
};

struct PetFrameRecord {
  PetFrameEncoding encoding = PetFrameEncoding::raw_rgb565;
  uint32_t payload_offset = 0;
  uint32_t stored_length = 0;
  uint32_t decoded_length = 0;
};

struct PetBundleMetadata {
  std::string pet_id;
  std::array<uint8_t, 32> content_digest{};
  uint16_t schema_version = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t interval_ms = 0;
  std::array<std::array<PetFrameRecord, 8>, 5> frames{};
};

enum class PetBundleError : uint8_t {
  none,
  truncated,
  magic,
  version,
  bounds,
  overflow,
  dimensions,
  utf8,
  digest,
  table,
  encoding,
  decoded_length,
  rle,
};

class PetByteSource {
 public:
  virtual ~PetByteSource() = default;
  [[nodiscard]] virtual std::size_t size() const = 0;
  virtual bool read(std::size_t offset,
                    std::span<uint8_t> output) const = 0;
};

PetBundleError validate_pet_bundle(
    const PetByteSource& source,
    const std::optional<std::array<uint8_t, 32>>& expected_upload_digest,
    PetBundleMetadata* output);

PetBundleError decode_pet_frame(
    const PetByteSource& source,
    const PetBundleMetadata& metadata,
    PetState state,
    uint8_t frame,
    std::span<uint16_t, kPetFramePixels> output);
