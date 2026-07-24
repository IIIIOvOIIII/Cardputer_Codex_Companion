#pragma once

#include <cstdint>

#include "product/profile.hpp"

struct ProfileBlob {
  Profile profile;
  uint32_t crc32 = 0;
};

class ProfileStoreBackend {
 public:
  virtual ~ProfileStoreBackend() = default;
  virtual bool read(uint8_t slot, ProfileBlob& output) = 0;
  virtual bool write(uint8_t slot, const ProfileBlob& input) = 0;
};

enum class ProfileLoadResult : uint8_t {
  stored,
  safe_fallback,
};

enum class ProfilePublishResult : uint8_t {
  ok,
  revision_conflict,
  invalid,
  storage_error,
};

class ProfileStore {
 public:
  explicit ProfileStore(ProfileStoreBackend& backend) : backend_(backend) {}
  ProfileLoadResult load(Profile& output);
  ProfilePublishResult publish(const Profile& profile,
                               uint32_t expected_revision);

 private:
  bool valid(const ProfileBlob& blob) const;
  ProfileStoreBackend& backend_;
  uint8_t active_slot_ = 0;
  uint32_t active_revision_ = 1;
};
