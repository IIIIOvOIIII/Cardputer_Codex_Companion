#include "probe/ble_services.hpp"

#include <algorithm>
#include <limits>
#include <string_view>

namespace {
constexpr char kBleAdvertisedName[] = "Cardputer Codex";
constexpr std::size_t kBleLegacyAdvertisingBudgetBytes = 31;
constexpr std::size_t kBleHidFixedAdvertisingBytes =
    3 + 4 + 3 + 4;  // flags + appearance + tx power + HID UUID16
constexpr int32_t kBleHidAdvertisingDurationMs =
    std::numeric_limits<int32_t>::max();
constexpr uint32_t kBleAdvertisingWatchdogIntervalMs = 5000;
constexpr uint8_t kBlePairingIoCapability = 0;

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

bool ble_should_start_advertising(bool connected, bool advertising_active) {
  return !connected && !advertising_active;
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
      true,
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
bool g_hid_connected = false;
TaskHandle_t g_advertising_watchdog_task = nullptr;
StaticTask_t g_advertising_watchdog_task_storage{};
std::array<StackType_t, 2048> g_advertising_watchdog_task_stack{};
ble_uuid16_t g_hid_service_uuid = BLE_UUID16_INIT(0x1812);

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
  return desc.sec_state.encrypted && desc.sec_state.authenticated &&
         desc.sec_state.bonded;
}

bool is_current_companion(uint16_t conn_handle) {
  return conn_handle != BLE_HS_CONN_HANDLE_NONE &&
         conn_handle == g_bound_conn &&
         (g_product_companion_mode ||
          companion_binding_proof_is_complete(g_binding));
}

int companion_characteristic_access(
    uint16_t conn_handle,
    uint16_t attr_handle,
    ble_gatt_access_ctxt* ctxt,
    void*) {
  if (!has_bonded_secure_state(conn_handle)) {
    return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
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
                 BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC |
                 BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHEN,
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
                 BLE_GATT_CHR_F_WRITE_ENC |
                 BLE_GATT_CHR_F_WRITE_AUTHEN,
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
                 BLE_GATT_CHR_F_READ_ENC |
                 BLE_GATT_CHR_F_READ_AUTHEN,
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

int hid_gap_event(struct ble_gap_event* event, void*) {
  ble_gap_conn_desc desc{};
  int rc = 0;
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      ESP_LOGI(kTag, "HID GAP connection %s; status=%d",
               event->connect.status == 0 ? "established" : "failed",
               event->connect.status);
      if (event->connect.status != 0) {
        g_hid_connected = false;
      }
      return 0;
    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(kTag, "HID GAP disconnect; reason=%d",
               event->disconnect.reason);
      g_hid_connected = false;
      return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
      g_hid_connected = false;
      ESP_LOGW(kTag, "HID advertising completed unexpectedly; reason=%d",
               event->adv_complete.reason);
      return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
      ESP_LOGI(kTag, "HID GAP encryption changed; status=%d",
               event->enc_change.status);
      rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
      if (rc == 0) {
        ble_hid_task_start_up();
      } else {
        ESP_LOGW(kTag, "encrypted connection lookup failed: rc=%d", rc);
      }
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
        passkey.passkey = 123456;
        rc = ble_sm_inject_io(event->passkey.conn_handle, &passkey);
        ESP_LOGI(kTag, "display passkey injected: rc=%d", rc);
      } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
        passkey.action = event->passkey.params.action;
        passkey.numcmp_accept = 1;
        rc = ble_sm_inject_io(event->passkey.conn_handle, &passkey);
        ESP_LOGI(kTag, "numeric comparison accepted: rc=%d", rc);
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
  fields.appearance = ESP_HID_APPEARANCE_KEYBOARD;
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

void ble_advertising_watchdog_task(void*) {
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(ble_advertising_watchdog_interval_ms()));
    if (!ble_should_start_advertising(
            g_hid_connected, ble_gap_adv_active() != 0)) {
      continue;
    }
    ESP_LOGW(kTag, "HID advertising inactive while disconnected; restarting");
    start_hid_advertising("restart");
  }
}

void ensure_ble_advertising_watchdog_started() {
  if (g_advertising_watchdog_task != nullptr) return;
  g_advertising_watchdog_task = xTaskCreateStatic(
      ble_advertising_watchdog_task, "ble-adv-watch",
      g_advertising_watchdog_task_stack.size(), nullptr,
      tskIDLE_PRIORITY + 1, g_advertising_watchdog_task_stack.data(),
      &g_advertising_watchdog_task_storage);
  if (g_advertising_watchdog_task == nullptr) {
    ESP_LOGE(kTag, "failed to start BLE advertising watchdog");
  }
}

void ble_hidd_event_callback(
    void*,
    esp_event_base_t,
    int32_t id,
    void*) {
  const auto event = static_cast<esp_hidd_event_t>(id);
  if (event == ESP_HIDD_START_EVENT) {
    g_hid_connected = false;
    ensure_ble_advertising_watchdog_started();
    start_hid_advertising("start");
    return;
  }

  if (event == ESP_HIDD_CONNECT_EVENT) {
    g_hid_connected = true;
    if (g_connection_handler != nullptr) {
      g_connection_handler(true);
    }
    return;
  }

  if (event == ESP_HIDD_DISCONNECT_EVENT) {
    g_hid_connected = false;
    clear_current_companion_binding();
    if (g_disconnect_handler != nullptr) {
      g_disconnect_handler();
    }
    if (g_connection_handler != nullptr) {
      g_connection_handler(false);
    }
    start_hid_advertising("restart");
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

  int nimble_rc = ble_gatts_count_cfg(kCompanionServices);
  if (nimble_rc != 0) {
    return ESP_FAIL;
  }
  nimble_rc = ble_gatts_add_svcs(kCompanionServices);
  if (nimble_rc != 0) {
    return ESP_FAIL;
  }

  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 1;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_io_cap = ble_pairing_io_capability();
  ble_hs_cfg.sm_our_key_dist =
      BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist =
      BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_store_config_init();
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
