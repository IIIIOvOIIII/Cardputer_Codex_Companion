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
  assert(manifest.identity_read_requires_authentication);
  assert(manifest.text_write_requires_current_companion);
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
  assert(uuids.service == service);
  assert(uuids.notify == notify);
  assert(uuids.control == control);
  assert(uuids.identity == identity);

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
  return 0;
}
