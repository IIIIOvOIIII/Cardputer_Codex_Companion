#include "product/profile_catalog.hpp"
#include "product/storage_partition_label.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "product/profile_codec.hpp"

#ifdef ESP_PLATFORM
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

namespace {
constexpr std::array<uint8_t, 4> kMagic{'C', 'C', 'P', 'F'};
constexpr uint16_t kSchema = 1;
constexpr std::size_t kCrcOffset = 20;
constexpr std::size_t kEntryOffset = 24;
constexpr std::size_t kEntryBytes = 48;
constexpr std::size_t kChunkBytes = 4096;
using CatalogChunk = std::array<uint8_t, kChunkBytes>;
#ifdef ESP_PLATFORM
constexpr uint8_t kCommandRead = 1;
constexpr uint8_t kCommandErase = 2;
constexpr uint8_t kCommandWrite = 3;
constexpr std::size_t kFlashEraseSectorBytes = 4096;
#endif

std::unique_ptr<CatalogChunk> allocate_catalog_chunk() {
  return std::unique_ptr<CatalogChunk>(
      new (std::nothrow) CatalogChunk());
}

uint16_t get_u16(const uint8_t* value) {
  return static_cast<uint16_t>(value[0]) |
         (static_cast<uint16_t>(value[1]) << 8);
}

uint32_t get_u32(const uint8_t* value) {
  return static_cast<uint32_t>(value[0]) |
         (static_cast<uint32_t>(value[1]) << 8) |
         (static_cast<uint32_t>(value[2]) << 16) |
         (static_cast<uint32_t>(value[3]) << 24);
}

void put_u16(uint8_t* output, uint16_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
}

void put_u32(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
  output[2] = static_cast<uint8_t>(value >> 16);
  output[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t crc32_update(uint32_t crc, std::span<const uint8_t> bytes) {
  for (const uint8_t byte : bytes) {
    crc ^= byte;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1) ^ (0xedb88320u & mask);
    }
  }
  return crc;
}

bool valid_c_string(const char* value, std::size_t capacity) {
  return std::memchr(value, '\0', capacity) != nullptr;
}

bool valid_id(std::string_view id) {
  if (id.size() != 8) return false;
  return std::all_of(id.begin(), id.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
  });
}

void assign_string(std::span<char> output, std::string_view value) {
  std::fill(output.begin(), output.end(), '\0');
  std::copy_n(value.begin(), std::min(value.size(), output.size() - 1),
              output.begin());
}

std::array<uint8_t, kProfileCatalogHeaderBytes> make_header(
    std::span<const ProfileCatalogStore::Entry> entries,
    uint32_t sequence,
    uint32_t payload_length
) {
  std::array<uint8_t, kProfileCatalogHeaderBytes> header{};
  std::copy(kMagic.begin(), kMagic.end(), header.begin());
  put_u16(header.data() + 4, kSchema);
  put_u16(header.data() + 6, kProfileCatalogHeaderBytes);
  put_u32(header.data() + 8, sequence);
  put_u32(header.data() + 12, static_cast<uint32_t>(entries.size()));
  put_u32(header.data() + 16, payload_length);
  put_u32(header.data() + kCrcOffset, 0);
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const auto& entry = entries[index];
    uint8_t* encoded = header.data() + kEntryOffset + index * kEntryBytes;
    std::memcpy(encoded, entry.summary.id.data(), entry.summary.id.size());
    std::memcpy(encoded + 9, entry.summary.name.data(),
                entry.summary.name.size());
    put_u32(encoded + 30, entry.summary.revision);
    put_u32(encoded + 34, entry.offset);
    put_u32(encoded + 38, entry.length);
  }
  return header;
}

struct LoadedBank {
  bool valid = false;
  uint32_t sequence = 0;
  std::size_t offset = 0;
  std::array<ProfileCatalogStore::Entry,
             kProfileCatalogMaximumCustomProfiles> entries{};
  std::size_t count = 0;
};

LoadedBank inspect_bank(ProfileCatalogBackend& backend,
                        std::size_t bank_offset, Profile& scratch) {
  LoadedBank result;
  result.offset = bank_offset;
  std::array<uint8_t, kProfileCatalogHeaderBytes> header{};
  if (!backend.read(bank_offset, header) ||
      !std::equal(kMagic.begin(), kMagic.end(), header.begin()) ||
      get_u16(header.data() + 4) != kSchema ||
      get_u16(header.data() + 6) != kProfileCatalogHeaderBytes) {
    return result;
  }
  const uint32_t count = get_u32(header.data() + 12);
  const uint32_t payload_length = get_u32(header.data() + 16);
  if (count > kProfileCatalogMaximumCustomProfiles ||
      payload_length > kProfileCatalogPayloadMaximum ||
      kProfileCatalogHeaderBytes + payload_length >
          kProfileCatalogBankBytes) {
    return result;
  }

  uint32_t prior_end = 0;
  for (uint32_t index = 0; index < count; ++index) {
    const uint8_t* encoded =
        header.data() + kEntryOffset + index * kEntryBytes;
    auto& entry = result.entries[index];
    std::memcpy(entry.summary.id.data(), encoded, entry.summary.id.size());
    std::memcpy(entry.summary.name.data(), encoded + 9,
                entry.summary.name.size());
    entry.summary.revision = get_u32(encoded + 30);
    entry.summary.builtin = false;
    entry.offset = get_u32(encoded + 34);
    entry.length = get_u32(encoded + 38);
    const std::string_view id(entry.summary.id.data(),
                              strnlen(entry.summary.id.data(), 9));
    if (!valid_c_string(entry.summary.id.data(), entry.summary.id.size()) ||
        !valid_c_string(entry.summary.name.data(),
                        entry.summary.name.size()) ||
        !valid_id(id) || entry.summary.revision == 0 ||
        entry.length == 0 || entry.length > kProfileJsonMaximumBytes ||
        entry.offset < prior_end ||
        entry.offset > payload_length ||
        entry.length > payload_length - entry.offset) {
      return result;
    }
    prior_end = entry.offset + entry.length;
  }

  const uint32_t expected_crc = get_u32(header.data() + kCrcOffset);
  put_u32(header.data() + kCrcOffset, 0);
  uint32_t crc = crc32_update(0xffffffffu, header);
  auto chunk = allocate_catalog_chunk();
  if (chunk == nullptr) return result;
  std::size_t position = 0;
  while (position < payload_length) {
    const std::size_t size =
        std::min<std::size_t>(chunk->size(), payload_length - position);
    if (!backend.read(bank_offset + kProfileCatalogHeaderBytes + position,
                      std::span(*chunk).first(size))) {
      return result;
    }
    crc = crc32_update(crc, std::span(*chunk).first(size));
    position += size;
  }
  if ((crc ^ 0xffffffffu) != expected_crc) return result;

  for (uint32_t index = 0; index < count; ++index) {
    const auto& entry = result.entries[index];
    std::string json(entry.length, '\0');
    std::size_t read_position = 0;
    while (read_position < entry.length) {
      const std::size_t size =
          std::min<std::size_t>(kChunkBytes, entry.length - read_position);
      if (!backend.read(
              bank_offset + kProfileCatalogHeaderBytes + entry.offset +
                  read_position,
              std::span(
                  reinterpret_cast<uint8_t*>(json.data() + read_position),
                  size))) {
        return result;
      }
      read_position += size;
    }
    if (decode_profile(json, scratch) != ProfileCodecResult::ok ||
        scratch.revision != entry.summary.revision ||
        scratch.name != entry.summary.name.data()) {
      return result;
    }
  }

  result.valid = true;
  result.sequence = get_u32(header.data() + 8);
  result.count = count;
  return result;
}

}  // namespace

#ifdef ESP_PLATFORM
struct EspProfileCatalogBackend::Impl {
  const esp_partition_t* partition = nullptr;
  StaticSemaphore_t command_mutex_storage{};
  SemaphoreHandle_t command_mutex = nullptr;
  StaticSemaphore_t completion_storage{};
  SemaphoreHandle_t completion = nullptr;
  StaticQueue_t command_queue_storage{};
  std::array<uint8_t, 1> command_queue_buffer{};
  QueueHandle_t command_queue = nullptr;
  StaticTask_t storage_task_storage{};
  std::array<StackType_t, 2048> storage_task_stack{};
  TaskHandle_t storage_task_handle = nullptr;
  std::atomic<bool> command_active{false};
  std::size_t command_offset = 0;
  std::size_t command_length = 0;
  std::array<uint8_t, kChunkBytes> command_input{};
  bool command_result = false;
};

bool EspProfileCatalogBackend::start() {
  if (impl_ != nullptr) return true;
  auto impl = std::make_unique<Impl>();
  impl->partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
      kProductStoragePartitionLabel);
  impl->command_mutex =
      xSemaphoreCreateMutexStatic(&impl->command_mutex_storage);
  impl->completion =
      xSemaphoreCreateBinaryStatic(&impl->completion_storage);
  impl->command_queue = xQueueCreateStatic(
      1, sizeof(uint8_t), impl->command_queue_buffer.data(),
      &impl->command_queue_storage);
  if (impl->partition == nullptr ||
      impl->partition->size <
          kProfileCatalogBankBOffset + kProfileCatalogBankBytes ||
      impl->command_mutex == nullptr || impl->completion == nullptr ||
      impl->command_queue == nullptr) {
    return false;
  }
  impl_ = impl.release();
  impl_->storage_task_handle = xTaskCreateStatic(
      storage_task, "profile-storage", impl_->storage_task_stack.size(),
      this, tskIDLE_PRIORITY, impl_->storage_task_stack.data(),
      &impl_->storage_task_storage);
  if (impl_->storage_task_handle == nullptr) {
    delete impl_;
    impl_ = nullptr;
    return false;
  }
  return true;
}

bool EspProfileCatalogBackend::read(
    std::size_t offset,
    std::span<uint8_t> output
) {
  if (impl_ == nullptr || impl_->partition == nullptr ||
      offset > impl_->partition->size ||
      output.size() > impl_->partition->size - offset ||
      output.size() > impl_->command_input.size() ||
      xSemaphoreTake(impl_->command_mutex, pdMS_TO_TICKS(30000)) != pdTRUE) {
    return false;
  }
  bool result = false;
  if (!impl_->command_active.load()) {
    impl_->command_offset = offset;
    impl_->command_length = output.size();
    result = submit_storage_command(kCommandRead);
    if (result) {
      std::copy_n(
          impl_->command_input.begin(), output.size(), output.begin());
    }
  }
  xSemaphoreGive(impl_->command_mutex);
  return result;
}

bool EspProfileCatalogBackend::erase(
    std::size_t offset,
    std::size_t length
) {
  if (impl_ == nullptr || impl_->partition == nullptr ||
      offset > impl_->partition->size ||
      length > impl_->partition->size - offset ||
      offset % kFlashEraseSectorBytes != 0 ||
      length % kFlashEraseSectorBytes != 0) {
    return false;
  }
  if (xSemaphoreTake(impl_->command_mutex, pdMS_TO_TICKS(30000)) != pdTRUE) {
    return false;
  }
  bool result = false;
  if (!impl_->command_active.load()) {
    impl_->command_offset = offset;
    impl_->command_length = length;
    result = submit_storage_command(kCommandErase);
  }
  xSemaphoreGive(impl_->command_mutex);
  return result;
}

bool EspProfileCatalogBackend::write(
    std::size_t offset,
    std::span<const uint8_t> input
) {
  if (impl_ == nullptr || impl_->partition == nullptr ||
      offset > impl_->partition->size ||
      input.size() > impl_->partition->size - offset ||
      input.size() > impl_->command_input.size() ||
      xSemaphoreTake(impl_->command_mutex, pdMS_TO_TICKS(30000)) != pdTRUE) {
    return false;
  }
  bool result = false;
  if (!impl_->command_active.load()) {
    impl_->command_offset = offset;
    impl_->command_length = input.size();
    std::copy(input.begin(), input.end(), impl_->command_input.begin());
    result = submit_storage_command(kCommandWrite);
  }
  xSemaphoreGive(impl_->command_mutex);
  return result;
}

bool EspProfileCatalogBackend::submit_storage_command(uint8_t command) {
  while (xSemaphoreTake(impl_->completion, 0) == pdTRUE) {
  }
  impl_->command_active.store(true);
  if (xQueueSend(impl_->command_queue, &command, 0) != pdTRUE) {
    impl_->command_active.store(false);
    return false;
  }
  if (xSemaphoreTake(impl_->completion, pdMS_TO_TICKS(30000)) != pdTRUE) {
    return false;
  }
  return impl_->command_result;
}

bool EspProfileCatalogBackend::do_read() {
  return esp_partition_read(
             impl_->partition, impl_->command_offset,
             impl_->command_input.data(), impl_->command_length) == ESP_OK;
}

bool EspProfileCatalogBackend::do_erase() {
  for (std::size_t erased = 0; erased < impl_->command_length;
       erased += kFlashEraseSectorBytes) {
    vTaskDelay(pdMS_TO_TICKS(10));
    if (esp_partition_erase_range(
            impl_->partition, impl_->command_offset + erased,
            kFlashEraseSectorBytes) != ESP_OK) {
      return false;
    }
  }
  return true;
}

bool EspProfileCatalogBackend::do_write() {
  return esp_partition_write(
             impl_->partition, impl_->command_offset,
             impl_->command_input.data(), impl_->command_length) == ESP_OK;
}

void EspProfileCatalogBackend::storage_task(void* context) {
  auto* backend = static_cast<EspProfileCatalogBackend*>(context);
  uint8_t command = 0;
  while (true) {
    if (xQueueReceive(
            backend->impl_->command_queue, &command, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    switch (command) {
      case kCommandRead:
        backend->impl_->command_result = backend->do_read();
        break;
      case kCommandErase:
        backend->impl_->command_result = backend->do_erase();
        break;
      case kCommandWrite:
        backend->impl_->command_result = backend->do_write();
        break;
      default:
        backend->impl_->command_result = false;
        break;
    }
    backend->impl_->command_active.store(false);
    xSemaphoreGive(backend->impl_->completion);
  }
}
#endif

ProfileCatalogStore::ProfileCatalogStore(ProfileCatalogBackend& backend)
    : backend_(backend) {}

bool ProfileCatalogStore::reserve_scratch() {
  if (scratch_ != nullptr) return true;
  scratch_.reset(new (std::nothrow) Profile());
  return scratch_ != nullptr;
}

void ProfileCatalogStore::release_scratch() {
  scratch_.reset();
}

ProfileCatalogResult ProfileCatalogStore::finish_transaction(
    ProfileCatalogResult result
) {
  release_scratch();
  return result;
}

std::optional<std::size_t> ProfileCatalogStore::find(
    std::string_view id
) const {
  for (std::size_t index = 0; index < entry_count_; ++index) {
    if (id == entries_[index].summary.id.data()) return index;
  }
  return std::nullopt;
}

ProfileCatalogLoadResult ProfileCatalogStore::load(
    std::optional<std::string_view> legacy_json
) {
  if (!reserve_scratch()) return ProfileCatalogLoadResult::storage_error;
  Profile& scratch = *scratch_;
  const LoadedBank first =
      inspect_bank(backend_, kProfileCatalogBankAOffset, scratch);
  const LoadedBank second =
      inspect_bank(backend_, kProfileCatalogBankBOffset, scratch);
  const LoadedBank* selected = nullptr;
  if (first.valid && second.valid) {
    selected = first.sequence >= second.sequence ? &first : &second;
  } else if (first.valid) {
    selected = &first;
  } else if (second.valid) {
    selected = &second;
  }
  if (selected != nullptr) {
    entries_ = selected->entries;
    entry_count_ = selected->count;
    sequence_ = selected->sequence;
    active_bank_offset_ = selected->offset;
    release_scratch();
    return ProfileCatalogLoadResult::loaded;
  }

  entries_ = {};
  entry_count_ = 0;
  sequence_ = 0;
  active_bank_offset_ = kProfileCatalogBankBOffset;
  if (!legacy_json.has_value() || legacy_json->empty()) {
    release_scratch();
    return ProfileCatalogLoadResult::empty;
  }
  if (decode_profile(*legacy_json, scratch) != ProfileCodecResult::ok) {
    release_scratch();
    return ProfileCatalogLoadResult::empty;
  }
  scratch.name = "IMPORTED";
  if (scratch.revision == 0) scratch.revision = 1;
  std::string encoded;
  if (encode_profile(scratch, encoded) != ProfileCodecResult::ok) {
    release_scratch();
    return ProfileCatalogLoadResult::empty;
  }
  Entry migrated;
  assign_string(migrated.summary.id, "00000001");
  assign_string(migrated.summary.name, scratch.name);
  migrated.summary.revision = scratch.revision;
  migrated.length = static_cast<uint32_t>(encoded.size());
  if (commit(std::span(&migrated, 1), "00000001", encoded) !=
      ProfileCatalogResult::ok) {
    return ProfileCatalogLoadResult::storage_error;
  }
  return ProfileCatalogLoadResult::migrated;
}

ProfileCatalogResult ProfileCatalogStore::list(
    std::span<ProfileSummary> output,
    std::size_t* count
) const {
  if (count == nullptr || output.size() < entry_count_ + 1) {
    return ProfileCatalogResult::invalid;
  }
  ProfileSummary safe;
  assign_string(safe.id, "SAFE");
  assign_string(safe.name, "SAFE");
  safe.revision = 1;
  safe.builtin = true;
  output[0] = safe;
  for (std::size_t index = 0; index < entry_count_; ++index) {
    output[index + 1] = entries_[index].summary;
  }
  *count = entry_count_ + 1;
  return ProfileCatalogResult::ok;
}

ProfileCatalogResult ProfileCatalogStore::read(
    std::string_view id,
    Profile& output
) const {
  if (id == "SAFE") {
    reset_to_safe_profile(output);
    return ProfileCatalogResult::ok;
  }
  const auto index = find(id);
  if (!index.has_value()) return ProfileCatalogResult::not_found;
  const Entry& entry = entries_[*index];
  std::string json(entry.length, '\0');
  std::size_t position = 0;
  while (position < entry.length) {
    const std::size_t size =
        std::min<std::size_t>(kChunkBytes, entry.length - position);
    if (!backend_.read(
            active_bank_offset_ + kProfileCatalogHeaderBytes + entry.offset +
                position,
            std::span(reinterpret_cast<uint8_t*>(json.data() + position),
                      size))) {
      return ProfileCatalogResult::storage_error;
    }
    position += size;
  }
  if (decode_profile(json, output) != ProfileCodecResult::ok) {
    return ProfileCatalogResult::invalid;
  }
  return ProfileCatalogResult::ok;
}

ProfileCatalogResult ProfileCatalogStore::create(
    std::optional<std::string_view> clone_id,
    std::string_view name,
    std::array<char, 9>* created_id
) {
  if (created_id == nullptr || name.empty() || name.size() > 20 ||
      entry_count_ >= kProfileCatalogMaximumCustomProfiles) {
    return entry_count_ >= kProfileCatalogMaximumCustomProfiles
               ? ProfileCatalogResult::capacity
               : ProfileCatalogResult::invalid;
  }
  if (!reserve_scratch()) return ProfileCatalogResult::storage_error;
  Profile& scratch = *scratch_;
  if (clone_id.has_value()) {
    const ProfileCatalogResult result = read(*clone_id, scratch);
    if (result != ProfileCatalogResult::ok) {
      return finish_transaction(result);
    }
  } else {
    scratch.name = "SAFE";
    scratch.revision = 1;
    for (KeyBinding& binding : scratch.bindings) {
      binding.action = KeyAction{};
    }
  }
  scratch.name = std::string(name);
  scratch.revision = 1;
  std::string json;
  if (encode_profile(scratch, json) != ProfileCodecResult::ok) {
    return finish_transaction(ProfileCatalogResult::invalid);
  }

  uint32_t seed = sequence_ + 1;
  std::array<char, 9> id{};
  do {
    std::snprintf(id.data(), id.size(), "%08" PRIX32, seed++);
  } while (find(id.data()).has_value());

  std::array<Entry, kProfileCatalogMaximumCustomProfiles> next = entries_;
  Entry& created = next[entry_count_];
  assign_string(created.summary.id, id.data());
  assign_string(created.summary.name, name);
  created.summary.revision = 1;
  created.summary.builtin = false;
  created.length = static_cast<uint32_t>(json.size());
  const ProfileCatalogResult result =
      commit(std::span(next).first(entry_count_ + 1), id.data(), json);
  if (result == ProfileCatalogResult::ok) *created_id = id;
  return result;
}

ProfileCatalogResult ProfileCatalogStore::publish(
    std::string_view id,
    Profile& profile,
    uint32_t expected_revision
) {
  if (id == "SAFE") return ProfileCatalogResult::builtin_profile;
  const auto index = find(id);
  if (!index.has_value()) return ProfileCatalogResult::not_found;
  if (entries_[*index].summary.revision != expected_revision ||
      profile.revision != expected_revision) {
    return ProfileCatalogResult::revision_conflict;
  }
  profile.revision = expected_revision + 1;
  std::string json;
  if (encode_profile(profile, json) != ProfileCodecResult::ok) {
    profile.revision = expected_revision;
    return ProfileCatalogResult::invalid;
  }
  std::array<Entry, kProfileCatalogMaximumCustomProfiles> next = entries_;
  assign_string(next[*index].summary.name, profile.name);
  next[*index].summary.revision = profile.revision;
  next[*index].length = static_cast<uint32_t>(json.size());
  return commit(std::span(next).first(entry_count_), id, json, &profile);
}

ProfileCatalogResult ProfileCatalogStore::remove(std::string_view id) {
  if (id == "SAFE") return ProfileCatalogResult::builtin_profile;
  if (id == active_id()) return ProfileCatalogResult::active_profile;
  const auto index = find(id);
  if (!index.has_value()) return ProfileCatalogResult::not_found;
  std::array<Entry, kProfileCatalogMaximumCustomProfiles> next{};
  std::size_t output = 0;
  for (std::size_t current = 0; current < entry_count_; ++current) {
    if (current != *index) next[output++] = entries_[current];
  }
  return commit(std::span(next).first(output), std::nullopt, std::nullopt);
}

ProfileCatalogResult ProfileCatalogStore::activate(std::string_view id) {
  if (id != "SAFE" && !find(id).has_value()) {
    return ProfileCatalogResult::not_found;
  }
  assign_string(active_id_, id);
  return ProfileCatalogResult::ok;
}

ProfileCatalogResult ProfileCatalogStore::commit(
    std::span<const Entry> requested,
    std::optional<std::string_view> replacement_id,
    std::optional<std::string_view> replacement_json,
    Profile* verification_profile
) {
  if (verification_profile == nullptr && !reserve_scratch()) {
    return ProfileCatalogResult::storage_error;
  }
  Profile& verification =
      verification_profile == nullptr ? *scratch_ : *verification_profile;
  std::array<Entry, kProfileCatalogMaximumCustomProfiles> next{};
  uint32_t payload_length = 0;
  for (std::size_t index = 0; index < requested.size(); ++index) {
    next[index] = requested[index];
    next[index].offset = payload_length;
    if (next[index].length == 0 ||
        next[index].length > kProfileJsonMaximumBytes ||
        payload_length > kProfileCatalogPayloadMaximum -
                             next[index].length) {
      return finish_transaction(ProfileCatalogResult::invalid);
    }
    payload_length += next[index].length;
  }

  const uint32_t next_sequence = sequence_ + 1;
  const std::size_t target =
      active_bank_offset_ == kProfileCatalogBankAOffset
          ? kProfileCatalogBankBOffset
          : kProfileCatalogBankAOffset;
  auto header =
      make_header(std::span(next).first(requested.size()), next_sequence,
                  payload_length);
  if (!backend_.erase(target, kProfileCatalogBankBytes)) {
    return finish_transaction(ProfileCatalogResult::storage_error);
  }
  uint32_t crc = crc32_update(0xffffffffu, header);
  std::unique_ptr<CatalogChunk> chunk;
  for (const Entry& destination : std::span(next).first(requested.size())) {
    const std::string_view id(destination.summary.id.data());
    if (replacement_id.has_value() && replacement_json.has_value() &&
        id == *replacement_id) {
      std::size_t position = 0;
      while (position < replacement_json->size()) {
        const std::size_t size =
            std::min<std::size_t>(kChunkBytes,
                                  replacement_json->size() - position);
        const auto bytes = std::span(
            reinterpret_cast<const uint8_t*>(
                replacement_json->data() + position),
            size);
        if (!backend_.write(target + kProfileCatalogHeaderBytes +
                                destination.offset + position,
                            bytes)) {
          return finish_transaction(ProfileCatalogResult::storage_error);
        }
        crc = crc32_update(crc, bytes);
        position += size;
      }
      continue;
    }

    const auto source_index = find(id);
    if (!source_index.has_value()) {
      return finish_transaction(ProfileCatalogResult::invalid);
    }
    if (chunk == nullptr) {
      chunk = allocate_catalog_chunk();
      if (chunk == nullptr) {
        return finish_transaction(ProfileCatalogResult::storage_error);
      }
    }
    const Entry& source = entries_[*source_index];
    std::size_t position = 0;
    while (position < source.length) {
      const std::size_t size =
          std::min<std::size_t>(chunk->size(), source.length - position);
      auto bytes = std::span(*chunk).first(size);
      if (!backend_.read(active_bank_offset_ + kProfileCatalogHeaderBytes +
                             source.offset + position,
                         bytes) ||
          !backend_.write(target + kProfileCatalogHeaderBytes +
                              destination.offset + position,
                          bytes)) {
        return finish_transaction(ProfileCatalogResult::storage_error);
      }
      crc = crc32_update(crc, bytes);
      position += size;
    }
  }
  const uint32_t completed_crc = crc ^ 0xffffffffu;
  put_u32(header.data() + kCrcOffset, completed_crc);
  if (!backend_.write(target, header)) {
    return finish_transaction(ProfileCatalogResult::storage_error);
  }
  const LoadedBank verified = inspect_bank(backend_, target, verification);
  if (!verified.valid || verified.sequence != next_sequence) {
    return finish_transaction(ProfileCatalogResult::storage_error);
  }
  entries_ = verified.entries;
  entry_count_ = verified.count;
  sequence_ = verified.sequence;
  active_bank_offset_ = target;
  return finish_transaction(ProfileCatalogResult::ok);
}
