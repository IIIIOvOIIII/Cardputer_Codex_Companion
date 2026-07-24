#include "probe/ble_services.hpp"

#include <algorithm>
#include <limits>
#include <string_view>

namespace {
constexpr char kBleAdvertisedName[] = "Cardputer Codex";
constexpr uint16_t kBleGapAppearance = 0x03C1;
constexpr std::size_t kBleLegacyAdvertisingBudgetBytes = 31;
constexpr std::size_t kBleHidFixedAdvertisingBytes =
    3 + 4 + 3 + 4;  // flags + appearance + tx power + HID UUID16
constexpr int32_t kBleHidAdvertisingDurationMs =
    std::numeric_limits<int32_t>::max();
constexpr uint32_t kBleAdvertisingWatchdogIntervalMs = 5000;
constexpr uint32_t kBleStaleLinkTimeoutMs = 15000;
constexpr uint8_t kBlePairingIoCapability = 2;
constexpr bool kBlePairingRequiresMitm = true;
constexpr uint32_t kBlePairingPasskey = 123456;
constexpr uint8_t kBlePairingPasskeyDigits = 6;
constexpr bool kBlePairingInitiatesSecurityOnConnect = true;
constexpr bool kBleKeyboardReadyRequiresSuccessfulEncryption = true;
constexpr bool kBleKeyboardReadyRequiresAuthenticatedLink = true;
constexpr bool kBleKeyboardReadyRequiresInputReportSubscription = true;
constexpr uint8_t kBleBondStoreSchemaVersion = 6;

constexpr std::size_t legacy_advertising_payload_bytes(
    std::string_view name) {
  return kBleHidFixedAdvertisingBytes + 2 + name.size();
}

static_assert(legacy_advertising_payload_bytes(kBleAdvertisedName) <=
              kBleLegacyAdvertisingBudgetBytes);

constexpr CompanionGattUuids kCompanionGattUuids{
    .service = {0x7a, 0x10, 0x00, 0x01, 0x2c, 0x4d, 0x4f, 0x20,
                0x9f, 0x20, 0x43, 0x4f, 0x44, 0x45, 0x58, 0x31},
    .notify = {0x7a, 0x10, 0x00, 0x02, 0x2c, 0x4d, 0x4f, 0x20,
               0x9f, 0x20, 0x43, 0x4f, 0x44, 0x45, 0x58, 0x31},
    .control = {0x7a, 0x10, 0x00, 0x03, 0x2c, 0x4d, 0x4f, 0x20,
                0x9f, 0x20, 0x43, 0x4f, 0x44, 0x45, 0x58, 0x31},
    .identity = {0x7a, 0x10, 0x00, 0x04, 0x2c, 0x4d, 0x4f, 0x20,
                 0x9f, 0x20, 0x43, 0x4f, 0x44, 0x45, 0x58, 0x31},
};

template <size_t N>
bool has_nonzero_byte(const std::array<uint8_t, N>& value) {
  return std::any_of(value.begin(), value.end(),
                     [](uint8_t byte) { return byte != 0; });
}
}  // namespace

std::string_view ble_advertised_name() {
  return kBleAdvertisedName;
}

std::string_view ble_device_name() {
  return kBleAdvertisedName;
}

uint16_t ble_gap_appearance() {
  return kBleGapAppearance;
}

std::size_t ble_hid_legacy_advertising_payload_bytes(std::string_view name) {
  return legacy_advertising_payload_bytes(name);
}

int32_t ble_hid_advertising_duration_ms() {
  return kBleHidAdvertisingDurationMs;
}

uint32_t ble_advertising_watchdog_interval_ms() {
  return kBleAdvertisingWatchdogIntervalMs;
}

uint8_t ble_pairing_io_capability() {
  return kBlePairingIoCapability;
}

bool ble_pairing_requires_mitm() {
  return kBlePairingRequiresMitm;
}

uint32_t ble_pairing_passkey() {
  return kBlePairingPasskey;
}

bool ble_pairing_requires_keyboard_input() {
  return true;
}

bool ble_pairing_initiates_security_on_connect() {
  return kBlePairingInitiatesSecurityOnConnect;
}

bool ble_keyboard_ready_requires_successful_encryption() {
  return kBleKeyboardReadyRequiresSuccessfulEncryption;
}

bool ble_keyboard_ready_requires_authenticated_link() {
  return kBleKeyboardReadyRequiresAuthenticatedLink;
}

bool ble_keyboard_ready_requires_input_report_subscription() {
  return kBleKeyboardReadyRequiresInputReportSubscription;
}

bool ble_keyboard_ready_from_state(const BleKeyboardLinkState& state) {
  return state.gap_connected &&
         (!ble_keyboard_ready_requires_successful_encryption() ||
          state.encrypted) &&
         (!ble_keyboard_ready_requires_authenticated_link() ||
          state.authenticated) &&
         state.hidd_connected &&
         (!ble_keyboard_ready_requires_input_report_subscription() ||
          state.input_report_subscribed);
}

BleKeyboardLinkState ble_keyboard_state_after_gap_connected(
    const BleKeyboardLinkState& current) {
  return {
      .gap_connected = true,
      .encrypted = false,
      .authenticated = false,
      .hidd_connected = current.hidd_connected,
      .input_report_subscribed = false,
  };
}

std::optional<uint8_t> ble_pairing_digit_from_hid_usage(uint8_t usage) {
  if (usage >= 0x1e && usage <= 0x26) {
    return static_cast<uint8_t>(usage - 0x1d);
  }
  if (usage == 0x27) {
    return 0;
  }
  return std::nullopt;
}

bool ble_companion_link_allows(bool encrypted, bool, bool bonded) {
  return encrypted && bonded;
}

uint8_t ble_bond_store_schema_version() {
  return kBleBondStoreSchemaVersion;
}

bool ble_should_reset_bond_store(uint8_t stored_version) {
  return stored_version != ble_bond_store_schema_version();
}

bool ble_should_start_advertising(bool connected, bool advertising_active) {
  return !connected && !advertising_active;
}

uint32_t ble_stale_link_timeout_ms() {
  return kBleStaleLinkTimeoutMs;
}

bool ble_should_reset_stale_link(const BleKeyboardLinkState& state,
                                 uint64_t now_ms,
                                 uint64_t state_changed_ms) {
  return state.gap_connected &&
         !ble_keyboard_ready_from_state(state) &&
         now_ms - state_changed_ms >= ble_stale_link_timeout_ms();
}

BleServiceManifest ble_service_manifest() {
  return {
      BleStack::nimble,
      HidGattOwner::esp_hid,
      {
          BleInitStep::esp_hid_gap_init,
          BleInitStep::esp_hid_ble_gap_adv_init,
          BleInitStep::esp_hidd_dev_init,
          BleInitStep::count_custom_service,
          BleInitStep::add_custom_service,
          BleInitStep::ble_store_config_init,
          BleInitStep::esp_nimble_enable,
      },
      false,
      1,
      1,
      true,
      false,
      true,
      1,
  };
}

CompanionGattUuids companion_gatt_uuids() {
  return kCompanionGattUuids;
}

bool companion_binding_proof_is_complete(const CompanionBindingProof& proof) {
  return proof.conn_handle != UINT16_MAX &&
         has_nonzero_byte(proof.companion_identity_sha256) &&
         has_nonzero_byte(proof.wss_challenge) &&
         has_nonzero_byte(proof.gatt_challenge) &&
         proof.wss_challenge == proof.gatt_challenge &&
         has_nonzero_byte(proof.connection_id);
}

bool device_id_is_valid(std::span<const uint8_t, 16> device_id) {
  return std::any_of(device_id.begin(), device_id.end(),
                     [](uint8_t byte) { return byte != 0; });
}

std::vector<uint8_t> encode_product_text_fragment(
    uint32_t operation_id, uint8_t fragment_index, uint8_t fragment_count,
    std::span<const uint8_t> utf8) {
  if (utf8.size() > UINT16_MAX || fragment_count == 0 ||
      fragment_index >= fragment_count) {
    return {};
  }
  std::vector<uint8_t> frame;
  frame.reserve(10 + utf8.size());
  frame.push_back(1);
  frame.push_back(1);
  frame.push_back(static_cast<uint8_t>(operation_id >> 24));
  frame.push_back(static_cast<uint8_t>(operation_id >> 16));
  frame.push_back(static_cast<uint8_t>(operation_id >> 8));
  frame.push_back(static_cast<uint8_t>(operation_id));
  frame.push_back(fragment_index);
  frame.push_back(fragment_count);
  frame.push_back(static_cast<uint8_t>(utf8.size() >> 8));
  frame.push_back(static_cast<uint8_t>(utf8.size()));
  frame.insert(frame.end(), utf8.begin(), utf8.end());
  return frame;
}

#ifdef ESP_PLATFORM

#include <array>
#include <cstring>
#include <string>

#include "esp_hid_gap.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include "os/os_mbuf.h"
#include "probe/hid_engine.hpp"
#include "services/gap/ble_svc_gap.h"

extern "C" void ble_store_config_init(void);
extern "C" void ble_hid_task_start_up(void);

namespace {
constexpr char kTag[] = "phase0-ble";
constexpr char kNvsNamespace[] = "phase0_id";
constexpr char kDeviceIdKey[] = "device_id";
constexpr char kBleStoreSchemaKey[] = "ble_store_v";
constexpr const char* kDeviceName = kBleAdvertisedName;
constexpr char kManufacturer[] = "Cardputer";
constexpr uint16_t kVendorId = 0x16C0;
constexpr uint16_t kProductId = 0x05DF;
constexpr uint16_t kDeviceVersion = 0x0100;
constexpr size_t kMaxControlFragmentBytes = 256;

DeviceId g_device_id{};
std::string g_hid_serial;
esp_hid_raw_report_map_t g_keyboard_reports[1]{};
uint16_t g_notify_handle = 0;
uint16_t g_control_handle = 0;
uint16_t g_identity_handle = 0;
uint16_t g_bound_conn = BLE_HS_CONN_HANDLE_NONE;
CompanionBindingProof g_binding{};
BleDisconnectHandler g_disconnect_handler = nullptr;
CompanionControlHandler g_control_handler = nullptr;
BleConnectionHandler g_connection_handler = nullptr;
bool g_product_companion_mode = false;
BleKeyboardLinkState g_hid_state{};
bool g_last_hid_ready = false;
uint16_t g_hid_conn_handle = BLE_HS_CONN_HANDLE_NONE;
uint64_t g_hid_state_changed_ms = 0;
uint16_t g_pending_passkey_conn = BLE_HS_CONN_HANDLE_NONE;
uint32_t g_pending_passkey_value = 0;
uint8_t g_pending_passkey_count = 0;
ble_uuid16_t g_hid_service_uuid = BLE_UUID16_INIT(0x1812);
StaticTask_t g_ble_watchdog_storage{};
std::array<StackType_t, 3072> g_ble_watchdog_stack{};
TaskHandle_t g_ble_watchdog_task = nullptr;

const ble_uuid128_t kServiceUuid = BLE_UUID128_INIT(
    0x31, 0x58, 0x45, 0x44, 0x4f, 0x43, 0x20, 0x9f,
    0x20, 0x4f, 0x4d, 0x2c, 0x01, 0x00, 0x10, 0x7a);
const ble_uuid128_t kNotifyUuid = BLE_UUID128_INIT(
    0x31, 0x58, 0x45, 0x44, 0x4f, 0x43, 0x20, 0x9f,
    0x20, 0x4f, 0x4d, 0x2c, 0x02, 0x00, 0x10, 0x7a);
const ble_uuid128_t kControlUuid = BLE_UUID128_INIT(
    0x31, 0x58, 0x45, 0x44, 0x4f, 0x43, 0x20, 0x9f,
    0x20, 0x4f, 0x4d, 0x2c, 0x03, 0x00, 0x10, 0x7a);
const ble_uuid128_t kIdentityUuid = BLE_UUID128_INIT(
    0x31, 0x58, 0x45, 0x44, 0x4f, 0x43, 0x20, 0x9f,
    0x20, 0x4f, 0x4d, 0x2c, 0x04, 0x00, 0x10, 0x7a);

bool has_bonded_secure_state(uint16_t conn_handle) {
  ble_gap_conn_desc desc{};
  if (conn_handle == BLE_HS_CONN_HANDLE_NONE ||
      ble_gap_conn_find(conn_handle, &desc) != 0) {
    return false;
  }
  return ble_companion_link_allows(desc.sec_state.encrypted,
                                   desc.sec_state.authenticated,
                                   desc.sec_state.bonded);
}

bool is_current_companion(uint16_t conn_handle) {
  return conn_handle != BLE_HS_CONN_HANDLE_NONE &&
         conn_handle == g_bound_conn &&
         (g_product_companion_mode ||
          companion_binding_proof_is_complete(g_binding));
}

uint64_t now_ms() {
  return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}

void mark_hid_state_changed() {
  g_hid_state_changed_ms = now_ms();
}

void publish_hid_ready_if_changed(const char* reason) {
  const bool ready = ble_keyboard_ready_from_state(g_hid_state);
  if (ready == g_last_hid_ready) {
    return;
  }
  g_last_hid_ready = ready;
  ESP_LOGI(kTag,
           "HID keyboard ready=%d reason=%s gap=%d enc=%d auth=%d hidd=%d sub=%d",
           ready ? 1 : 0,
           reason == nullptr ? "unknown" : reason,
           g_hid_state.gap_connected ? 1 : 0,
           g_hid_state.encrypted ? 1 : 0,
           g_hid_state.authenticated ? 1 : 0,
           g_hid_state.hidd_connected ? 1 : 0,
           g_hid_state.input_report_subscribed ? 1 : 0);
  if (g_connection_handler != nullptr) {
    g_connection_handler(ready);
  }
}

void reset_hid_link_state(const char* reason) {
  g_hid_conn_handle = BLE_HS_CONN_HANDLE_NONE;
  g_hid_state = {};
  mark_hid_state_changed();
  publish_hid_ready_if_changed(reason);
}

esp_err_t migrate_ble_bond_store_if_needed() {
  nvs_handle_t handle = 0;
  esp_err_t rc = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (rc != ESP_OK) {
    return rc;
  }

  uint8_t stored_version = 0;
  rc = nvs_get_u8(handle, kBleStoreSchemaKey, &stored_version);
  if (rc != ESP_OK && rc != ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    return rc;
  }

  if (!ble_should_reset_bond_store(stored_version)) {
    nvs_close(handle);
    return ESP_OK;
  }

  const int clear_rc = ble_store_clear();
  if (clear_rc != 0) {
    ESP_LOGW(kTag, "failed to clear BLE bond store for schema migration: rc=%d",
             clear_rc);
    nvs_close(handle);
    return ESP_FAIL;
  }

  rc = nvs_set_u8(handle, kBleStoreSchemaKey,
                  ble_bond_store_schema_version());
  if (rc == ESP_OK) {
    rc = nvs_commit(handle);
  }
  nvs_close(handle);
  if (rc == ESP_OK) {
    ESP_LOGI(kTag, "BLE bond store migrated to schema %u",
             static_cast<unsigned>(ble_bond_store_schema_version()));
  }
  return rc;
}

int companion_characteristic_access(
    uint16_t conn_handle,
    uint16_t attr_handle,
    ble_gatt_access_ctxt* ctxt,
    void*) {
  if (!has_bonded_secure_state(conn_handle)) {
    return BLE_ATT_ERR_INSUFFICIENT_ENC;
  }

  if (attr_handle == g_identity_handle &&
      ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    return os_mbuf_append(ctxt->om, g_device_id.data(), g_device_id.size());
  }

  if (attr_handle == g_control_handle &&
      ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    const uint16_t fragment_size = OS_MBUF_PKTLEN(ctxt->om);
    if (g_product_companion_mode && fragment_size == 5) {
      std::array<uint8_t, 5> bind{};
      uint16_t copied = 0;
      if (ble_hs_mbuf_to_flat(ctxt->om, bind.data(), bind.size(), &copied) == 0 &&
          copied == bind.size() &&
          std::memcmp(bind.data(), "BIND1", bind.size()) == 0) {
        g_bound_conn = conn_handle;
        return 0;
      }
    }
    if (!is_current_companion(conn_handle)) {
      return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    }

    if (fragment_size > kMaxControlFragmentBytes) {
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    std::array<uint8_t, kMaxControlFragmentBytes> fragment{};
    uint16_t copied = 0;
    const int rc = ble_hs_mbuf_to_flat(
        ctxt->om, fragment.data(), fragment.size(), &copied);
    if (rc != 0 || copied != fragment_size) {
      return BLE_ATT_ERR_UNLIKELY;
    }
    if (g_control_handler != nullptr) {
      g_control_handler(std::span<const uint8_t>(fragment.data(), copied));
    }
    return 0;
  }

  if (attr_handle == g_notify_handle) {
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }
  return BLE_ATT_ERR_UNLIKELY;
}

ble_gatt_chr_def kCompanionCharacteristics[] = {
    {
        .uuid = &kNotifyUuid.u,
        .access_cb = companion_characteristic_access,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_NOTIFY |
                 BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC,
        .min_key_size = 16,
        .val_handle = &g_notify_handle,
        .cpfd = nullptr,
    },
    {
        .uuid = &kControlUuid.u,
        .access_cb = companion_characteristic_access,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_WRITE |
                 BLE_GATT_CHR_F_WRITE_ENC,
        .min_key_size = 16,
        .val_handle = &g_control_handle,
        .cpfd = nullptr,
    },
    {
        .uuid = &kIdentityUuid.u,
        .access_cb = companion_characteristic_access,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ |
                 BLE_GATT_CHR_F_READ_ENC,
        .min_key_size = 16,
        .val_handle = &g_identity_handle,
        .cpfd = nullptr,
    },
    {},
};

ble_gatt_svc_def kCompanionServices[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kServiceUuid.u,
        .includes = nullptr,
        .characteristics = kCompanionCharacteristics,
    },
    {},
};

void ble_hid_device_host_task(void*) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

esp_err_t start_hid_advertising(const char* action);

void ble_reconnect_watchdog_task(void*) {
  while (true) {
    const bool advertising_active = ble_gap_adv_active();
    if (ble_should_reset_stale_link(g_hid_state, now_ms(),
                                    g_hid_state_changed_ms)) {
      ESP_LOGW(kTag, "terminating stale HID link for reconnect");
      if (g_hid_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(g_hid_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
      }
      reset_hid_link_state("stale-link");
    } else if (ble_should_start_advertising(g_hid_state.gap_connected,
                                            advertising_active)) {
      start_hid_advertising("watchdog");
    }
    vTaskDelay(pdMS_TO_TICKS(ble_advertising_watchdog_interval_ms()));
  }
}

int hid_gap_event(struct ble_gap_event* event, void*) {
  ble_gap_conn_desc desc{};
  int rc = 0;
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      ESP_LOGI(kTag, "HID GAP connection %s; status=%d",
               event->connect.status == 0 ? "established" : "failed",
               event->connect.status);
      if (event->connect.status != 0) {
        reset_hid_link_state("gap-connect-failed");
        return 0;
      }
      g_hid_conn_handle = event->connect.conn_handle;
      g_hid_state = ble_keyboard_state_after_gap_connected(g_hid_state);
      mark_hid_state_changed();
      publish_hid_ready_if_changed("gap-connect");
      if (ble_pairing_initiates_security_on_connect()) {
        rc = ble_gap_security_initiate(event->connect.conn_handle);
        ESP_LOGI(kTag, "HID GAP security initiate: rc=%d", rc);
      }
      return 0;
    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(kTag, "HID GAP disconnect; reason=%d",
               event->disconnect.reason);
      reset_hid_link_state("gap-disconnect");
      return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
      reset_hid_link_state("adv-complete");
      ESP_LOGW(kTag, "HID advertising completed unexpectedly; reason=%d",
               event->adv_complete.reason);
      return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
      ESP_LOGI(kTag, "HID GAP encryption changed; status=%d",
               event->enc_change.status);
      if (event->enc_change.status != 0) {
        g_hid_state.encrypted = false;
        g_hid_state.authenticated = false;
        mark_hid_state_changed();
        publish_hid_ready_if_changed("enc-failed");
        return 0;
      }
      rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
      if (rc == 0) {
        g_hid_state.encrypted = desc.sec_state.encrypted;
        g_hid_state.authenticated = desc.sec_state.authenticated;
        mark_hid_state_changed();
        publish_hid_ready_if_changed("enc-change");
        ble_hid_task_start_up();
      } else {
        ESP_LOGW(kTag, "encrypted connection lookup failed: rc=%d", rc);
      }
      return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
      ESP_LOGI(kTag,
               "HID GAP subscribe conn=%u attr=%u notify %u->%u indicate %u->%u",
               static_cast<unsigned>(event->subscribe.conn_handle),
               static_cast<unsigned>(event->subscribe.attr_handle),
               static_cast<unsigned>(event->subscribe.prev_notify),
               static_cast<unsigned>(event->subscribe.cur_notify),
               static_cast<unsigned>(event->subscribe.prev_indicate),
               static_cast<unsigned>(event->subscribe.cur_indicate));
      if (event->subscribe.attr_handle == g_notify_handle) {
        ESP_LOGI(kTag, "Companion GATT notify subscription ignored for HID readiness");
        return 0;
      }
      g_hid_state.input_report_subscribed = event->subscribe.cur_notify != 0;
      mark_hid_state_changed();
      publish_hid_ready_if_changed("hid-subscribe");
      return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
      rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
      if (rc == 0) {
        ble_store_util_delete_peer(&desc.peer_id_addr);
      }
      return BLE_GAP_REPEAT_PAIRING_RETRY;
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
      ble_sm_io passkey{};
      if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
        passkey.action = event->passkey.params.action;
        passkey.passkey = ble_pairing_passkey();
        rc = ble_sm_inject_io(event->passkey.conn_handle, &passkey);
        ESP_LOGI(kTag, "display passkey injected: rc=%d", rc);
      } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
        passkey.action = event->passkey.params.action;
        passkey.numcmp_accept = 1;
        rc = ble_sm_inject_io(event->passkey.conn_handle, &passkey);
        ESP_LOGI(kTag, "numeric comparison accepted: rc=%d", rc);
      } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
        g_pending_passkey_conn = event->passkey.conn_handle;
        g_pending_passkey_value = 0;
        g_pending_passkey_count = 0;
        ESP_LOGI(kTag, "passkey input requested");
      } else {
        ESP_LOGW(kTag, "unsupported passkey action: %d",
                 event->passkey.params.action);
      }
      return 0;
    }
    default:
      return 0;
  }
}

esp_err_t start_hid_advertising(const char* action) {
  ble_hs_adv_fields fields{};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.appearance = ble_gap_appearance();
  fields.appearance_is_present = 1;
  fields.tx_pwr_lvl_is_present = 1;
  fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
  fields.name = reinterpret_cast<uint8_t*>(
      const_cast<char*>(kBleAdvertisedName));
  fields.name_len = std::strlen(kBleAdvertisedName);
  fields.name_is_complete = 1;
  fields.uuids16 = &g_hid_service_uuid;
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;

  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(kTag, "failed to set HID advertising fields for %s: rc=%d",
             action, rc);
    return ESP_FAIL;
  }

  ble_gap_adv_params adv_params{};
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(30);
  adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(50);
  rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr,
                         ble_hid_advertising_duration_ms(), &adv_params,
                         hid_gap_event, nullptr);
  if (rc != 0) {
    ESP_LOGE(kTag, "failed to %s HID advertising: rc=%d", action, rc);
    return ESP_FAIL;
  }
  return ESP_OK;
}

void ble_hidd_event_callback(
    void*,
    esp_event_base_t,
    int32_t id,
    void* event_data) {
  const auto event = static_cast<esp_hidd_event_t>(id);
  auto* param = static_cast<esp_hidd_event_data_t*>(event_data);
  if (event == ESP_HIDD_START_EVENT) {
    ESP_LOGI(kTag, "HIDD keyboard service started");
    reset_hid_link_state("hidd-start");
    start_hid_advertising("start");
    if (g_ble_watchdog_task == nullptr) {
      g_ble_watchdog_task = xTaskCreateStatic(
          ble_reconnect_watchdog_task,
          "ble-watchdog",
          g_ble_watchdog_stack.size(),
          nullptr,
          tskIDLE_PRIORITY + 1,
          g_ble_watchdog_stack.data(),
          &g_ble_watchdog_storage);
    }
    return;
  }

  if (event == ESP_HIDD_CONNECT_EVENT) {
    ESP_LOGI(kTag, "HIDD keyboard connected");
    g_hid_state.hidd_connected = true;
    mark_hid_state_changed();
    publish_hid_ready_if_changed("hidd-connect");
    return;
  }

  if (event == ESP_HIDD_DISCONNECT_EVENT) {
    ESP_LOGI(kTag, "HIDD keyboard disconnected; reason=%d",
             param == nullptr ? -1 : static_cast<int>(param->disconnect.reason));
    reset_hid_link_state("hidd-disconnect");
    clear_current_companion_binding();
    if (g_disconnect_handler != nullptr) {
      g_disconnect_handler();
    }
    start_hid_advertising("restart");
    return;
  }

  if (event == ESP_HIDD_PROTOCOL_MODE_EVENT && param != nullptr) {
    ESP_LOGI(kTag, "HIDD protocol mode map=%u mode=%u",
             static_cast<unsigned>(param->protocol_mode.map_index),
             static_cast<unsigned>(param->protocol_mode.protocol_mode));
  } else if (event == ESP_HIDD_CONTROL_EVENT && param != nullptr) {
    ESP_LOGI(kTag, "HIDD control map=%u value=%u",
             static_cast<unsigned>(param->control.map_index),
             static_cast<unsigned>(param->control.control));
  } else if (event == ESP_HIDD_OUTPUT_EVENT && param != nullptr) {
    ESP_LOGI(kTag, "HIDD output map=%u report=%u len=%u",
             static_cast<unsigned>(param->output.map_index),
             static_cast<unsigned>(param->output.report_id),
             static_cast<unsigned>(param->output.length));
  } else if (event == ESP_HIDD_FEATURE_EVENT && param != nullptr) {
    ESP_LOGI(kTag, "HIDD feature map=%u report=%u len=%u",
             static_cast<unsigned>(param->feature.map_index),
             static_cast<unsigned>(param->feature.report_id),
             static_cast<unsigned>(param->feature.length));
  } else {
    ESP_LOGI(kTag, "HIDD event id=%ld", static_cast<long>(id));
  }
}

}  // namespace

esp_err_t load_or_create_device_id(DeviceId* device_id) {
  if (device_id == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle = 0;
  esp_err_t rc = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (rc != ESP_OK) {
    return rc;
  }

  size_t length = device_id->size();
  rc = nvs_get_blob(handle, kDeviceIdKey, device_id->data(), &length);
  if (rc == ESP_OK) {
    nvs_close(handle);
    return length == device_id->size() && device_id_is_valid(*device_id)
               ? ESP_OK
               : ESP_ERR_INVALID_STATE;
  }
  if (rc != ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    return rc;
  }

  do {
    esp_fill_random(device_id->data(), device_id->size());
  } while (!device_id_is_valid(*device_id));

  rc = nvs_set_blob(handle, kDeviceIdKey, device_id->data(), device_id->size());
  if (rc == ESP_OK) {
    rc = nvs_commit(handle);
  }
  if (rc == ESP_OK) {
    DeviceId verified{};
    size_t verified_length = verified.size();
    rc = nvs_get_blob(
        handle, kDeviceIdKey, verified.data(), &verified_length);
    if (rc == ESP_OK &&
        (verified_length != verified.size() || verified != *device_id)) {
      rc = ESP_ERR_INVALID_STATE;
    }
  }
  nvs_close(handle);
  return rc;
}

esp_err_t initialize_ble(
    std::span<const uint8_t, 16> device_id,
    esp_hidd_dev_t** hid_device) {
  if (hid_device == nullptr || !device_id_is_valid(device_id)) {
    return ESP_ERR_INVALID_ARG;
  }

  std::copy(device_id.begin(), device_id.end(), g_device_id.begin());
  g_hid_serial = hid_serial_from_device_id(device_id);
  const auto report_map = keyboard_report_map();
  g_keyboard_reports[0] = {
      .data = report_map.data(),
      .len = static_cast<uint16_t>(report_map.size()),
  };

  esp_hid_device_config_t hid_config{
      .vendor_id = kVendorId,
      .product_id = kProductId,
      .version = kDeviceVersion,
      .device_name = kDeviceName,
      .manufacturer_name = kManufacturer,
      .serial_number = g_hid_serial.c_str(),
      .report_maps = g_keyboard_reports,
      .report_maps_len = 1,
  };

  esp_err_t rc = esp_hid_gap_init(HIDD_BLE_MODE);
  if (rc != ESP_OK) {
    return rc;
  }
  rc = esp_hid_ble_gap_adv_init(
      ESP_HID_APPEARANCE_KEYBOARD, kBleAdvertisedName);
  if (rc != ESP_OK) {
    return rc;
  }
  rc = esp_hidd_dev_init(
      &hid_config,
      ESP_HID_TRANSPORT_BLE,
      ble_hidd_event_callback,
      hid_device);
  if (rc != ESP_OK) {
    return rc;
  }
  if (ble_svc_gap_device_name_set(kDeviceName) != 0) {
    return ESP_FAIL;
  }
  if (ble_svc_gap_device_appearance_set(ble_gap_appearance()) != 0) {
    return ESP_FAIL;
  }

  int nimble_rc = ble_gatts_count_cfg(kCompanionServices);
  if (nimble_rc != 0) {
    return ESP_FAIL;
  }
  nimble_rc = ble_gatts_add_svcs(kCompanionServices);
  if (nimble_rc != 0) {
    return ESP_FAIL;
  }

  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = ble_pairing_requires_mitm() ? 1 : 0;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_io_cap = ble_pairing_io_capability();
  ble_hs_cfg.sm_our_key_dist =
      BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist =
      BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_store_config_init();
  rc = migrate_ble_bond_store_if_needed();
  if (rc != ESP_OK) {
    return rc;
  }
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  return esp_nimble_enable(
      reinterpret_cast<void*>(ble_hid_device_host_task));
}

esp_err_t bind_current_companion(const CompanionBindingProof& proof) {
  if (!companion_binding_proof_is_complete(proof) ||
      !has_bonded_secure_state(proof.conn_handle)) {
    return ESP_ERR_INVALID_STATE;
  }
  g_binding = proof;
  g_bound_conn = proof.conn_handle;
  return ESP_OK;
}

void clear_current_companion_binding() {
  g_bound_conn = BLE_HS_CONN_HANDLE_NONE;
  g_binding = {};
}

void set_ble_disconnect_handler(BleDisconnectHandler handler) {
  g_disconnect_handler = handler;
}

void set_companion_control_handler(CompanionControlHandler handler) {
  g_control_handler = handler;
}

void set_ble_connection_handler(BleConnectionHandler handler) {
  g_connection_handler = handler;
}

void enable_product_companion_mode() {
  g_product_companion_mode = true;
}

bool ble_pairing_input_active() {
  return g_pending_passkey_conn != BLE_HS_CONN_HANDLE_NONE;
}

bool ble_pairing_input_digit(uint8_t digit) {
  if (digit > 9 || g_pending_passkey_conn == BLE_HS_CONN_HANDLE_NONE) {
    return false;
  }
  g_pending_passkey_value =
      static_cast<uint32_t>(g_pending_passkey_value * 10 + digit);
  ++g_pending_passkey_count;
  if (g_pending_passkey_count < kBlePairingPasskeyDigits) {
    return true;
  }

  ble_sm_io passkey{};
  passkey.action = BLE_SM_IOACT_INPUT;
  passkey.passkey = g_pending_passkey_value;
  const int rc = ble_sm_inject_io(g_pending_passkey_conn, &passkey);
  ESP_LOGI(kTag, "input passkey injected: digits=%u rc=%d",
           static_cast<unsigned>(g_pending_passkey_count), rc);
  g_pending_passkey_conn = BLE_HS_CONN_HANDLE_NONE;
  g_pending_passkey_value = 0;
  g_pending_passkey_count = 0;
  return rc == 0;
}

bool ble_keyboard_ready() {
  return ble_keyboard_ready_from_state(g_hid_state);
}

esp_err_t notify_current_companion(std::span<const uint8_t> frame) {
  if (!is_current_companion(g_bound_conn) ||
      !has_bonded_secure_state(g_bound_conn) ||
      frame.size() > UINT16_MAX) {
    return ESP_ERR_INVALID_STATE;
  }

  os_mbuf* payload = ble_hs_mbuf_from_flat(
      frame.data(), static_cast<uint16_t>(frame.size()));
  if (payload == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  const int rc =
      ble_gatts_notify_custom(g_bound_conn, g_notify_handle, payload);
  return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t notify_product_utf8(uint32_t operation_id,
                              std::span<const uint8_t> utf8) {
  constexpr std::size_t kFragmentBytes = 160;
  if (utf8.empty() || utf8.size() > 1024) {
    return ESP_ERR_INVALID_ARG;
  }
  const std::size_t count =
      (utf8.size() + kFragmentBytes - 1) / kFragmentBytes;
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t offset = index * kFragmentBytes;
    const std::size_t length =
        std::min(kFragmentBytes, utf8.size() - offset);
    const auto frame = encode_product_text_fragment(
        operation_id, static_cast<uint8_t>(index),
        static_cast<uint8_t>(count), utf8.subspan(offset, length));
    const esp_err_t result = notify_current_companion(frame);
    if (result != ESP_OK) return result;
  }
  return ESP_OK;
}
#endif
