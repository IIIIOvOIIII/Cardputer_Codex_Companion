#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "product/pet_bundle.hpp"

enum class PetSlot : uint8_t { a = 0, b = 1, none = 0xff };

constexpr PetSlot inactive_pet_slot(PetSlot active) {
  return active == PetSlot::a ? PetSlot::b : PetSlot::a;
}

constexpr PetSlot select_boot_pet_slot(PetSlot selected, bool slot_a_valid,
                                       bool slot_b_valid) {
  if (selected == PetSlot::a && slot_a_valid) return PetSlot::a;
  if (selected == PetSlot::b && slot_b_valid) return PetSlot::b;
  if (slot_a_valid) return PetSlot::a;
  if (slot_b_valid) return PetSlot::b;
  return PetSlot::none;
}

constexpr bool pet_commit_can_activate(bool upload_valid,
                                       bool rename_succeeded,
                                       bool nvs_commit_succeeded) {
  return upload_valid && rename_succeeded && nvs_commit_succeeded;
}

struct PetUploadBegin {
  std::string pet_id;
  uint16_t format_version = 0;
  std::size_t length = 0;
  std::array<uint8_t, 32> upload_digest{};
};

struct PetUploadStatus {
  bool active = false;
  std::string transaction_id;
  std::size_t received = 0;
  std::size_t expected = 0;
};

struct PetStoreStatus {
  std::string pet_id;
  std::string digest;
  uint16_t format_version = 0;
  std::size_t storage_used = 0;
  PetUploadStatus transaction;
  std::string last_result = "none";
};

#ifdef ESP_PLATFORM
#include "esp_err.h"

class PetStore {
 public:
  esp_err_t start();
  esp_err_t begin(const PetUploadBegin& request, PetUploadStatus* output);
  esp_err_t append(std::string_view transaction_id, std::size_t offset,
                   std::span<const uint8_t> chunk,
                   std::span<const uint8_t, 32> chunk_digest);
  esp_err_t commit(std::string_view transaction_id);
  [[nodiscard]] PetStoreStatus status() const;
  bool decode(PetState state, uint8_t frame,
              std::span<uint16_t, kPetFramePixels> output);

 private:
  esp_err_t submit_upload_command(uint8_t command);
  esp_err_t do_begin(const PetUploadBegin& request, PetUploadStatus* output);
  esp_err_t do_append(std::string_view transaction_id, std::size_t offset,
                      std::span<const uint8_t> chunk,
                      std::span<const uint8_t, 32> chunk_digest);
  esp_err_t do_commit(std::string_view transaction_id);
  static void upload_task(void* context);
  struct Impl;
  Impl* impl_ = nullptr;
};
#endif
