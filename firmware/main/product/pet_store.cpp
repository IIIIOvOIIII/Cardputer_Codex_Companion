#include "product/pet_store.hpp"

#ifdef ESP_PLATFORM

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "esp_random.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

namespace {
constexpr char kPartition[] = "storage";
constexpr char kMount[] = "/pet";
constexpr char kSlotA[] = "/pet/slot-a.ccpt";
constexpr char kSlotB[] = "/pet/slot-b.ccpt";
constexpr char kUpload[] = "/pet/upload.tmp";
constexpr char kNamespace[] = "product";
constexpr char kSlotKey[] = "pet_slot";
constexpr char kDigestKey[] = "pet_digest";
constexpr uint8_t kCommandBegin = 1;
constexpr uint8_t kCommandAppend = 2;
constexpr uint8_t kCommandCommit = 3;

const char* slot_path(PetSlot slot) {
  return slot == PetSlot::a ? kSlotA : kSlotB;
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

class FileSource final : public PetByteSource {
 public:
  explicit FileSource(const char* path) : file_(std::fopen(path, "rb")) {
    if (file_ != nullptr) {
      std::fseek(file_, 0, SEEK_END);
      const long size = std::ftell(file_);
      size_ = size > 0 ? static_cast<std::size_t>(size) : 0;
      std::fseek(file_, 0, SEEK_SET);
    }
  }
  ~FileSource() override {
    if (file_ != nullptr) std::fclose(file_);
  }
  bool valid() const { return file_ != nullptr; }
  std::size_t size() const override { return size_; }
  bool read(std::size_t offset, std::span<uint8_t> output) const override {
    if (file_ == nullptr || offset > size_ || output.size() > size_ - offset) {
      return false;
    }
    if (std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) {
      return false;
    }
    return std::fread(output.data(), 1, output.size(), file_) == output.size();
  }

 private:
  mutable FILE* file_ = nullptr;
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

bool validate_slot(const char* path, PetBundleMetadata* metadata) {
  FileSource source(path);
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
  std::array<StackType_t, 4096> upload_task_stack{};
  TaskHandle_t upload_task_handle = nullptr;
  std::atomic<bool> command_active{false};
  PetUploadBegin command_begin;
  PetUploadStatus command_status;
  std::string command_transaction;
  std::size_t command_offset = 0;
  std::vector<uint8_t> command_chunk;
  std::array<uint8_t, 32> command_digest{};
  esp_err_t command_result = ESP_FAIL;
  PetSlot active_slot = PetSlot::none;
  PetBundleMetadata active_metadata;
  std::size_t active_size = 0;
  PetStoreStatus public_status;
  FILE* upload_file = nullptr;
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
  const esp_vfs_spiffs_conf_t config{
      .base_path = kMount,
      .partition_label = kPartition,
      .max_files = 5,
      .format_if_mount_failed = true,
  };
  esp_err_t result = esp_vfs_spiffs_register(&config);
  if (result != ESP_OK) return result;

  uint8_t selected_raw = static_cast<uint8_t>(PetSlot::none);
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) == ESP_OK) {
    nvs_get_u8(handle, kSlotKey, &selected_raw);
    nvs_close(handle);
  }
  PetBundleMetadata slot_a;
  PetBundleMetadata slot_b;
  const bool a_valid = validate_slot(kSlotA, &slot_a);
  const bool b_valid = validate_slot(kSlotB, &slot_b);
  const PetSlot selected =
      selected_raw <= 1 ? static_cast<PetSlot>(selected_raw) : PetSlot::none;
  impl->active_slot = select_boot_pet_slot(selected, a_valid, b_valid);
  if (impl->active_slot != PetSlot::none) {
    impl->active_metadata =
        impl->active_slot == PetSlot::a ? slot_a : slot_b;
    FileSource active(slot_path(impl->active_slot));
    impl->active_size = active.size();
    impl->public_status.pet_id = impl->active_metadata.pet_id;
    impl->public_status.digest = hex(impl->active_metadata.content_digest);
    impl->public_status.format_version =
        impl->active_metadata.schema_version;
    impl->public_status.storage_used = impl->active_size;
    impl->public_status.last_result = "cached";
    if (impl->active_slot != selected) {
      persist_slot(impl->active_slot, impl->active_metadata.content_digest);
    }
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
  if (impl_->upload_file != nullptr) return ESP_ERR_INVALID_STATE;
  if (request.pet_id.empty() || request.pet_id.size() > 64 ||
      request.format_version != 1 || request.length == 0 ||
      request.length > kPetBundleMaximumBytes) {
    return ESP_ERR_INVALID_ARG;
  }
  std::remove(slot_path(inactive_pet_slot(impl_->active_slot)));
  std::remove(kUpload);
  impl_->upload_file = std::fopen(kUpload, "wb");
  if (impl_->upload_file == nullptr) return ESP_FAIL;
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
  if (chunk.size() > 8192) return ESP_ERR_INVALID_ARG;
  Lock lock(impl_->command_mutex);
  if (!lock.locked()) return ESP_ERR_TIMEOUT;
  impl_->command_transaction.assign(transaction_id);
  impl_->command_offset = offset;
  impl_->command_chunk.assign(chunk.begin(), chunk.end());
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
  if (impl_->upload_file == nullptr ||
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
  if (std::fwrite(chunk.data(), 1, chunk.size(), impl_->upload_file) !=
      chunk.size()) {
    return ESP_FAIL;
  }
  std::fflush(impl_->upload_file);
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
  if (impl_->upload_file == nullptr ||
      transaction_id != impl_->transaction_id ||
      impl_->received != impl_->upload_begin.length) {
    return ESP_ERR_INVALID_STATE;
  }
  std::fclose(impl_->upload_file);
  impl_->upload_file = nullptr;
  FileSource source(kUpload);
  PetBundleMetadata metadata;
  const PetBundleError valid = validate_pet_bundle(
      source, impl_->upload_begin.upload_digest, &metadata);
  if (valid != PetBundleError::none ||
      metadata.pet_id != impl_->upload_begin.pet_id ||
      metadata.schema_version != impl_->upload_begin.format_version) {
    std::remove(kUpload);
    impl_->public_status.transaction = {};
    impl_->public_status.last_result = "invalid_bundle";
    return ESP_ERR_INVALID_CRC;
  }
  const PetSlot next = inactive_pet_slot(impl_->active_slot);
  const char* next_path = slot_path(next);
  std::remove(next_path);
  if (std::rename(kUpload, next_path) != 0) {
    impl_->public_status.transaction = {};
    impl_->public_status.last_result = "rename_failed";
    return ESP_FAIL;
  }
  const esp_err_t persisted = persist_slot(next, metadata.content_digest);
  if (persisted != ESP_OK) {
    impl_->public_status.transaction = {};
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
      case kCommandBegin:
        result = store->do_begin(store->impl_->command_begin,
                                 &store->impl_->command_status);
        break;
      case kCommandAppend:
        result = store->do_append(
            store->impl_->command_transaction,
            store->impl_->command_offset,
            store->impl_->command_chunk,
            store->impl_->command_digest);
        break;
      case kCommandCommit:
        result = store->do_commit(store->impl_->command_transaction);
        break;
      default:
        break;
    }
    std::vector<uint8_t>().swap(store->impl_->command_chunk);
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
  FileSource source(slot_path(impl_->active_slot));
  return source.valid() &&
         decode_pet_frame(source, impl_->active_metadata, state, frame,
                          output) == PetBundleError::none;
}

#endif
