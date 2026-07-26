#include <openssl/sha.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "product/pet_bundle.hpp"

namespace {
void put16(std::vector<uint8_t>& data, std::size_t offset, uint16_t value) {
  data[offset] = static_cast<uint8_t>(value);
  data[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void put32(std::vector<uint8_t>& data, std::size_t offset, uint32_t value) {
  for (int byte = 0; byte < 4; ++byte) {
    data[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
  }
}

std::array<uint8_t, 32> digest(std::span<const uint8_t> data) {
  std::array<uint8_t, 32> result{};
  SHA256(data.data(), data.size(), result.data());
  return result;
}

std::vector<uint8_t> valid_bundle(bool rle) {
  constexpr std::size_t payload_offset = 812;
  const std::size_t frame_size = rle ? 104 * 6 : 96 * 104 * 2;
  std::vector<uint8_t> data(payload_offset + 40 * frame_size, 0);
  std::memcpy(data.data(), "CCPT", 4);
  put16(data, 4, 1);
  put16(data, 6, 132);
  put32(data, 8, static_cast<uint32_t>(data.size()));
  data[12] = 5;
  put16(data, 16, 96);
  put16(data, 18, 104);
  put16(data, 20, 400);
  data[22] = 5;
  data[23] = 8;
  put32(data, 56, 132);
  put32(data, 60, 172);
  put32(data, 64, payload_offset);
  std::memcpy(data.data() + 68, "rocky", 5);

  std::size_t cursor = payload_offset;
  for (uint8_t state = 0; state < 5; ++state) {
    const std::size_t state_offset = 132 + state * 8;
    data[state_offset] = state;
    put16(data, state_offset + 2, 8);
    put32(data, state_offset + 4, state * 8);
    for (uint8_t frame = 0; frame < 8; ++frame) {
      const std::size_t index = state * 8 + frame;
      const std::size_t record = 172 + index * 16;
      data[record] = rle ? 1 : 0;
      put32(data, record + 4, static_cast<uint32_t>(cursor));
      put32(data, record + 8, static_cast<uint32_t>(frame_size));
      put32(data, record + 12, 96 * 104 * 2);
      const uint16_t pixel = static_cast<uint16_t>(0x1000 + index);
      if (rle) {
        for (int row = 0; row < 104; ++row) {
          put16(data, cursor, 1);
          put16(data, cursor + 2, 96);
          put16(data, cursor + 4, pixel);
          cursor += 6;
        }
      } else {
        for (int pixel_index = 0; pixel_index < 96 * 104; ++pixel_index) {
          put16(data, cursor, pixel);
          cursor += 2;
        }
      }
    }
  }
  std::vector<uint8_t> zeroed = data;
  std::fill(zeroed.begin() + 24, zeroed.begin() + 56, 0);
  const auto content = digest(zeroed);
  std::copy(content.begin(), content.end(), data.begin() + 24);
  return data;
}

class MemorySource final : public PetByteSource {
 public:
  explicit MemorySource(const std::vector<uint8_t>& data) : data_(data) {}
  std::size_t size() const override { return data_.size(); }
  bool read(std::size_t offset, std::span<uint8_t> output) const override {
    max_read_size_ = std::max(max_read_size_, output.size());
    if (offset > data_.size() || output.size() > data_.size() - offset) {
      return false;
    }
    std::copy_n(data_.begin() + offset, output.size(), output.begin());
    return true;
  }
  std::size_t max_read_size() const { return max_read_size_; }
  void reset_max_read_size() const { max_read_size_ = 0; }

 private:
  const std::vector<uint8_t>& data_;
  mutable std::size_t max_read_size_ = 0;
};

struct RowCollector {
  std::array<uint16_t, kPetFramePixels> pixels{};
  std::size_t rows = 0;
  bool reject_row_2 = false;
};

bool collect_row(
    void* context, std::size_t row,
    std::span<const uint16_t, kPetFrameWidth> pixels) {
  auto& collector = *static_cast<RowCollector*>(context);
  if (collector.reject_row_2 && row == 2) return false;
  std::copy(pixels.begin(), pixels.end(),
            collector.pixels.begin() + row * kPetFrameWidth);
  ++collector.rows;
  return true;
}
}  // namespace

int main() {
  for (bool rle : {false, true}) {
    auto bytes = valid_bundle(rle);
    MemorySource source(bytes);
    PetBundleMetadata metadata;
    const auto upload = digest(bytes);
    assert(validate_pet_bundle(source, upload, &metadata) ==
           PetBundleError::none);
    assert(metadata.pet_id == "rocky");
    assert(metadata.width == 96);
    std::array<uint16_t, 96 * 104> decoded{};
    source.reset_max_read_size();
    assert(decode_pet_frame(source, metadata, PetState::working, 3, decoded) ==
           PetBundleError::none);
    assert(decoded.front() == 0x100b);
    assert(decoded.back() == 0x100b);
    RowCollector collector;
    assert(decode_pet_frame_rows(
               source, metadata, PetState::working, 3,
               collect_row, &collector) == PetBundleError::none);
    assert(collector.rows == kPetFrameHeight);
    assert(collector.pixels == decoded);
    assert(source.max_read_size() <= kPetFrameWidth * 2);

    RowCollector rejected;
    rejected.reject_row_2 = true;
    assert(decode_pet_frame_rows(
               source, metadata, PetState::working, 3,
               collect_row, &rejected) == PetBundleError::consumer);
    assert(rejected.rows == 2);
  }

  {
    auto bytes = valid_bundle(false);
    bytes[0] = 'X';
    MemorySource source(bytes);
    assert(validate_pet_bundle(source, std::nullopt, nullptr) ==
           PetBundleError::magic);
  }
  {
    auto bytes = valid_bundle(false);
    put32(bytes, 8, 900 * 1024);
    MemorySource source(bytes);
    assert(validate_pet_bundle(source, std::nullopt, nullptr) ==
           PetBundleError::bounds);
  }
  {
    auto bytes = valid_bundle(true);
    put16(bytes, 812 + 2, 97);
    std::fill(bytes.begin() + 24, bytes.begin() + 56, 0);
    const auto content = digest(bytes);
    std::copy(content.begin(), content.end(), bytes.begin() + 24);
    MemorySource source(bytes);
    PetBundleMetadata metadata;
    assert(validate_pet_bundle(source, digest(bytes), &metadata) ==
           PetBundleError::none);
    std::array<uint16_t, 96 * 104> decoded{};
    assert(decode_pet_frame(source, metadata, PetState::idle, 0, decoded) ==
           PetBundleError::rle);
    RowCollector collector;
    assert(decode_pet_frame_rows(
               source, metadata, PetState::idle, 0,
               collect_row, &collector) == PetBundleError::rle);
    assert(collector.rows == 0);
  }
  {
    auto bytes = valid_bundle(false);
    auto upload = digest(bytes);
    upload[0] ^= 0xff;
    MemorySource source(bytes);
    assert(validate_pet_bundle(source, upload, nullptr) ==
           PetBundleError::digest);
  }
  return 0;
}
