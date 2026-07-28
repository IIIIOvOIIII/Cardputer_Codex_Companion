#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "product/profile.hpp"
#include "product/profile_catalog.hpp"
#include "product/onboarding.hpp"
#include "product/pet_store.hpp"
#include "product/product_types.hpp"
#include "product/storage_compatibility.hpp"

enum class ProductHttpMethod : uint8_t { get, post, put, delete_ };
inline constexpr bool kProductWebUsesTls = true;
inline constexpr std::size_t kProductWebTaskStackBytes = 4864;
inline constexpr uint64_t kProductWebTlsCleanupWindowUs = 5'000'000;
inline constexpr std::size_t kProductWebPinLength = 8;
inline constexpr std::string_view kProductPairingHeader =
    "X-Cardputer-Pairing";
inline constexpr std::size_t kProductPetChunkMaximumBytes = 8192;

constexpr std::string_view product_web_resource_scenario(
    std::size_t active_tls_sessions) {
  return active_tls_sessions == 0 ? "steady" : "tls_burst";
}

constexpr bool product_web_tls_resource_window_active(
    std::size_t active_tls_sessions,
    uint64_t now_us,
    uint64_t cleanup_until_us) {
  return active_tls_sessions > 0 || now_us < cleanup_until_us;
}

constexpr bool product_web_pin_is_valid(std::string_view pin) {
  if (pin.size() != kProductWebPinLength) return false;
  for (char digit : pin) {
    if (digit < '0' || digit > '9') return false;
  }
  return true;
}

enum class ProductWebPinLoadAction : uint8_t {
  use_stored,
  generate_and_persist,
  generate_ephemeral,
};

constexpr ProductWebPinLoadAction product_web_pin_load_action(
    bool nvs_open_ok,
    bool stored_found,
    bool stored_valid) {
  if (!nvs_open_ok) return ProductWebPinLoadAction::generate_ephemeral;
  return stored_found && stored_valid
             ? ProductWebPinLoadAction::use_stored
             : ProductWebPinLoadAction::generate_and_persist;
}

constexpr bool product_web_binding_uses_sparse_null(ActionKind kind) {
  return kind == ActionKind::passthrough;
}

enum class ProductWebProfileActivation : uint8_t {
  keep_active,
  replace_active,
};

constexpr ProductWebProfileActivation product_web_profile_activation(
    bool persistence_succeeded) {
  return persistence_succeeded
             ? ProductWebProfileActivation::replace_active
             : ProductWebProfileActivation::keep_active;
}

constexpr bool product_web_companion_needs_snapshot(ServiceState state) {
  return state != ServiceState::ok;
}

enum class ProductWebMicrophoneState : uint8_t {
  unavailable,
  ready,
  live24,
  live16,
  error,
};

enum class ProductWebMicrophoneError : uint8_t {
  none,
  mac_not_ready,
  mic_init_failed,
  mic_no_signal,
  ble_audio_busy,
  audio_driver_mismatch,
};

struct ProductWebMicrophoneStatus {
  ProductWebMicrophoneState state = ProductWebMicrophoneState::unavailable;
  uint32_t sample_rate_hz = 0;
  uint8_t drop_percent = 0;
  ProductWebMicrophoneError last_error = ProductWebMicrophoneError::none;
};

constexpr std::string_view product_web_microphone_state_name(
    ProductWebMicrophoneState state) {
  switch (state) {
    case ProductWebMicrophoneState::unavailable: return "UNAVAILABLE";
    case ProductWebMicrophoneState::ready: return "READY";
    case ProductWebMicrophoneState::live24: return "LIVE24";
    case ProductWebMicrophoneState::live16: return "LIVE16";
    case ProductWebMicrophoneState::error: return "ERROR";
  }
  return "ERROR";
}

constexpr std::string_view product_web_microphone_error_name(
    ProductWebMicrophoneError error) {
  switch (error) {
    case ProductWebMicrophoneError::none: return "NONE";
    case ProductWebMicrophoneError::mac_not_ready: return "MAC_NOT_READY";
    case ProductWebMicrophoneError::mic_init_failed: return "MIC_INIT_FAILED";
    case ProductWebMicrophoneError::mic_no_signal: return "MIC_NO_SIGNAL";
    case ProductWebMicrophoneError::ble_audio_busy: return "BLE_AUDIO_BUSY";
    case ProductWebMicrophoneError::audio_driver_mismatch:
      return "AUDIO_DRIVER_MISMATCH";
  }
  return "MIC_INIT_FAILED";
}

inline std::string product_web_microphone_json(
    const ProductWebMicrophoneStatus& status) {
  char json[160]{};
  const std::string_view state =
      product_web_microphone_state_name(status.state);
  const std::string_view error =
      product_web_microphone_error_name(status.last_error);
  std::snprintf(
      json, sizeof(json),
      "{\"state\":\"%.*s\",\"sample_rate_hz\":%lu,"
      "\"drop_percent\":%u,\"last_error\":\"%.*s\"}",
      static_cast<int>(state.size()), state.data(),
      static_cast<unsigned long>(status.sample_rate_hz),
      static_cast<unsigned>(status.drop_percent),
      static_cast<int>(error.size()), error.data());
  return json;
}

constexpr std::string_view product_web_storage_state_name(
    StorageCompatibilityState state) {
  switch (state) {
    case StorageCompatibilityState::ready: return "READY";
    case StorageCompatibilityState::missing: return "MISSING";
    case StorageCompatibilityState::wrong_type: return "WRONG_TYPE";
    case StorageCompatibilityState::too_small: return "TOO_SMALL";
  }
  return "MISSING";
}

inline std::string product_web_storage_json(
    const StorageCompatibility& status) {
  char json[96]{};
  const std::string_view state =
      product_web_storage_state_name(status.state);
  std::snprintf(
      json, sizeof(json),
      "{\"state\":\"%.*s\",\"size_bytes\":%lu}",
      static_cast<int>(state.size()), state.data(),
      static_cast<unsigned long>(status.size_bytes));
  return json;
}

inline std::string product_web_partition_error_json(
    const StorageCompatibility& status) {
  char json[96]{};
  const std::string_view reason =
      storage_compatibility_name(status.state);
  std::snprintf(
      json, sizeof(json),
      "{\"error\":\"partition_incompatible\",\"reason\":\"%.*s\"}",
      static_cast<int>(reason.size()), reason.data());
  return json;
}

constexpr std::string_view product_web_onboarding_step_name(
    OnboardingStep step
) {
  switch (step) {
    case OnboardingStep::wifi_scan: return "wifi_scan";
    case OnboardingStep::wifi_select: return "wifi_select";
    case OnboardingStep::wifi_password: return "wifi_password";
    case OnboardingStep::wifi_connect_verify: return "wifi_connect";
    case OnboardingStep::ble_pair_guide: return "ble_pair";
    case OnboardingStep::agent_install_guide: return "agent_install";
    case OnboardingStep::complete: return "complete";
  }
  return "wifi_scan";
}

constexpr bool product_web_configuration_available(OnboardingStep step) {
  return step == OnboardingStep::complete;
}

constexpr bool product_web_restart_confirmation_valid(
    std::string_view confirmation
) {
  return confirmation == "RUN_SETUP_AGAIN";
}

inline std::string product_web_setup_json(OnboardingStep step) {
  char json[192]{};
  const std::string_view name = product_web_onboarding_step_name(step);
  std::snprintf(
      json, sizeof(json),
      "{\"product\":\"Cardputer Codex Companion\","
      "\"version\":\"%.*s\",\"complete\":%s,\"step\":\"%.*s\"}",
      static_cast<int>(kProductVersion.size()), kProductVersion.data(),
      step == OnboardingStep::complete ? "true" : "false",
      static_cast<int>(name.size()), name.data());
  return json;
}

struct ProductWebRoute {
  ProductHttpMethod method;
  std::string_view path;
  bool requires_pairing;
};

inline constexpr std::array<ProductWebRoute, 18> kProductWebRoutes{{
    {ProductHttpMethod::get, "/", false},
    {ProductHttpMethod::get, "/api/v1/setup", false},
    {ProductHttpMethod::get, "/api/v1/status", true},
    {ProductHttpMethod::get, "/api/v1/profile", true},
    {ProductHttpMethod::put, "/api/v1/profile", true},
    {ProductHttpMethod::delete_, "/api/v1/profile", true},
    {ProductHttpMethod::get, "/api/v1/profiles", true},
    {ProductHttpMethod::post, "/api/v1/profiles", true},
    {ProductHttpMethod::post, "/api/v1/profile/activate", true},
    {ProductHttpMethod::post, "/api/v1/wifi", true},
    {ProductHttpMethod::post, "/api/v1/pin", true},
    {ProductHttpMethod::post, "/api/v1/setup/restart", true},
    {ProductHttpMethod::post, "/api/v1/companion/status", true},
    {ProductHttpMethod::get, "/api/v1/companion/action", true},
    {ProductHttpMethod::post, "/api/v1/companion/pet/begin", true},
    {ProductHttpMethod::put, "/api/v1/companion/pet/chunk", true},
    {ProductHttpMethod::post, "/api/v1/companion/pet/commit", true},
    {ProductHttpMethod::get, "/api/v1/companion/pet", true},
}};

#ifdef ESP_PLATFORM
#include "esp_err.h"

using ProductCompanionSnapshotHandler = void (*)(std::string_view json);
using ProductCompanionHeartbeatHandler = void (*)();
using ProductOnboardingRestartHandler = bool (*)();

esp_err_t product_web_start();
bool product_web_tls_resource_window_active();
const char* product_web_pairing_code();
void product_web_set_status(ServiceState ble, ServiceState wifi,
                            ServiceState companion);
void product_web_set_onboarding(OnboardingStep step);
void product_web_set_microphone(ProductWebMicrophoneStatus status);
void product_web_set_storage_compatibility(
    StorageCompatibility compatibility);
void product_web_set_companion_snapshot_handler(
    ProductCompanionSnapshotHandler handler);
void product_web_set_companion_heartbeat_handler(
    ProductCompanionHeartbeatHandler handler);
void product_web_set_onboarding_restart_handler(
    ProductOnboardingRestartHandler handler);
void product_web_set_pet_store(PetStore* store);
void product_web_set_profile_catalog(ProfileCatalogStore* catalog);
esp_err_t product_web_prepare_profile_catalog(ProfileCatalogStore* catalog);
bool product_web_cycle_profile(bool forward);
bool product_web_activate_profile(std::string_view id);
bool product_web_profile_summaries(
    std::span<ProfileSummary> output,
    std::size_t* count
);
esp_err_t product_web_rotate_pin(std::string_view pin);
bool product_web_action(uint8_t layer, uint8_t physical_key, KeyAction* action);
bool product_web_profile_name(char* output, std::size_t output_size);
void product_web_queue_codex_action(CodexAction action);
#endif
