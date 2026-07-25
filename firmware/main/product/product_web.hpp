#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "product/profile.hpp"
#include "product/profile_catalog.hpp"
#include "product/pet_store.hpp"
#include "product/product_types.hpp"

enum class ProductHttpMethod : uint8_t { get, post, put, delete_ };
inline constexpr bool kProductWebUsesTls = true;
inline constexpr std::size_t kProductWebPinLength = 8;

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

struct ProductWebRoute {
  ProductHttpMethod method;
  std::string_view path;
  bool requires_pairing;
};

inline constexpr std::array<ProductWebRoute, 16> kProductWebRoutes{{
    {ProductHttpMethod::get, "/", false},
    {ProductHttpMethod::get, "/api/v1/status", false},
    {ProductHttpMethod::get, "/api/v1/profile", true},
    {ProductHttpMethod::put, "/api/v1/profile", true},
    {ProductHttpMethod::delete_, "/api/v1/profile", true},
    {ProductHttpMethod::get, "/api/v1/profiles", true},
    {ProductHttpMethod::post, "/api/v1/profiles", true},
    {ProductHttpMethod::post, "/api/v1/profile/activate", true},
    {ProductHttpMethod::post, "/api/v1/wifi", true},
    {ProductHttpMethod::post, "/api/v1/pin", true},
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

esp_err_t product_web_start();
const char* product_web_pairing_code();
void product_web_set_status(ServiceState ble, ServiceState wifi,
                            ServiceState companion);
void product_web_set_companion_snapshot_handler(
    ProductCompanionSnapshotHandler handler);
void product_web_set_companion_heartbeat_handler(
    ProductCompanionHeartbeatHandler handler);
void product_web_set_pet_store(PetStore* store);
void product_web_set_profile_catalog(ProfileCatalogStore* catalog);
bool product_web_action(uint8_t layer, uint8_t physical_key, KeyAction* action);
bool product_web_profile_name(char* output, std::size_t output_size);
void product_web_queue_codex_action(CodexAction action);
#endif
