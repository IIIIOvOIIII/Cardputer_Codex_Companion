#include "product/boot_recovery.hpp"

#include <array>

bool BootRecoveryHoldDetector::update(
    bool backspace_pressed,
    uint64_t now_ms
) {
  if (!backspace_pressed) {
    tracking_ = false;
    pressed_at_ms_ = 0;
    return false;
  }
  if (!tracking_) {
    tracking_ = true;
    pressed_at_ms_ = now_ms;
    return false;
  }
  return now_ms >= pressed_at_ms_ &&
         now_ms - pressed_at_ms_ >= kBootRecoveryHoldMs;
}

BootRecoveryChoice boot_recovery_choice(
    bool yes_pressed,
    bool no_pressed
) {
  if (yes_pressed == no_pressed) return BootRecoveryChoice::none;
  return yes_pressed ? BootRecoveryChoice::erase
                     : BootRecoveryChoice::cancel;
}

BootRecoveryResetResult reset_companion_data(
    BootRecoveryResetBackend& backend
) {
  constexpr std::array stages{
      BootRecoveryResetStage::wifi,
      BootRecoveryResetStage::product,
      BootRecoveryResetStage::product_tls,
      BootRecoveryResetStage::phase0_id,
      BootRecoveryResetStage::nimble_bond,
  };
  for (std::size_t index = 0;
       index < kCompanionResetNamespaces.size(); ++index) {
    if (!backend.erase_namespace(kCompanionResetNamespaces[index])) {
      return {
          .success = false,
          .stage = stages[index],
      };
    }
  }
  if (backend.erase_storage() == BootRecoveryStorageResult::failed) {
    return {
        .success = false,
        .stage = BootRecoveryResetStage::storage,
    };
  }
  return {
      .success = true,
      .stage = BootRecoveryResetStage::complete,
  };
}

std::string_view boot_recovery_stage_name(BootRecoveryResetStage stage) {
  switch (stage) {
    case BootRecoveryResetStage::none: return "NONE";
    case BootRecoveryResetStage::wifi: return "NVS WIFI";
    case BootRecoveryResetStage::product: return "NVS PRODUCT";
    case BootRecoveryResetStage::product_tls: return "NVS TLS";
    case BootRecoveryResetStage::phase0_id: return "NVS DEVICE";
    case BootRecoveryResetStage::nimble_bond: return "NVS BLE";
    case BootRecoveryResetStage::storage: return "STORAGE";
    case BootRecoveryResetStage::complete: return "COMPLETE";
  }
  return "UNKNOWN";
}

#ifdef ESP_PLATFORM

#include <algorithm>

#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "product/display.hpp"
#include "product/keyboard_matrix.hpp"
#include "product/storage_partition_label.hpp"

namespace {
constexpr std::size_t kFlashSectorBytes = 0x1000;
constexpr uint32_t kRecoveryPollMs = 20;

class EspBootRecoveryResetBackend final : public BootRecoveryResetBackend {
 public:
  bool erase_namespace(std::string_view name) override {
    nvs_handle_t handle = 0;
    const std::array<char, 16> namespace_name =
        terminated_namespace(name);
    esp_err_t result =
        nvs_open(namespace_name.data(), NVS_READWRITE, &handle);
    if (result != ESP_OK) return false;
    result = nvs_erase_all(handle);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result == ESP_OK;
  }

  BootRecoveryStorageResult erase_storage() override {
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
        kProductStoragePartitionLabel);
    if (partition == nullptr) {
      return BootRecoveryStorageResult::absent;
    }
    if (partition->size % kFlashSectorBytes != 0) {
      return BootRecoveryStorageResult::failed;
    }
    for (std::size_t offset = 0; offset < partition->size;
         offset += kFlashSectorBytes) {
      if (esp_partition_erase_range(
              partition, offset, kFlashSectorBytes) != ESP_OK) {
        return BootRecoveryStorageResult::failed;
      }
      vTaskDelay(1);
    }
    return BootRecoveryStorageResult::erased;
  }

 private:
  static std::array<char, 16> terminated_namespace(
      std::string_view name
  ) {
    std::array<char, 16> output{};
    std::copy_n(
        name.begin(), std::min(name.size(), output.size() - 1),
        output.begin());
    return output;
  }
};

uint64_t now_ms() {
  return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}

void wait_for_recovery_keys_released() {
  while (keyboard_matrix_key_pressed(kBootRecoveryBackspaceKey) ||
         keyboard_matrix_key_pressed(kBootRecoveryYesKey) ||
         keyboard_matrix_key_pressed(kBootRecoveryNoKey)) {
    vTaskDelay(pdMS_TO_TICKS(kRecoveryPollMs));
  }
}
}  // namespace

esp_err_t product_boot_recovery() {
  if (!keyboard_matrix_key_pressed(kBootRecoveryBackspaceKey)) {
    return ESP_OK;
  }

  BootRecoveryHoldDetector hold;
  while (!hold.update(
      keyboard_matrix_key_pressed(kBootRecoveryBackspaceKey), now_ms())) {
    if (!keyboard_matrix_key_pressed(kBootRecoveryBackspaceKey)) {
      return ESP_OK;
    }
    vTaskDelay(pdMS_TO_TICKS(kRecoveryPollMs));
  }

  display_render_boot_recovery_prompt();
  wait_for_recovery_keys_released();
  while (true) {
    const BootRecoveryChoice choice = boot_recovery_choice(
        keyboard_matrix_key_pressed(kBootRecoveryYesKey),
        keyboard_matrix_key_pressed(kBootRecoveryNoKey));
    if (choice == BootRecoveryChoice::cancel) {
      return ESP_OK;
    }
    if (choice == BootRecoveryChoice::erase) {
      EspBootRecoveryResetBackend backend;
      const BootRecoveryResetResult result =
          reset_companion_data(backend);
      display_render_boot_recovery_result(
          result.success, boot_recovery_stage_name(result.stage));
      if (result.success) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
      }
      while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(kRecoveryPollMs));
  }
}

#endif
