#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "product/profile.hpp"

#ifdef ESP_PLATFORM
#include "esp_partition.h"
#endif

inline constexpr std::size_t kProfileCatalogBankAOffset = 0x1c0000;
inline constexpr std::size_t kProfileCatalogBankBOffset = 0x1d0000;
inline constexpr std::size_t kProfileCatalogBankBytes = 0x10000;
inline constexpr std::size_t kProfileCatalogHeaderBytes = 256;
inline constexpr std::size_t kProfileCatalogPayloadMaximum = 60 * 1024;
inline constexpr std::size_t kProfileCatalogMaximumCustomProfiles = 4;

struct ProfileSummary {
  std::array<char, 9> id{};
  std::array<char, 21> name{};
  uint32_t revision = 0;
  bool builtin = false;
};

class ProfileCatalogBackend {
 public:
  virtual ~ProfileCatalogBackend() = default;
  virtual bool read(std::size_t offset, std::span<uint8_t> output) = 0;
  virtual bool erase(std::size_t offset, std::size_t length) = 0;
  virtual bool write(
      std::size_t offset,
      std::span<const uint8_t> input
  ) = 0;
};

#ifdef ESP_PLATFORM
class EspProfileCatalogBackend final : public ProfileCatalogBackend {
 public:
  bool start();
  bool read(std::size_t offset, std::span<uint8_t> output) override;
  bool erase(std::size_t offset, std::size_t length) override;
 bool write(
      std::size_t offset,
      std::span<const uint8_t> input
  ) override;

 private:
  bool submit_storage_command(uint8_t command);
  bool do_read();
  bool do_erase();
  bool do_write();
  static void storage_task(void* context);
  struct Impl;
  Impl* impl_ = nullptr;
};
#endif

enum class ProfileCatalogLoadResult : uint8_t {
  loaded,
  empty,
  migrated,
  storage_error,
};

enum class ProfileCatalogResult : uint8_t {
  ok,
  not_found,
  invalid,
  revision_conflict,
  capacity,
  storage_error,
  active_profile,
  builtin_profile,
};

class ProfileCatalogStore {
 public:
  struct Entry {
    ProfileSummary summary{};
    uint32_t offset = 0;
    uint32_t length = 0;
  };

  explicit ProfileCatalogStore(ProfileCatalogBackend& backend);

  bool reserve_scratch();
  void release_scratch();
  ProfileCatalogLoadResult load(
      std::optional<std::string_view> legacy_json
  );
  ProfileCatalogResult list(
      std::span<ProfileSummary> output,
      std::size_t* count
  ) const;
  ProfileCatalogResult read(std::string_view id, Profile& output) const;
  ProfileCatalogResult create(
      std::optional<std::string_view> clone_id,
      std::string_view name,
      std::array<char, 9>* created_id
  );
  ProfileCatalogResult publish(
      std::string_view id,
      Profile& profile,
      uint32_t expected_revision
  );
  ProfileCatalogResult remove(std::string_view id);
  ProfileCatalogResult activate(std::string_view id);

  [[nodiscard]] uint32_t sequence() const { return sequence_; }
  [[nodiscard]] std::size_t active_bank_offset() const {
    return active_bank_offset_;
  }
  [[nodiscard]] std::string_view active_id() const {
    return active_id_.data();
  }

 private:
  ProfileCatalogBackend& backend_;
  std::array<Entry, kProfileCatalogMaximumCustomProfiles> entries_{};
  std::size_t entry_count_ = 0;
  uint32_t sequence_ = 0;
  std::size_t active_bank_offset_ = kProfileCatalogBankAOffset;
  std::array<char, 9> active_id_{'S', 'A', 'F', 'E', '\0'};
  std::unique_ptr<Profile> scratch_;

  std::optional<std::size_t> find(std::string_view id) const;
  ProfileCatalogResult finish_transaction(ProfileCatalogResult result);
  ProfileCatalogResult commit(
      std::span<const Entry> next,
      std::optional<std::string_view> replacement_id,
      std::optional<std::string_view> replacement_json,
      Profile* verification_profile = nullptr
  );
};
