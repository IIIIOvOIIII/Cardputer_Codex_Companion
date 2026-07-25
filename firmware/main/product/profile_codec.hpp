#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "product/profile.hpp"

inline constexpr std::size_t kProfileJsonMaximumBytes = 16 * 1024;

enum class ProfileCodecResult : uint8_t {
  ok,
  malformed,
  invalid,
  too_large,
  allocation_error,
};

ProfileCodecResult encode_profile(const Profile& profile,
                                  std::string& output);
ProfileCodecResult decode_profile(std::string_view json, Profile& output);
