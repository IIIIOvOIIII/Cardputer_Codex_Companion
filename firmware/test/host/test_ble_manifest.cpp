#include <array>
#include <cassert>
#include <cstdint>

#include "probe/ble_services.hpp"

int main() {
  constexpr std::array expected{
      BleInitStep::esp_hid_gap_init,
      BleInitStep::esp_hid_ble_gap_adv_init,
      BleInitStep::esp_hidd_dev_init,
      BleInitStep::count_custom_service,
      BleInitStep::add_custom_service,
      BleInitStep::ble_store_config_init,
      BleInitStep::esp_nimble_enable,
  };

  const BleServiceManifest manifest = ble_service_manifest();
  assert(manifest.stack == BleStack::nimble);
  assert(manifest.hid_owner == HidGattOwner::esp_hid);
  assert(manifest.steps == expected);
  assert(!manifest.overrides_gatts_register_cb);
  assert(manifest.custom_count_cfg_calls == 1);
  assert(manifest.custom_add_svcs_calls == 1);
  assert(manifest.identity_read_requires_encryption);
  assert(!manifest.identity_read_requires_authentication);
  assert(manifest.text_write_requires_current_companion);
  assert(manifest.audio_data_notify_requires_encryption);
  assert(manifest.audio_control_write_requires_encryption);
  assert(manifest.audio_status_notify_requires_encryption);
  assert(manifest.audio_control_requires_current_companion);
  assert(manifest.max_bonds == 1);

  const CompanionGattUuids uuids = companion_gatt_uuids();
  constexpr Uuid128 service{
      0x7a, 0x10, 0x00, 0x01, 0x2c, 0x4d, 0x4f, 0x20,
      0x9f, 0x20, 0x43, 0x4f, 0x44, 0x45, 0x58, 0x31};
  constexpr Uuid128 notify{
      0x7a, 0x10, 0x00, 0x02, 0x2c, 0x4d, 0x4f, 0x20,
      0x9f, 0x20, 0x43, 0x4f, 0x44, 0x45, 0x58, 0x31};
  constexpr Uuid128 control{
      0x7a, 0x10, 0x00, 0x03, 0x2c, 0x4d, 0x4f, 0x20,
      0x9f, 0x20, 0x43, 0x4f, 0x44, 0x45, 0x58, 0x31};
  constexpr Uuid128 identity{
      0x7a, 0x10, 0x00, 0x04, 0x2c, 0x4d, 0x4f, 0x20,
      0x9f, 0x20, 0x43, 0x4f, 0x44, 0x45, 0x58, 0x31};
  constexpr Uuid128 audio_data{
      0x7a, 0x10, 0x00, 0x05, 0x2c, 0x4d, 0x4f, 0x20,
      0x9f, 0x20, 0x43, 0x4f, 0x44, 0x45, 0x58, 0x31};
  constexpr Uuid128 audio_control{
      0x7a, 0x10, 0x00, 0x06, 0x2c, 0x4d, 0x4f, 0x20,
      0x9f, 0x20, 0x43, 0x4f, 0x44, 0x45, 0x58, 0x31};
  constexpr Uuid128 audio_status{
      0x7a, 0x10, 0x00, 0x07, 0x2c, 0x4d, 0x4f, 0x20,
      0x9f, 0x20, 0x43, 0x4f, 0x44, 0x45, 0x58, 0x31};
  assert(uuids.service == service);
  assert(uuids.notify == notify);
  assert(uuids.control == control);
  assert(uuids.identity == identity);
  assert(uuids.audio_data == audio_data);
  assert(uuids.audio_control == audio_control);
  assert(uuids.audio_status == audio_status);

  assert(!ble_audio_sink_ready_from_state({}));
  assert(!ble_audio_sink_ready_from_state({
      .data_notify = true,
      .status_notify = false,
      .companion_bound = false,
      .encrypted = true,
  }));
  assert(!ble_audio_sink_ready_from_state({
      .data_notify = true,
      .status_notify = false,
      .companion_bound = true,
      .encrypted = false,
  }));
  assert(ble_audio_sink_ready_from_state({
      .data_notify = true,
      .status_notify = false,
      .companion_bound = true,
      .encrypted = true,
  }));
  assert(!ble_audio_control_allowed_from_state({
      .data_notify = false,
      .status_notify = true,
      .companion_bound = false,
      .encrypted = true,
  }));
  assert(ble_audio_control_allowed_from_state({
      .data_notify = false,
      .status_notify = true,
      .companion_bound = true,
      .encrypted = true,
  }));

  assert(ble_advertised_name() == "Cardputer Codex");
  assert(ble_device_name() == ble_advertised_name());
  assert(ble_gap_appearance() == 0x03C1);
  assert(ble_hid_legacy_advertising_payload_bytes(
             "Cardputer Companion") > 31);
  assert(ble_hid_legacy_advertising_payload_bytes(
             ble_advertised_name()) <= 31);
  assert(ble_hid_advertising_duration_ms() == 2147483647);
  assert(ble_advertising_watchdog_interval_ms() == 5000);
  assert(ble_pairing_io_capability() == 2);
  assert(ble_pairing_requires_mitm());
  assert(ble_pairing_passkey() == 123456);
  assert(ble_pairing_requires_keyboard_input());
  assert(ble_pairing_initiates_security_on_connect());
  assert(ble_should_initiate_security_on_connect(false, false));
  assert(ble_should_initiate_security_on_connect(true, false));
  assert(!ble_should_initiate_security_on_connect(true, true));
  assert(ble_audio_notify_pipeline_depth() == 6);
  assert(ble_audio_notify_credit_wait_ms() == 60);
  assert(ble_audio_notify_min_interval_us() == 15'000);
  assert(ble_audio_notification_allocation_bytes(240) == 254);
  assert(ble_audio_notification_allocation_bytes(236) == 250);
  assert(ble_audio_notification_allocation_bytes(240) > 128);
  assert(ble_audio_notification_allocation_bytes(240) <= 320);
  assert(ble_audio_preferred_connection_interval_min() == 6);
  assert(ble_audio_preferred_connection_interval_max() == 12);
  assert(ble_audio_preferred_connection_latency() == 0);
  assert(ble_audio_preferred_supervision_timeout() == 400);
  assert(ble_keyboard_ready_requires_successful_encryption());
  assert(ble_keyboard_ready_requires_authenticated_link());
  assert(ble_keyboard_ready_requires_input_report_subscription());
  assert(!ble_should_terminate_after_encryption_change(0));
  assert(ble_should_terminate_after_encryption_change(13));
  assert(!ble_keyboard_ready_from_state(BleKeyboardLinkState{
      .gap_connected = false,
      .encrypted = true,
      .authenticated = true,
      .hidd_connected = true,
      .input_report_subscribed = true,
  }));
  assert(!ble_keyboard_ready_from_state(BleKeyboardLinkState{
      .gap_connected = true,
      .encrypted = false,
      .authenticated = true,
      .hidd_connected = true,
      .input_report_subscribed = true,
  }));
  assert(!ble_keyboard_ready_from_state(BleKeyboardLinkState{
      .gap_connected = true,
      .encrypted = true,
      .authenticated = false,
      .hidd_connected = true,
      .input_report_subscribed = true,
  }));
  assert(!ble_keyboard_ready_from_state(BleKeyboardLinkState{
      .gap_connected = true,
      .encrypted = true,
      .authenticated = true,
      .hidd_connected = false,
      .input_report_subscribed = true,
  }));
  assert(!ble_keyboard_ready_from_state(BleKeyboardLinkState{
      .gap_connected = true,
      .encrypted = true,
      .authenticated = true,
      .hidd_connected = true,
      .input_report_subscribed = false,
  }));
  assert(ble_keyboard_ready_from_state(BleKeyboardLinkState{
      .gap_connected = true,
      .encrypted = true,
      .authenticated = true,
      .hidd_connected = true,
      .input_report_subscribed = true,
  }));
  const auto out_of_order_gap_state =
      ble_keyboard_state_after_gap_connected(BleKeyboardLinkState{
          .gap_connected = false,
          .encrypted = true,
          .authenticated = true,
          .hidd_connected = true,
          .input_report_subscribed = true,
      });
  assert(out_of_order_gap_state.gap_connected);
  assert(out_of_order_gap_state.encrypted);
  assert(out_of_order_gap_state.authenticated);
  assert(out_of_order_gap_state.hidd_connected);
  assert(out_of_order_gap_state.input_report_subscribed);
  assert(ble_pairing_digit_from_hid_usage(0x1e).has_value());
  assert(*ble_pairing_digit_from_hid_usage(0x1e) == 1);
  assert(*ble_pairing_digit_from_hid_usage(0x27) == 0);
  assert(!ble_pairing_digit_from_hid_usage(0x04).has_value());
  assert(ble_companion_link_allows(true, false, true));
  assert(ble_companion_link_allows(true, true, true));
  assert(!ble_companion_link_allows(false, true, true));
  assert(!ble_companion_link_allows(true, true, false));
  assert(ble_bond_store_schema_version() == 6);
  assert(ble_should_reset_bond_store(0));
  assert(ble_should_reset_bond_store(1));
  assert(ble_should_reset_bond_store(2));
  assert(ble_should_reset_bond_store(3));
  assert(ble_should_reset_bond_store(4));
  assert(ble_should_reset_bond_store(5));
  assert(!ble_should_reset_bond_store(ble_bond_store_schema_version()));
  assert(ble_should_publish_gatt_service_changed(0));
  assert(!ble_should_publish_gatt_service_changed(
      ble_gatt_database_schema_version()));
  assert(ble_should_signal_gatt_database_change(true, true));
  assert(!ble_should_signal_gatt_database_change(false, true));
  assert(!ble_should_signal_gatt_database_change(true, false));
  assert(ble_should_start_advertising(false, false));
  assert(!ble_should_start_advertising(true, false));
  assert(!ble_should_start_advertising(false, true));
  assert(ble_stale_link_timeout_ms() == 15000);
  assert(ble_should_reset_stale_link(
      true, false, false, false, 16000, 0));
  assert(!ble_should_reset_stale_link(
      true, false, false, true, 60000, 0));
  assert(!ble_should_reset_stale_link(
      true, false, true, false, 16000, 0));
  assert(!ble_should_reset_stale_link(
      true, true, false, false, 60000, 0));
  assert(!ble_should_reset_stale_link(
      false, false, false, false, 60000, 0));
  assert(!ble_should_reset_stale_link(
      true, false, false, false, 14999, 0));

  CompanionBindingProof proof{};
  proof.conn_handle = 7;
  proof.companion_identity_sha256[0] = 1;
  proof.connection_id[0] = 2;
  proof.wss_challenge[0] = 3;
  proof.gatt_challenge[0] = 3;
  assert(companion_binding_proof_is_complete(proof));
  proof.gatt_challenge[0] = 4;
  assert(!companion_binding_proof_is_complete(proof));
  proof.gatt_challenge = proof.wss_challenge;
  proof.companion_identity_sha256.fill(0);
  assert(!companion_binding_proof_is_complete(proof));

  const DeviceId valid_id{1};
  const DeviceId zero_id{};
  assert(device_id_is_valid(valid_id));
  assert(!device_id_is_valid(zero_id));

  constexpr std::array<uint8_t, 6> utf8{0xe4, 0xbd, 0xa0, 0xe5, 0xa5, 0xbd};
  const auto text_frame =
      encode_product_text_fragment(0x01020304, 0, 1, utf8);
  assert(text_frame.size() == 16);
  assert(text_frame[0] == 1);
  assert(text_frame[1] == 1);
  assert(text_frame[2] == 0x01);
  assert(text_frame[5] == 0x04);
  assert(text_frame[6] == 0);
  assert(text_frame[7] == 1);
  assert(text_frame[8] == 0);
  assert(text_frame[9] == 6);
  assert(text_frame[10] == 0xe4);
  return 0;
}
