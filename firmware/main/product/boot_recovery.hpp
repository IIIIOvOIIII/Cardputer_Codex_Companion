#pragma once

#include <array>
#include <cstdint>
#include <string_view>

inline constexpr uint64_t kBootRecoveryHoldMs = 600;
inline constexpr uint8_t kBootRecoveryBackspaceKey = 13;
inline constexpr uint8_t kBootRecoveryYesKey = 20;
inline constexpr uint8_t kBootRecoveryNoKey = 50;

inline constexpr std::array<std::string_view, 5>
    kCompanionResetNamespaces{
        "wifi",
        "product",
        "product_tls",
        "phase0_id",
        "nimble_bond",
    };

enum class BootRecoveryChoice : uint8_t {
  none,
  erase,
  cancel,
};

enum class BootRecoveryResetStage : uint8_t {
  none,
  wifi,
  product,
  product_tls,
  phase0_id,
  nimble_bond,
  storage,
  complete,
};

struct BootRecoveryResetResult {
  bool success = false;
  BootRecoveryResetStage stage = BootRecoveryResetStage::none;
};

enum class BootRecoveryStorageResult : uint8_t {
  erased,
  absent,
  failed,
};

class BootRecoveryHoldDetector {
 public:
  bool update(bool backspace_pressed, uint64_t now_ms);

 private:
  bool tracking_ = false;
  uint64_t pressed_at_ms_ = 0;
};

class BootRecoveryResetBackend {
 public:
  virtual ~BootRecoveryResetBackend() = default;
  virtual bool erase_namespace(std::string_view name) = 0;
  virtual BootRecoveryStorageResult erase_storage() = 0;
};

BootRecoveryChoice boot_recovery_choice(bool yes_pressed, bool no_pressed);
BootRecoveryResetResult reset_companion_data(
    BootRecoveryResetBackend& backend
);
std::string_view boot_recovery_stage_name(BootRecoveryResetStage stage);

#ifdef ESP_PLATFORM
#include "esp_err.h"

esp_err_t product_boot_recovery();
#endif
