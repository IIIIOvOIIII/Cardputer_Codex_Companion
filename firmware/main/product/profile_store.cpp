#include "product/profile_store.hpp"

bool ProfileStore::valid(const ProfileBlob& blob) const {
  return validate_profile(blob.profile) == ProfileError::none &&
         blob.crc32 == profile_crc32(blob.profile);
}

ProfileLoadResult ProfileStore::load(Profile& output) {
  ProfileBlob first;
  ProfileBlob second;
  const bool first_valid = backend_.read(0, first) && valid(first);
  const bool second_valid = backend_.read(1, second) && valid(second);
  if (!first_valid && !second_valid) {
    output = safe_profile();
    active_slot_ = 0;
    active_revision_ = output.revision;
    return ProfileLoadResult::safe_fallback;
  }
  if (second_valid &&
      (!first_valid || second.profile.revision > first.profile.revision)) {
    output = second.profile;
    active_slot_ = 1;
  } else {
    output = first.profile;
    active_slot_ = 0;
  }
  active_revision_ = output.revision;
  return ProfileLoadResult::stored;
}

ProfilePublishResult ProfileStore::publish(const Profile& profile,
                                           uint32_t expected_revision) {
  if (validate_profile(profile) != ProfileError::none) {
    return ProfilePublishResult::invalid;
  }
  if (expected_revision != active_revision_ ||
      profile.revision != expected_revision + 1) {
    return ProfilePublishResult::revision_conflict;
  }
  const uint8_t target = active_slot_ ^ 1u;
  const ProfileBlob blob{.profile = profile, .crc32 = profile_crc32(profile)};
  if (!backend_.write(target, blob)) {
    return ProfilePublishResult::storage_error;
  }
  active_slot_ = target;
  active_revision_ = profile.revision;
  return ProfilePublishResult::ok;
}
