#include "product/pet_bundle.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#ifdef ESP_PLATFORM
#include "mbedtls/sha256.h"
#else
#include <openssl/evp.h>
#endif

namespace {
constexpr std::size_t kHeaderLength = 132;
constexpr std::size_t kStateTableLength = 5 * 8;
constexpr std::size_t kFrameTableLength = 40 * 16;
constexpr std::size_t kDecodedFrameBytes = kPetFramePixels * 2;

uint16_t read16(std::span<const uint8_t> bytes, std::size_t offset) {
  return static_cast<uint16_t>(bytes[offset]) |
         static_cast<uint16_t>(bytes[offset + 1] << 8);
}

uint32_t read32(std::span<const uint8_t> bytes, std::size_t offset) {
  uint32_t value = 0;
  for (int byte = 0; byte < 4; ++byte) {
    value |= static_cast<uint32_t>(bytes[offset + byte]) << (byte * 8);
  }
  return value;
}

bool range_valid(std::size_t offset, std::size_t length,
                 std::size_t total) {
  return offset <= total && length <= total - offset;
}

bool valid_utf8(std::span<const uint8_t> bytes) {
  std::size_t index = 0;
  while (index < bytes.size()) {
    const uint8_t first = bytes[index++];
    if (first < 0x80) {
      if (first == 0) return false;
      continue;
    }
    int continuation = 0;
    uint32_t codepoint = 0;
    if ((first & 0xe0) == 0xc0) {
      continuation = 1;
      codepoint = first & 0x1f;
    } else if ((first & 0xf0) == 0xe0) {
      continuation = 2;
      codepoint = first & 0x0f;
    } else if ((first & 0xf8) == 0xf0) {
      continuation = 3;
      codepoint = first & 0x07;
    } else {
      return false;
    }
    if (index + continuation > bytes.size()) return false;
    for (int count = 0; count < continuation; ++count) {
      const uint8_t next = bytes[index++];
      if ((next & 0xc0) != 0x80) return false;
      codepoint = (codepoint << 6) | (next & 0x3f);
    }
    if ((continuation == 1 && codepoint < 0x80) ||
        (continuation == 2 && codepoint < 0x800) ||
        (continuation == 3 && codepoint < 0x10000) ||
        codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
      return false;
    }
  }
  return true;
}

class Sha256 {
 public:
  Sha256() {
#ifdef ESP_PLATFORM
    mbedtls_sha256_init(&context_);
    mbedtls_sha256_starts(&context_, 0);
#else
    context_ = EVP_MD_CTX_new();
    if (context_ != nullptr) EVP_DigestInit_ex(context_, EVP_sha256(), nullptr);
#endif
  }
  ~Sha256() {
#ifdef ESP_PLATFORM
    mbedtls_sha256_free(&context_);
#else
    EVP_MD_CTX_free(context_);
#endif
  }
  void update(std::span<const uint8_t> bytes) {
#ifdef ESP_PLATFORM
    mbedtls_sha256_update(&context_, bytes.data(), bytes.size());
#else
    EVP_DigestUpdate(context_, bytes.data(), bytes.size());
#endif
  }
  std::array<uint8_t, 32> finish() {
    std::array<uint8_t, 32> result{};
#ifdef ESP_PLATFORM
    mbedtls_sha256_finish(&context_, result.data());
#else
    unsigned int length = 0;
    EVP_DigestFinal_ex(context_, result.data(), &length);
#endif
    return result;
  }

 private:
#ifdef ESP_PLATFORM
  mbedtls_sha256_context context_{};
#else
  EVP_MD_CTX* context_ = nullptr;
#endif
};

std::optional<std::array<uint8_t, 32>> source_digest(
    const PetByteSource& source, bool zero_content_digest) {
  Sha256 hash;
  std::array<uint8_t, 4096> buffer{};
  std::size_t cursor = 0;
  while (cursor < source.size()) {
    const std::size_t count =
        std::min(buffer.size(), source.size() - cursor);
    std::span<uint8_t> chunk(buffer.data(), count);
    if (!source.read(cursor, chunk)) return std::nullopt;
    if (zero_content_digest) {
      const std::size_t start = std::max<std::size_t>(cursor, 24);
      const std::size_t end =
          std::min<std::size_t>(cursor + count, 56);
      if (start < end) {
        std::fill(buffer.begin() + (start - cursor),
                  buffer.begin() + (end - cursor), 0);
      }
    }
    hash.update(chunk);
    cursor += count;
  }
  return hash.finish();
}
}  // namespace

PetBundleError validate_pet_bundle(
    const PetByteSource& source,
    const std::optional<std::array<uint8_t, 32>>& expected_upload_digest,
    PetBundleMetadata* output) {
  if (source.size() < kHeaderLength) return PetBundleError::truncated;
  if (source.size() > kPetBundleMaximumBytes) return PetBundleError::bounds;
  std::array<uint8_t, kHeaderLength> header{};
  if (!source.read(0, header)) return PetBundleError::truncated;
  if (std::memcmp(header.data(), "CCPT", 4) != 0) {
    return PetBundleError::magic;
  }
  if (read16(header, 4) != 1 || read16(header, 6) != kHeaderLength) {
    return PetBundleError::version;
  }
  if (read32(header, 8) != source.size()) return PetBundleError::bounds;
  const uint8_t pet_id_length = header[12];
  if (pet_id_length == 0 || pet_id_length > 64 ||
      !valid_utf8(std::span<const uint8_t>(header.data() + 68,
                                           pet_id_length))) {
    return PetBundleError::utf8;
  }
  if (read16(header, 16) != kPetFrameWidth ||
      read16(header, 18) != kPetFrameHeight ||
      read16(header, 20) != 400 ||
      header[22] != 5 || header[23] != 8) {
    return PetBundleError::dimensions;
  }
  const uint32_t state_offset = read32(header, 56);
  const uint32_t frame_offset = read32(header, 60);
  const uint32_t payload_offset = read32(header, 64);
  if (!range_valid(state_offset, kStateTableLength, source.size()) ||
      !range_valid(frame_offset, kFrameTableLength, source.size()) ||
      payload_offset > source.size() ||
      state_offset < kHeaderLength ||
      frame_offset < state_offset + kStateTableLength ||
      payload_offset < frame_offset + kFrameTableLength) {
    return PetBundleError::table;
  }

  const auto content = source_digest(source, true);
  if (!content.has_value() ||
      !std::equal(content->begin(), content->end(), header.begin() + 24)) {
    return PetBundleError::digest;
  }
  if (expected_upload_digest.has_value()) {
    const auto upload = source_digest(source, false);
    if (!upload.has_value() || *upload != *expected_upload_digest) {
      return PetBundleError::digest;
    }
  }

  std::array<uint8_t, kStateTableLength> states{};
  std::array<uint8_t, kFrameTableLength> frames{};
  if (!source.read(state_offset, states) ||
      !source.read(frame_offset, frames)) {
    return PetBundleError::truncated;
  }
  PetBundleMetadata metadata;
  metadata.pet_id.assign(
      reinterpret_cast<const char*>(header.data() + 68), pet_id_length);
  std::copy(header.begin() + 24, header.begin() + 56,
            metadata.content_digest.begin());
  metadata.schema_version = 1;
  metadata.width = kPetFrameWidth;
  metadata.height = kPetFrameHeight;
  metadata.interval_ms = 400;

  std::array<bool, 5> seen{};
  for (std::size_t entry = 0; entry < 5; ++entry) {
    const std::size_t offset = entry * 8;
    const uint8_t state = states[offset];
    const uint16_t count = read16(states, offset + 2);
    const uint32_t first = read32(states, offset + 4);
    if (state >= 5 || seen[state] || count != 8 || first > 32) {
      return PetBundleError::table;
    }
    seen[state] = true;
    for (std::size_t frame = 0; frame < 8; ++frame) {
      const std::size_t index = first + frame;
      if (index >= 40) return PetBundleError::table;
      const std::size_t record_offset = index * 16;
      const uint8_t encoding = frames[record_offset];
      if (encoding > 1) return PetBundleError::encoding;
      PetFrameRecord record{
          .encoding = static_cast<PetFrameEncoding>(encoding),
          .payload_offset = read32(frames, record_offset + 4),
          .stored_length = read32(frames, record_offset + 8),
          .decoded_length = read32(frames, record_offset + 12),
      };
      if (record.decoded_length != kDecodedFrameBytes) {
        return PetBundleError::decoded_length;
      }
      if (record.stored_length == 0 ||
          record.stored_length > kDecodedFrameBytes ||
          record.payload_offset < payload_offset ||
          !range_valid(record.payload_offset, record.stored_length,
                       source.size())) {
        return PetBundleError::bounds;
      }
      metadata.frames[state][frame] = record;
    }
  }
  if (output != nullptr) *output = std::move(metadata);
  return PetBundleError::none;
}

PetBundleError decode_pet_frame(
    const PetByteSource& source,
    const PetBundleMetadata& metadata,
    PetState state,
    uint8_t frame,
    std::span<uint16_t, kPetFramePixels> output) {
  const std::size_t state_index = static_cast<std::size_t>(state);
  if (state_index >= metadata.frames.size() || frame >= 8) {
    return PetBundleError::bounds;
  }
  const PetFrameRecord record = metadata.frames[state_index][frame];
  std::vector<uint8_t> encoded(record.stored_length);
  if (!source.read(record.payload_offset, encoded)) {
    return PetBundleError::truncated;
  }
  if (record.encoding == PetFrameEncoding::raw_rgb565) {
    if (encoded.size() != kDecodedFrameBytes) {
      return PetBundleError::decoded_length;
    }
    for (std::size_t index = 0; index < output.size(); ++index) {
      output[index] = read16(encoded, index * 2);
    }
    return PetBundleError::none;
  }
  if (record.encoding != PetFrameEncoding::rle_rgb565) {
    return PetBundleError::encoding;
  }
  std::size_t cursor = 0;
  std::size_t output_cursor = 0;
  for (std::size_t row = 0; row < kPetFrameHeight; ++row) {
    if (!range_valid(cursor, 2, encoded.size())) return PetBundleError::rle;
    const uint16_t run_count = read16(encoded, cursor);
    cursor += 2;
    std::size_t row_pixels = 0;
    for (uint16_t run = 0; run < run_count; ++run) {
      if (!range_valid(cursor, 4, encoded.size())) return PetBundleError::rle;
      const uint16_t count = read16(encoded, cursor);
      const uint16_t pixel = read16(encoded, cursor + 2);
      cursor += 4;
      if (count == 0 || count > kPetFrameWidth - row_pixels) {
        return PetBundleError::rle;
      }
      std::fill_n(output.begin() + output_cursor, count, pixel);
      output_cursor += count;
      row_pixels += count;
    }
    if (row_pixels != kPetFrameWidth) return PetBundleError::rle;
  }
  if (cursor != encoded.size() || output_cursor != output.size()) {
    return PetBundleError::rle;
  }
  return PetBundleError::none;
}
