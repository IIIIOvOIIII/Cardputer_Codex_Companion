#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

enum class ImaAdpcmError : uint8_t {
  none,
  null_output,
  invalid_sample_count,
  output_too_small,
};

ImaAdpcmError ima_adpcm_encode_block(std::span<const int16_t> samples,
                                     std::span<uint8_t> output,
                                     size_t* written);
