#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "product/audio_protocol.hpp"

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
  bool audio_data_notify_requires_encryption;
  bool audio_control_write_requires_encryption;
  bool audio_status_notify_requires_encryption;
  bool audio_control_requires_current_companion;
  uint8_t max_bonds;
};

struct CompanionGattUuids {
  Uuid128 service;
  Uuid128 notify;
  Uuid128 control;
  Uuid128 identity;
  Uuid128 audio_data;
  Uuid128 audio_control;
  Uuid128 audio_status;
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

struct BleAudioSubscriptionState {
  bool data_notify = false;
  bool status_notify = false;
  bool companion_bound = false;
  bool encrypted = false;
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
uint8_t ble_audio_notify_pipeline_depth();
uint16_t ble_audio_notify_credit_wait_ms();
uint32_t ble_audio_notify_min_interval_us();
constexpr std::size_t ble_audio_notification_allocation_bytes(
    std::size_t payload_bytes) {
  constexpr std::size_t kLegacyVhciAttHeadroomBytes =
      4 + 4 + 5 + 1;  // HCI + L2CAP + largest ATT base + packet type.
  return kLegacyVhciAttHeadroomBytes + payload_bytes;
}
uint16_t ble_audio_preferred_connection_interval_min();
uint16_t ble_audio_preferred_connection_interval_max();
uint16_t ble_audio_preferred_connection_latency();
uint16_t ble_audio_preferred_supervision_timeout();
uint8_t ble_pairing_io_capability();
bool ble_pairing_requires_mitm();
uint32_t ble_pairing_passkey();
bool ble_pairing_requires_keyboard_input();
bool ble_pairing_initiates_security_on_connect();
bool ble_should_initiate_security_on_connect(
    bool encrypted, bool authenticated);
bool ble_keyboard_ready_requires_successful_encryption();
bool ble_keyboard_ready_requires_authenticated_link();
bool ble_keyboard_ready_requires_input_report_subscription();
bool ble_should_terminate_after_encryption_change(int status);
bool ble_keyboard_ready_from_state(const BleKeyboardLinkState& state);
bool ble_audio_sink_ready_from_state(
    const BleAudioSubscriptionState& state);
bool ble_audio_control_allowed_from_state(
    const BleAudioSubscriptionState& state);
bool ble_audio_status_ready_from_state(
    const BleAudioSubscriptionState& state);
BleKeyboardLinkState ble_keyboard_state_after_gap_connected(
    const BleKeyboardLinkState& current);
std::optional<uint8_t> ble_pairing_digit_from_hid_usage(uint8_t usage);
bool ble_companion_link_allows(bool encrypted, bool authenticated, bool bonded);
uint8_t ble_bond_store_schema_version();
bool ble_should_reset_bond_store(uint8_t stored_version);
uint8_t ble_gatt_database_schema_version();
bool ble_should_publish_gatt_service_changed(uint8_t stored_version);
bool ble_should_signal_gatt_database_change(
    bool migration_pending,
    bool indicate_enabled);
bool ble_should_start_advertising(bool connected, bool advertising_active);
uint32_t ble_stale_link_timeout_ms();
bool ble_should_reset_stale_link(bool gap_connected,
                                 bool keyboard_ready,
                                 bool audio_sink_ready,
                                 bool pairing_input_active,
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
using AudioControlHandler = void (*)(AudioControlMessage message);
using AudioSinkStateHandler = void (*)(bool ready);

esp_err_t load_or_create_device_id(DeviceId* device_id);
esp_err_t initialize_ble(
    std::span<const uint8_t, 16> device_id,
    esp_hidd_dev_t** hid_device);
esp_err_t bind_current_companion(const CompanionBindingProof& proof);
void clear_current_companion_binding();
void set_ble_disconnect_handler(BleDisconnectHandler handler);
void set_companion_control_handler(CompanionControlHandler handler);
void set_ble_connection_handler(BleConnectionHandler handler);
void set_audio_control_handler(AudioControlHandler handler);
void set_audio_sink_state_handler(AudioSinkStateHandler handler);
void enable_product_companion_mode();
bool ble_pairing_input_active();
bool ble_pairing_input_digit(uint8_t digit);
bool ble_keyboard_ready();
bool ble_audio_sink_ready();
bool ble_audio_status_ready();
esp_err_t notify_current_companion(std::span<const uint8_t> frame);
esp_err_t notify_audio_frame(std::span<const uint8_t> frame);
esp_err_t notify_audio_status(std::span<const uint8_t> status);
esp_err_t notify_product_utf8(uint32_t operation_id,
                              std::span<const uint8_t> utf8);
#endif
