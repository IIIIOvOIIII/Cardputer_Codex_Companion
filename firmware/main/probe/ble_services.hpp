#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

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

struct BleKeyboardLinkState {
  bool gap_connected = false;
  bool encrypted = false;
  bool authenticated = false;
  bool hidd_connected = false;
  bool input_report_subscribed = false;
};

BleServiceManifest ble_service_manifest();
CompanionGattUuids companion_gatt_uuids();
bool companion_binding_proof_is_complete(const CompanionBindingProof& proof);
bool device_id_is_valid(std::span<const uint8_t, 16> device_id);
std::string_view ble_advertised_name();
std::string_view ble_device_name();
uint16_t ble_gap_appearance();
std::size_t ble_hid_legacy_advertising_payload_bytes(std::string_view name);
int32_t ble_hid_advertising_duration_ms();
uint32_t ble_advertising_watchdog_interval_ms();
uint8_t ble_pairing_io_capability();
bool ble_pairing_requires_mitm();
uint32_t ble_pairing_passkey();
bool ble_pairing_requires_keyboard_input();
bool ble_pairing_initiates_security_on_connect();
bool ble_keyboard_ready_requires_successful_encryption();
bool ble_keyboard_ready_requires_authenticated_link();
bool ble_keyboard_ready_requires_input_report_subscription();
bool ble_keyboard_ready_from_state(const BleKeyboardLinkState& state);
BleKeyboardLinkState ble_keyboard_state_after_gap_connected(
    const BleKeyboardLinkState& current);
std::optional<uint8_t> ble_pairing_digit_from_hid_usage(uint8_t usage);
bool ble_companion_link_allows(bool encrypted, bool authenticated, bool bonded);
uint8_t ble_bond_store_schema_version();
bool ble_should_reset_bond_store(uint8_t stored_version);
bool ble_should_start_advertising(bool connected, bool advertising_active);
uint32_t ble_stale_link_timeout_ms();
bool ble_should_reset_stale_link(const BleKeyboardLinkState& state,
                                 uint64_t now_ms,
                                 uint64_t state_changed_ms);
std::vector<uint8_t> encode_product_text_fragment(
    uint32_t operation_id, uint8_t fragment_index, uint8_t fragment_count,
    std::span<const uint8_t> utf8);

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "esp_hidd.h"

using BleDisconnectHandler = void (*)();
using CompanionControlHandler = void (*)(std::span<const uint8_t>);
using BleConnectionHandler = void (*)(bool connected);

esp_err_t load_or_create_device_id(DeviceId* device_id);
esp_err_t initialize_ble(
    std::span<const uint8_t, 16> device_id,
    esp_hidd_dev_t** hid_device);
esp_err_t bind_current_companion(const CompanionBindingProof& proof);
void clear_current_companion_binding();
void set_ble_disconnect_handler(BleDisconnectHandler handler);
void set_companion_control_handler(CompanionControlHandler handler);
void set_ble_connection_handler(BleConnectionHandler handler);
void enable_product_companion_mode();
bool ble_pairing_input_active();
bool ble_pairing_input_digit(uint8_t digit);
bool ble_keyboard_ready();
esp_err_t notify_current_companion(std::span<const uint8_t> frame);
esp_err_t notify_product_utf8(uint32_t operation_id,
                              std::span<const uint8_t> utf8);
#endif
