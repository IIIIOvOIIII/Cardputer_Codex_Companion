#include "product/wifi_manager.hpp"

WifiCommand WifiStateMachine::begin(uint64_t now_ms, bool recovery_mode) {
  if (recovery_mode) {
    state_ = WifiState::provisioning;
    return WifiCommand::start_provisioning_ap;
  }
  const auto runtime = credentials_.load_runtime();
  if (credentials_.runtime_override_enabled() && runtime.has_value()) {
    selected_ = runtime;
    state_ = WifiState::connecting;
    connect_started_ms_ = now_ms;
    return WifiCommand::connect_runtime;
  }
  selected_ = credentials_.load_private();
  if (selected_.has_value()) {
    state_ = WifiState::connecting;
    connect_started_ms_ = now_ms;
    return WifiCommand::connect_private;
  }
  selected_ = runtime;
  if (selected_.has_value()) {
    state_ = WifiState::connecting;
    connect_started_ms_ = now_ms;
    return WifiCommand::connect_runtime;
  }
  state_ = WifiState::provisioning;
  return WifiCommand::start_provisioning_ap;
}

WifiCommand WifiStateMachine::tick(uint64_t now_ms) {
  if (state_ == WifiState::candidate_connecting) {
    return on_candidate_timeout(now_ms);
  }
  if (state_ == WifiState::rollback_connecting &&
      now_ms - connect_started_ms_ >= kWifiConnectTimeoutMs) {
    state_ = WifiState::offline;
    return WifiCommand::rollback_failed;
  }
  if (state_ == WifiState::connecting &&
      now_ms - connect_started_ms_ >= kWifiConnectTimeoutMs) {
    state_ = WifiState::offline;
    return WifiCommand::stop_and_offline;
  }
  return WifiCommand::none;
}

WifiCommand WifiStateMachine::stage(WifiCredentials candidate,
                                    uint64_t now_ms) {
  if (candidate.ssid.empty()) return WifiCommand::none;
  previous_ = selected_;
  selected_ = std::move(candidate);
  state_ = WifiState::candidate_connecting;
  connect_started_ms_ = now_ms;
  return WifiCommand::connect_candidate;
}

WifiCommand WifiStateMachine::on_candidate_timeout(uint64_t now_ms) {
  if (state_ != WifiState::candidate_connecting ||
      now_ms - connect_started_ms_ < kWifiConnectTimeoutMs) {
    return WifiCommand::none;
  }
  if (!previous_.has_value()) {
    state_ = WifiState::offline;
    return WifiCommand::rollback_failed;
  }
  selected_ = previous_;
  state_ = WifiState::rollback_connecting;
  connect_started_ms_ = now_ms;
  return WifiCommand::reconnect_previous;
}

WifiCommand WifiStateMachine::on_persist_failed(uint64_t now_ms) {
  if (!previous_.has_value()) {
    state_ = WifiState::offline;
    return WifiCommand::rollback_failed;
  }
  selected_ = previous_;
  state_ = WifiState::rollback_connecting;
  connect_started_ms_ = now_ms;
  return WifiCommand::reconnect_previous;
}

void WifiStateMachine::on_persisted() {
  previous_.reset();
}

void WifiStateMachine::connect_runtime(WifiCredentials credentials,
                                       uint64_t now_ms) {
  selected_ = std::move(credentials);
  state_ = WifiState::connecting;
  connect_started_ms_ = now_ms;
}

WifiCommand WifiStateMachine::on_connected() {
  const WifiState prior = state_;
  state_ = WifiState::online;
  if (prior == WifiState::candidate_connecting) {
    return WifiCommand::persist_candidate;
  }
  if (prior == WifiState::rollback_connecting) {
    previous_.reset();
    return WifiCommand::rollback_restored;
  }
  return WifiCommand::none;
}

void WifiStateMachine::on_disconnected() {
  state_ = WifiState::offline;
}

#ifdef ESP_PLATFORM
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {
constexpr char kTag[] = "product-wifi";

class NvsWifiCredentialSource final : public WifiCredentialSource {
 public:
  std::optional<WifiCredentials> load_private() override {
    return load("wifi_cfg");
  }
  std::optional<WifiCredentials> load_runtime() override {
    return load("nvs");
  }
  bool runtime_override_enabled() override {
    nvs_handle_t handle;
    if (nvs_open_from_partition("nvs", "wifi", NVS_READONLY, &handle) !=
        ESP_OK) {
      return false;
    }
    uint8_t enabled = 0;
    const bool result =
        nvs_get_u8(handle, "override", &enabled) == ESP_OK && enabled == 1;
    nvs_close(handle);
    return result;
  }

 private:
  static std::optional<WifiCredentials> load(const char* partition) {
    nvs_handle_t handle;
    if (nvs_open_from_partition(partition, "wifi", NVS_READONLY, &handle) !=
        ESP_OK) {
      return std::nullopt;
    }
    std::array<char, 33> ssid{};
    std::array<char, 65> password{};
    size_t ssid_size = ssid.size();
    size_t password_size = password.size();
    const bool ok =
        nvs_get_str(handle, "ssid", ssid.data(), &ssid_size) == ESP_OK &&
        nvs_get_str(handle, "password", password.data(), &password_size) ==
            ESP_OK &&
        ssid[0] != '\0';
    nvs_close(handle);
    if (!ok) {
      return std::nullopt;
    }
    return WifiCredentials{.ssid = ssid.data(), .password = password.data()};
  }
};

NvsWifiCredentialSource g_credentials;
WifiStateMachine g_machine(g_credentials);
WifiStatusHandler g_handler = nullptr;
WifiScanHandler g_scan_handler = nullptr;
std::array<WifiScanEntry, 12> g_scan_results{};
esp_netif_t* g_sta_netif = nullptr;
esp_netif_t* g_ap_netif = nullptr;
std::array<char, 16> g_ipv4{"0.0.0.0"};
std::array<char, 13> g_ap_password{};
StaticTask_t g_wifi_task_storage{};
std::array<StackType_t, 3072> g_wifi_task_stack{};

void notify(WifiState state, const char* detail) {
  if (g_handler != nullptr) {
    g_handler(state, detail);
  }
}

void connect_selected() {
  const auto& selected = g_machine.selected();
  if (!selected.has_value()) {
    return;
  }
  wifi_config_t config{};
  std::strncpy(reinterpret_cast<char*>(config.sta.ssid),
               selected->ssid.c_str(), sizeof(config.sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char*>(config.sta.password),
               selected->password.c_str(), sizeof(config.sta.password) - 1);
  config.sta.threshold.authmode = selected->password.empty()
                                      ? WIFI_AUTH_OPEN
                                      : WIFI_AUTH_WPA2_PSK;
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_set_config(WIFI_IF_STA, &config);
  esp_wifi_start();
  esp_wifi_connect();
  notify(WifiState::connecting, nullptr);
}

esp_err_t persist_runtime_credentials(const WifiCredentials& credentials) {
  nvs_handle_t handle;
  esp_err_t result =
      nvs_open_from_partition("nvs", "wifi", NVS_READWRITE, &handle);
  if (result != ESP_OK) return result;
  result = nvs_set_str(handle, "ssid", credentials.ssid.c_str());
  if (result == ESP_OK) {
    result = nvs_set_str(handle, "password", credentials.password.c_str());
  }
  if (result == ESP_OK) result = nvs_set_u8(handle, "override", 1);
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result;
}

void publish_scan_results() {
  uint16_t count = 0;
  if (esp_wifi_scan_get_ap_num(&count) != ESP_OK || count == 0) {
    if (g_scan_handler != nullptr) g_scan_handler({});
    return;
  }
  count = std::min<uint16_t>(count, 48);
  std::vector<wifi_ap_record_t> records(count);
  if (esp_wifi_scan_get_ap_records(&count, records.data()) != ESP_OK) {
    if (g_scan_handler != nullptr) g_scan_handler({});
    return;
  }
  std::vector<WifiScanEntry> unique;
  unique.reserve(count);
  for (uint16_t index = 0; index < count; ++index) {
    const auto& record = records[index];
    const std::string_view ssid(
        reinterpret_cast<const char*>(record.ssid),
        strnlen(reinterpret_cast<const char*>(record.ssid),
                sizeof(record.ssid)));
    if (ssid.empty()) continue;
    auto existing = std::find_if(
        unique.begin(), unique.end(), [&](const WifiScanEntry& entry) {
          return ssid == entry.ssid.data();
        });
    if (existing != unique.end()) {
      if (record.rssi > existing->rssi) existing->rssi = record.rssi;
      continue;
    }
    WifiScanEntry entry;
    std::copy_n(ssid.begin(), std::min(ssid.size(), entry.ssid.size() - 1),
                entry.ssid.begin());
    entry.rssi = record.rssi;
    entry.secured = record.authmode != WIFI_AUTH_OPEN;
    unique.push_back(entry);
  }
  std::sort(unique.begin(), unique.end(),
            [](const WifiScanEntry& lhs, const WifiScanEntry& rhs) {
              return lhs.rssi > rhs.rssi;
            });
  const std::size_t published =
      std::min(unique.size(), g_scan_results.size());
  std::copy_n(unique.begin(), published, g_scan_results.begin());
  if (g_scan_handler != nullptr) {
    g_scan_handler(std::span(g_scan_results).first(published));
  }
}

void start_provisioning() {
  if (g_ap_netif == nullptr) {
    g_ap_netif = esp_netif_create_default_wifi_ap();
  }
  uint8_t mac[6]{};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  wifi_config_t config{};
  std::snprintf(reinterpret_cast<char*>(config.ap.ssid),
                sizeof(config.ap.ssid), "Cardputer-%02X%02X",
                mac[4], mac[5]);
  std::snprintf(g_ap_password.data(), g_ap_password.size(), "%08lu",
                static_cast<unsigned long>(esp_random() % 100000000u));
  std::strncpy(reinterpret_cast<char*>(config.ap.password),
               g_ap_password.data(), sizeof(config.ap.password) - 1);
  config.ap.ssid_len = 0;
  config.ap.channel = 1;
  config.ap.max_connection = 2;
  config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  esp_wifi_set_mode(WIFI_MODE_AP);
  esp_wifi_set_config(WIFI_IF_AP, &config);
  esp_wifi_start();
  notify(WifiState::provisioning, g_ap_password.data());
}

void event_handler(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const auto* event = static_cast<ip_event_got_ip_t*>(data);
    std::snprintf(g_ipv4.data(), g_ipv4.size(), IPSTR,
                  IP2STR(&event->ip_info.ip));
    const WifiCommand command = g_machine.on_connected();
    if (command == WifiCommand::persist_candidate) {
      const auto& selected = g_machine.selected();
      const esp_err_t persisted =
          selected.has_value()
              ? persist_runtime_credentials(*selected)
              : ESP_ERR_INVALID_STATE;
      if (persisted == ESP_OK) {
        g_machine.on_persisted();
        notify(WifiState::online, g_ipv4.data());
      } else {
        g_machine.on_persist_failed(
            static_cast<uint64_t>(esp_timer_get_time()) / 1000);
        esp_wifi_disconnect();
        connect_selected();
      }
    } else {
      notify(WifiState::online, g_ipv4.data());
    }
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
    publish_scan_results();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED &&
             g_machine.state() == WifiState::online) {
    g_machine.on_disconnected();
    notify(WifiState::offline, nullptr);
  }
}

void wifi_timeout_task(void*) {
  while (true) {
    const uint64_t now_ms =
        static_cast<uint64_t>(esp_timer_get_time()) / 1000;
    const WifiCommand command = g_machine.tick(now_ms);
    if (command == WifiCommand::reconnect_previous) {
      esp_wifi_disconnect();
      connect_selected();
    } else if (command == WifiCommand::stop_and_offline ||
               command == WifiCommand::rollback_failed) {
      esp_wifi_disconnect();
      notify(WifiState::offline, nullptr);
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

esp_err_t init_optional_wifi_config_partition() {
  esp_err_t result = nvs_flash_init_partition("wifi_cfg");
  if (result == ESP_OK) {
    return ESP_OK;
  }
  if (result == ESP_ERR_NOT_FOUND) {
    ESP_LOGW(kTag, "wifi_cfg partition is absent; private Wi-Fi config disabled");
    return ESP_OK;
  }
  if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
      result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    result = nvs_flash_erase_partition("wifi_cfg");
    if (result == ESP_OK) {
      result = nvs_flash_init_partition("wifi_cfg");
    }
  }
  return result;
}
}  // namespace

esp_err_t product_wifi_start(bool recovery_mode, WifiStatusHandler handler) {
  g_handler = handler;
  esp_err_t result = init_optional_wifi_config_partition();
  if (result != ESP_OK) return result;
  result = esp_netif_init();
  if (result != ESP_OK) return result;
  result = esp_event_loop_create_default();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
  if (g_sta_netif == nullptr) {
    g_sta_netif = esp_netif_create_default_wifi_sta();
  }
  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  result = esp_wifi_init(&config);
  if (result != ESP_OK) {
    return result;
  }
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler,
                             nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler,
                             nullptr);

  const WifiCommand command = g_machine.begin(
      static_cast<uint64_t>(esp_timer_get_time()) / 1000, recovery_mode);
  if (command == WifiCommand::start_provisioning_ap) {
    start_provisioning();
  } else {
    connect_selected();
  }
  xTaskCreateStatic(wifi_timeout_task, "wifi-state", g_wifi_task_stack.size(),
                    nullptr, tskIDLE_PRIORITY + 1, g_wifi_task_stack.data(),
                    &g_wifi_task_storage);
  return ESP_OK;
}

esp_err_t product_wifi_save(std::string_view ssid,
                            std::string_view password) {
  if (ssid.empty() || ssid.size() > 32 || password.size() > 64) {
    return ESP_ERR_INVALID_ARG;
  }
  const std::string ssid_copy(ssid);
  const std::string password_copy(password);
  const WifiCommand command = g_machine.stage(
      WifiCredentials{.ssid = ssid_copy, .password = password_copy},
      static_cast<uint64_t>(esp_timer_get_time()) / 1000);
  if (command != WifiCommand::connect_candidate) {
    return ESP_ERR_INVALID_STATE;
  }
  esp_wifi_disconnect();
  connect_selected();
  return ESP_OK;
}

esp_err_t product_wifi_scan(WifiScanHandler handler) {
  if (handler == nullptr) return ESP_ERR_INVALID_ARG;
  g_scan_handler = handler;
  return esp_wifi_scan_start(nullptr, false);
}

esp_err_t product_wifi_reconnect() {
  if (!g_machine.selected().has_value()) {
    return ESP_ERR_INVALID_STATE;
  }
  g_machine.connect_runtime(
      *g_machine.selected(),
      static_cast<uint64_t>(esp_timer_get_time()) / 1000);
  esp_wifi_disconnect();
  connect_selected();
  return ESP_OK;
}

WifiState product_wifi_state() {
  return g_machine.state();
}

const char* product_wifi_ipv4() {
  return g_ipv4.data();
}
#endif
