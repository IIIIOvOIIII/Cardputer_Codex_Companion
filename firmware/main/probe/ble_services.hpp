#pragma once

#include <array>
#include <cstdint>
#include <span>

using DeviceId = std::array<uint8_t, 16>;
using Uuid128 = std::array<uint8_t, 16>;

enum class BleStack { nimble };
enum class HidGattOwner { esp_hid };

enum class BleInitStep {
  esp_hid_gap_init,
  esp_hid_ble_gap_adv_init,
  esp_hidd_dev_init,
  count_custom_service,
  add_custom_service,
  ble_store_config_init,
  esp_nimble_enable,
};

struct BleServiceManifest {
  BleStack stack;
  HidGattOwner hid_owner;
  std::array<BleInitStep, 7> steps;
  bool overrides_gatts_register_cb;
  uint8_t custom_count_cfg_calls;
  uint8_t custom_add_svcs_calls;
  bool identity_read_requires_encryption;
  bool identity_read_requires_authentication;
  bool text_write_requires_current_companion;
  uint8_t max_bonds;
};

struct CompanionGattUuids {
  Uuid128 service;
  Uuid128 notify;
  Uuid128 control;
  Uuid128 identity;
};

struct CompanionBindingProof {
  uint16_t conn_handle = UINT16_MAX;
  std::array<uint8_t, 32> companion_identity_sha256{};
  std::array<uint8_t, 32> wss_challenge{};
  std::array<uint8_t, 32> gatt_challenge{};
  std::array<uint8_t, 16> connection_id{};
};

BleServiceManifest ble_service_manifest();
CompanionGattUuids companion_gatt_uuids();
bool companion_binding_proof_is_complete(const CompanionBindingProof& proof);
bool device_id_is_valid(std::span<const uint8_t, 16> device_id);

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "esp_hidd.h"

using BleDisconnectHandler = void (*)();
using CompanionControlHandler = void (*)(std::span<const uint8_t>);

esp_err_t load_or_create_device_id(DeviceId* device_id);
esp_err_t initialize_ble(
    std::span<const uint8_t, 16> device_id,
    esp_hidd_dev_t** hid_device);
esp_err_t bind_current_companion(const CompanionBindingProof& proof);
void clear_current_companion_binding();
void set_ble_disconnect_handler(BleDisconnectHandler handler);
void set_companion_control_handler(CompanionControlHandler handler);
esp_err_t notify_current_companion(std::span<const uint8_t> frame);
#endif
