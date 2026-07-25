#include "product/pet_store.hpp"

#ifdef ESP_PLATFORM

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>

#include "esp_partition.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

namespace {
constexpr char kPartition[] = "storage";
constexpr char kNamespace[] = "product";
constexpr char kSlotKey[] = "pet_slot";
constexpr char kDigestKey[] = "pet_digest";
constexpr std::size_t kSlotCapacity = 0xe0000;
constexpr std::size_t kEraseBlock = 0x1000;
constexpr uint8_t kCommandBegin = 1;
constexpr uint8_t kCommandAppend = 2;
constexpr uint8_t kCommandCommit = 3;
constexpr uint8_t kCommandInitialize = 4;

std::size_t slot_offset(PetSlot slot) {
  return slot == PetSlot::b ? kSlotCapacity : 0;
}

std::string hex(std::span<const uint8_t> bytes) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string output(bytes.size() * 2, '0');
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    output[index * 2] = digits[bytes[index] >> 4];
    output[index * 2 + 1] = digits[bytes[index] & 0x0f];
  }
  return output;
}

class PartitionSource final : public PetByteSource {
 public:
  PartitionSource(const esp_partition_t* partition, std::size_t base,
                  std::size_t size)
      : partition_(partition), base_(base), size_(size) {}
  bool valid() const {
    return partition_ != nullptr && base_ <= partition_->size &&
           size_ <= partition_->size - base_;
  }
  std::size_t size() const override { return size_; }
  bool read(std::size_t offset, std::span<uint8_t> output) const override {
    if (!valid() || offset > size_ || output.size() > size_ - offset) {
      return false;
    }
    return esp_partition_read(partition_, base_ + offset, output.data(),
                              output.size()) == ESP_OK;
  }

 private:
  const esp_partition_t* partition_ = nullptr;
  std::size_t base_ = 0;
  std::size_t size_ = 0;
};

class Lock {
 public:
  explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex) {
    locked_ = mutex_ != nullptr &&
              xSemaphoreTake(mutex_, pdMS_TO_TICKS(15000)) == pdTRUE;
  }
  ~Lock() {
    if (locked_) xSemaphoreGive(mutex_);
  }
  bool locked() const { return locked_; }

 private:
  SemaphoreHandle_t mutex_;
  bool locked_ = false;
};

std::size_t stored_bundle_size(const esp_partition_t* partition,
                               PetSlot slot) {
  std::array<uint8_t, 12> header{};
  if (partition == nullptr ||
      esp_partition_read(partition, slot_offset(slot), header.data(),
                         header.size()) != ESP_OK ||
      std::memcmp(header.data(), "CCPT", 4) != 0) {
    return 0;
  }
  const std::size_t size =
      static_cast<std::size_t>(header[8]) |
      (static_cast<std::size_t>(header[9]) << 8) |
      (static_cast<std::size_t>(header[10]) << 16) |
      (static_cast<std::size_t>(header[11]) << 24);
  return size > 0 && size <= kPetBundleMaximumBytes &&
                 size <= kSlotCapacity
             ? size
             : 0;
}

bool validate_slot(const esp_partition_t* partition, PetSlot slot,
                   std::size_t size, PetBundleMetadata* metadata) {
  if (size == 0) return false;
  PartitionSource source(partition, slot_offset(slot), size);
  return source.valid() &&
         validate_pet_bundle(source, std::nullopt, metadata) ==
             PetBundleError::none;
}

esp_err_t persist_slot(PetSlot slot,
                       std::span<const uint8_t, 32> digest) {
  nvs_handle_t handle;
  esp_err_t result = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (result != ESP_OK) return result;
  result = nvs_set_u8(handle, kSlotKey, static_cast<uint8_t>(slot));
  const std::string digest_text = hex(digest);
  if (result == ESP_OK) {
    result = nvs_set_str(handle, kDigestKey, digest_text.c_str());
  }
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result;
}
}  // namespace

struct PetStore::Impl {
  StaticSemaphore_t mutex_storage{};
  SemaphoreHandle_t mutex = nullptr;
  StaticSemaphore_t command_mutex_storage{};
  SemaphoreHandle_t command_mutex = nullptr;
  StaticSemaphore_t completion_storage{};
  SemaphoreHandle_t completion = nullptr;
  StaticQueue_t command_queue_storage{};
  std::array<uint8_t, 1> command_queue_buffer{};
  QueueHandle_t command_queue = nullptr;
  StaticTask_t upload_task_storage{};
  std::array<StackType_t, 8192> upload_task_stack{};
  TaskHandle_t upload_task_handle = nullptr;
  std::atomic<bool> command_active{false};
  PetUploadBegin command_begin;
  PetUploadStatus command_status;
  std::string command_transaction;
  std::size_t command_offset = 0;
  std::unique_ptr<uint8_t[]> command_chunk;
  std::size_t command_chunk_size = 0;
  std::array<uint8_t, 32> command_digest{};
  esp_err_t command_result = ESP_FAIL;
  const esp_partition_t* partition = nullptr;
  PetSlot active_slot = PetSlot::none;
  PetBundleMetadata active_metadata;
  std::size_t active_size = 0;
  PetStoreStatus public_status;
  PetUploadBegin upload_begin;
  std::size_t received = 0;
  std::string transaction_id;
  std::size_t previous_offset = 0;
  std::size_t previous_length = 0;
  std::array<uint8_t, 32> previous_digest{};
};

esp_err_t PetStore::start() {
  if (impl_ != nullptr) return ESP_ERR_INVALID_STATE;
  auto impl = std::make_unique<Impl>();
  impl->mutex = xSemaphoreCreateMutexStatic(&impl->mutex_storage);
  impl->command_mutex =
      xSemaphoreCreateMutexStatic(&impl->command_mutex_storage);
  impl->completion =
      xSemaphoreCreateBinaryStatic(&impl->completion_storage);
  impl->command_queue = xQueueCreateStatic(
      1, sizeof(uint8_t), impl->command_queue_buffer.data(),
      &impl->command_queue_storage);
  if (impl->mutex == nullptr || impl->command_mutex == nullptr ||
      impl->completion == nullptr || impl->command_queue == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  impl->partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, kPartition);
  if (impl->partition == nullptr ||
      impl->partition->size < 2 * kSlotCapacity) {
    return ESP_ERR_NOT_FOUND;
  }

  impl_ = impl.release();
  impl_->upload_task_handle = xTaskCreateStatic(
      upload_task, "product-pet-upload", impl_->upload_task_stack.size(),
      this, tskIDLE_PRIORITY, impl_->upload_task_stack.data(),
      &impl_->upload_task_storage);
  if (impl_->upload_task_handle == nullptr) {
    delete impl_;
    impl_ = nullptr;
    return ESP_ERR_NO_MEM;
  }
  const esp_err_t initialized = submit_upload_command(kCommandInitialize);
  if (initialized != ESP_OK) {
    vTaskDelete(impl_->upload_task_handle);
    delete impl_;
    impl_ = nullptr;
  }
  return initialized;
}

esp_err_t PetStore::do_initialize() {
  if (impl_ == nullptr) return ESP_ERR_INVALID_STATE;
  Lock lock(impl_->mutex);
  if (!lock.locked()) return ESP_ERR_TIMEOUT;
  uint8_t selected_raw = static_cast<uint8_t>(PetSlot::none);
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) == ESP_OK) {
    nvs_get_u8(handle, kSlotKey, &selected_raw);
    nvs_close(handle);
  }
  PetBundleMetadata slot_a;
  PetBundleMetadata slot_b;
  const std::size_t slot_a_size =
      stored_bundle_size(impl_->partition, PetSlot::a);
  const std::size_t slot_b_size =
      stored_bundle_size(impl_->partition, PetSlot::b);
  const bool a_valid =
      validate_slot(impl_->partition, PetSlot::a, slot_a_size, &slot_a);
  const bool b_valid =
      validate_slot(impl_->partition, PetSlot::b, slot_b_size, &slot_b);
  const PetSlot selected =
      selected_raw <= 1 ? static_cast<PetSlot>(selected_raw) : PetSlot::none;
  impl_->active_slot = select_boot_pet_slot(selected, a_valid, b_valid);
  if (impl_->active_slot == PetSlot::none) return ESP_OK;
  impl_->active_metadata =
      impl_->active_slot == PetSlot::a ? slot_a : slot_b;
  impl_->active_size =
      impl_->active_slot == PetSlot::a ? slot_a_size : slot_b_size;
  impl_->public_status.pet_id = impl_->active_metadata.pet_id;
  impl_->public_status.digest = hex(impl_->active_metadata.content_digest);
  impl_->public_status.format_version =
      impl_->active_metadata.schema_version;
  impl_->public_status.storage_used = impl_->active_size;
  impl_->public_status.last_result = "cached";
  if (impl_->active_slot != selected) {
    return persist_slot(impl_->active_slot,
                        impl_->active_metadata.content_digest);
  }
  return ESP_OK;
}

esp_err_t PetStore::begin(const PetUploadBegin& request,
                          PetUploadStatus* output) {
  if (impl_ == nullptr) return ESP_ERR_INVALID_STATE;
  Lock lock(impl_->command_mutex);
  if (!lock.locked()) return ESP_ERR_TIMEOUT;
  impl_->command_begin = request;
  const esp_err_t result = submit_upload_command(kCommandBegin);
  if (result == ESP_OK && output != nullptr) {
    *output = impl_->command_status;
  }
  return result;
}

esp_err_t PetStore::do_begin(const PetUploadBegin& request,
                             PetUploadStatus* output) {
  if (impl_ == nullptr) return ESP_ERR_INVALID_STATE;
  Lock lock(impl_->mutex);
  if (!lock.locked()) return ESP_ERR_TIMEOUT;
  if (impl_->public_status.transaction.active) {
    return ESP_ERR_INVALID_STATE;
  }
  if (request.pet_id.empty() || request.pet_id.size() > 64 ||
      request.format_version != 1 || request.length == 0 ||
      request.length > kPetBundleMaximumBytes) {
    return ESP_ERR_INVALID_ARG;
  }
  const PetSlot target = inactive_pet_slot(impl_->active_slot);
  const std::size_t erase_length =
      (request.length + kEraseBlock - 1) & ~(kEraseBlock - 1);
  const esp_err_t erased = esp_partition_erase_range(
      impl_->partition, slot_offset(target), erase_length);
  if (erased != ESP_OK) return erased;
  char transaction[17]{};
  std::snprintf(transaction, sizeof(transaction), "%08lx%08lx",
                static_cast<unsigned long>(esp_random()),
                static_cast<unsigned long>(esp_random()));
  impl_->transaction_id = transaction;
  impl_->upload_begin = request;
  impl_->received = 0;
  impl_->previous_length = 0;
  impl_->public_status.transaction = {
      .active = true,
      .transaction_id = impl_->transaction_id,
      .received = 0,
      .expected = request.length,
  };
  impl_->public_status.last_result = "uploading";
  if (output != nullptr) *output = impl_->public_status.transaction;
  return ESP_OK;
}

esp_err_t PetStore::append(
    std::string_view transaction_id, std::size_t offset,
    std::span<const uint8_t> chunk,
    std::span<const uint8_t, 32> chunk_digest) {
  if (impl_ == nullptr) return ESP_ERR_INVALID_STATE;
  if (chunk.empty() || chunk.size() > 8192) return ESP_ERR_INVALID_ARG;
  auto owned =
      std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[chunk.size()]);
  if (owned == nullptr) return ESP_ERR_NO_MEM;
  std::copy(chunk.begin(), chunk.end(), owned.get());
  return append_owned(transaction_id, offset, std::move(owned), chunk.size(),
                      chunk_digest);
}

esp_err_t PetStore::append_owned(
    std::string_view transaction_id, std::size_t offset,
    std::unique_ptr<uint8_t[]> chunk, std::size_t chunk_size,
    std::span<const uint8_t, 32> chunk_digest) {
  if (impl_ == nullptr) return ESP_ERR_INVALID_STATE;
  if (chunk == nullptr || chunk_size == 0 || chunk_size > 8192) {
    return ESP_ERR_INVALID_ARG;
  }
  Lock lock(impl_->command_mutex);
  if (!lock.locked()) return ESP_ERR_TIMEOUT;
  impl_->command_transaction.assign(transaction_id);
  impl_->command_offset = offset;
  impl_->command_chunk = std::move(chunk);
  impl_->command_chunk_size = chunk_size;
  std::copy(chunk_digest.begin(), chunk_digest.end(),
            impl_->command_digest.begin());
  return submit_upload_command(kCommandAppend);
}

esp_err_t PetStore::do_append(
    std::string_view transaction_id, std::size_t offset,
    std::span<const uint8_t> chunk,
    std::span<const uint8_t, 32> chunk_digest) {
  if (impl_ == nullptr) return ESP_ERR_INVALID_STATE;
  Lock lock(impl_->mutex);
  if (!lock.locked()) return ESP_ERR_TIMEOUT;
  if (!impl_->public_status.transaction.active ||
      transaction_id != impl_->transaction_id) {
    return ESP_ERR_INVALID_STATE;
  }
  if (chunk.empty() || chunk.size() > 8192 ||
      offset > impl_->upload_begin.length ||
      chunk.size() > impl_->upload_begin.length - offset) {
    return ESP_ERR_INVALID_ARG;
  }
  std::array<uint8_t, 32> actual{};
  mbedtls_sha256(chunk.data(), chunk.size(), actual.data(), 0);
  if (!std::equal(actual.begin(), actual.end(), chunk_digest.begin())) {
    return ESP_ERR_INVALID_CRC;
  }
  if (offset == impl_->previous_offset &&
      chunk.size() == impl_->previous_length &&
      actual == impl_->previous_digest) {
    return ESP_OK;
  }
  if (offset != impl_->received) return ESP_ERR_INVALID_SIZE;
  const PetSlot target = inactive_pet_slot(impl_->active_slot);
  const esp_err_t written = esp_partition_write(
      impl_->partition, slot_offset(target) + offset, chunk.data(),
      chunk.size());
  if (written != ESP_OK) return written;
  impl_->previous_offset = offset;
  impl_->previous_length = chunk.size();
  impl_->previous_digest = actual;
  impl_->received += chunk.size();
  impl_->public_status.transaction.received = impl_->received;
  return ESP_OK;
}

esp_err_t PetStore::commit(std::string_view transaction_id) {
  if (impl_ == nullptr) return ESP_ERR_INVALID_STATE;
  Lock lock(impl_->command_mutex);
  if (!lock.locked()) return ESP_ERR_TIMEOUT;
  impl_->command_transaction.assign(transaction_id);
  return submit_upload_command(kCommandCommit);
}

esp_err_t PetStore::do_commit(std::string_view transaction_id) {
  if (impl_ == nullptr) return ESP_ERR_INVALID_STATE;
  Lock lock(impl_->mutex);
  if (!lock.locked()) return ESP_ERR_TIMEOUT;
  if (!impl_->public_status.transaction.active ||
      transaction_id != impl_->transaction_id ||
      impl_->received != impl_->upload_begin.length) {
    return ESP_ERR_INVALID_STATE;
  }
  const PetSlot next = inactive_pet_slot(impl_->active_slot);
  PartitionSource source(impl_->partition, slot_offset(next),
                         impl_->received);
  PetBundleMetadata metadata;
  const PetBundleError valid = validate_pet_bundle(
      source, impl_->upload_begin.upload_digest, &metadata);
  if (valid != PetBundleError::none ||
      metadata.pet_id != impl_->upload_begin.pet_id ||
      metadata.schema_version != impl_->upload_begin.format_version) {
    impl_->public_status.transaction = {};
    impl_->transaction_id.clear();
    impl_->public_status.last_result = "invalid_bundle";
    return ESP_ERR_INVALID_CRC;
  }
  const esp_err_t persisted = persist_slot(next, metadata.content_digest);
  if (persisted != ESP_OK) {
    impl_->public_status.transaction = {};
    impl_->transaction_id.clear();
    impl_->public_status.last_result = "nvs_failed";
    return persisted;
  }
  impl_->active_slot = next;
  impl_->active_metadata = metadata;
  impl_->active_size = source.size();
  impl_->public_status.pet_id = metadata.pet_id;
  impl_->public_status.digest = hex(metadata.content_digest);
  impl_->public_status.format_version = metadata.schema_version;
  impl_->public_status.storage_used = impl_->active_size;
  impl_->public_status.transaction = {};
  impl_->transaction_id.clear();
  impl_->public_status.last_result = "ok";
  return ESP_OK;
}

esp_err_t PetStore::submit_upload_command(uint8_t command) {
  if (impl_->command_active.load()) return ESP_ERR_INVALID_STATE;
  while (xSemaphoreTake(impl_->completion, 0) == pdTRUE) {
  }
  impl_->command_active.store(true);
  if (xQueueSend(impl_->command_queue, &command, 0) != pdTRUE) {
    impl_->command_active.store(false);
    return ESP_ERR_TIMEOUT;
  }
  if (xSemaphoreTake(impl_->completion, pdMS_TO_TICKS(30000)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }
  impl_->command_active.store(false);
  return impl_->command_result;
}

void PetStore::upload_task(void* context) {
  auto* store = static_cast<PetStore*>(context);
  uint8_t command = 0;
  while (true) {
    if (xQueueReceive(store->impl_->command_queue, &command,
                      portMAX_DELAY) != pdTRUE) {
      continue;
    }
    esp_err_t result = ESP_ERR_INVALID_ARG;
    switch (command) {
      case kCommandInitialize:
        result = store->do_initialize();
        break;
      case kCommandBegin:
        result = store->do_begin(store->impl_->command_begin,
                                 &store->impl_->command_status);
        break;
      case kCommandAppend:
        result = store->do_append(
            store->impl_->command_transaction,
            store->impl_->command_offset,
            std::span<const uint8_t>(
                store->impl_->command_chunk.get(),
                store->impl_->command_chunk_size),
            store->impl_->command_digest);
        break;
      case kCommandCommit:
        result = store->do_commit(store->impl_->command_transaction);
        break;
      default:
        break;
    }
    store->impl_->command_chunk.reset();
    store->impl_->command_chunk_size = 0;
    store->impl_->command_result = result;
    xSemaphoreGive(store->impl_->completion);
    store->impl_->command_active.store(false);
  }
}

PetStoreStatus PetStore::status() const {
  if (impl_ == nullptr) return {};
  Lock lock(impl_->mutex);
  return lock.locked() ? impl_->public_status : PetStoreStatus{};
}

bool PetStore::decode(
    PetState state, uint8_t frame,
    std::span<uint16_t, kPetFramePixels> output) {
  if (impl_ == nullptr) return false;
  Lock lock(impl_->mutex);
  if (!lock.locked() || impl_->active_slot == PetSlot::none) return false;
  PartitionSource source(impl_->partition, slot_offset(impl_->active_slot),
                         impl_->active_size);
  return source.valid() &&
         decode_pet_frame(source, impl_->active_metadata, state, frame,
                          output) == PetBundleError::none;
}

#endif
