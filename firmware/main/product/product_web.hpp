#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "product/profile.hpp"
#include "product/product_types.hpp"

enum class ProductHttpMethod : uint8_t { get, post, put };
inline constexpr bool kProductWebUsesTls = true;

struct ProductWebRoute {
  ProductHttpMethod method;
  std::string_view path;
  bool requires_pairing;
};

inline constexpr std::array<ProductWebRoute, 7> kProductWebRoutes{{
    {ProductHttpMethod::get, "/", false},
    {ProductHttpMethod::get, "/api/v1/status", false},
    {ProductHttpMethod::get, "/api/v1/profile", true},
    {ProductHttpMethod::put, "/api/v1/profile", true},
    {ProductHttpMethod::post, "/api/v1/wifi", true},
    {ProductHttpMethod::post, "/api/v1/companion/status", true},
    {ProductHttpMethod::get, "/api/v1/companion/action", true},
}};

#ifdef ESP_PLATFORM
#include "esp_err.h"

using ProductCompanionSnapshotHandler = void (*)(std::string_view json);

esp_err_t product_web_start();
const char* product_web_pairing_code();
void product_web_set_status(ServiceState ble, ServiceState wifi,
                            ServiceState companion);
void product_web_set_companion_snapshot_handler(
    ProductCompanionSnapshotHandler handler);
bool product_web_action(uint8_t layer, uint8_t physical_key, KeyAction* action);
bool product_web_profile_name(char* output, std::size_t output_size);
void product_web_queue_codex_action(CodexAction action);
#endif
