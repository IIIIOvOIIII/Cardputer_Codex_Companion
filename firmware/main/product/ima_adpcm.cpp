#include "product/ima_adpcm.hpp"

#include <algorithm>
#include <array>

namespace {

constexpr std::array<int, 89> kStepTable{
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
    19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
    5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

constexpr std::array<int, 16> kIndexAdjustment{
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

bool valid_sample_count(size_t count) {
  return count == 448 || count == 456;
}

}  // namespace

ImaAdpcmError ima_adpcm_encode_block(std::span<const int16_t> samples,
                                     std::span<uint8_t> output,
                                     size_t* written) {
  if (written == nullptr) {
    return ImaAdpcmError::null_output;
  }
  *written = 0;
  if (!valid_sample_count(samples.size())) {
    return ImaAdpcmError::invalid_sample_count;
  }
  const size_t required = 4 + samples.size() / 2;
  if (output.size() < required) {
    return ImaAdpcmError::output_too_small;
  }

  std::fill_n(output.begin(), required, 0);
  int predictor = samples.front();
  int step_index = 0;
  output[0] = static_cast<uint8_t>(predictor & 0xFF);
  output[1] = static_cast<uint8_t>((predictor >> 8) & 0xFF);
  output[2] = static_cast<uint8_t>(step_index);
  output[3] = 0;

  size_t output_index = 4;
  bool low_nibble = true;
  for (size_t sample_index = 1; sample_index < samples.size();
       ++sample_index) {
    int difference = static_cast<int>(samples[sample_index]) - predictor;
    uint8_t code = 0;
    if (difference < 0) {
      code = 8;
      difference = -difference;
    }

    const int step = kStepTable[step_index];
    int delta = step >> 3;
    if (difference >= step) {
      code |= 4;
      difference -= step;
      delta += step;
    }
    if (difference >= (step >> 1)) {
      code |= 2;
      difference -= step >> 1;
      delta += step >> 1;
    }
    if (difference >= (step >> 2)) {
      code |= 1;
      delta += step >> 2;
    }

    predictor += (code & 8U) != 0 ? -delta : delta;
    predictor = std::clamp(predictor, -32768, 32767);
    step_index =
        std::clamp(step_index + kIndexAdjustment[code], 0, 88);

    if (low_nibble) {
      output[output_index] = code;
      low_nibble = false;
    } else {
      output[output_index] |= static_cast<uint8_t>(code << 4U);
      ++output_index;
      low_nibble = true;
    }
  }
  *written = required;
  return ImaAdpcmError::none;
}
