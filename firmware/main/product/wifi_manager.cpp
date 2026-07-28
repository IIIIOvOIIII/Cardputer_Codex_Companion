#include "product/wifi_manager.hpp"

#include <algorithm>

namespace {
bool scan_entry_better(
    const WifiScanEntry& lhs,
    const WifiScanEntry& rhs
) {
  if (lhs.rssi != rhs.rssi) return lhs.rssi > rhs.rssi;
  return std::string_view(lhs.ssid.data()) <
         std::string_view(rhs.ssid.data());
}
}  // namespace

std::size_t select_wifi_scan_entries(
    std::span<const WifiScanEntry> candidates,
    std::span<WifiScanEntry> output
) {
  std::fill(output.begin(), output.end(), WifiScanEntry{});
  std::size_t selected = 0;
  for (const WifiScanEntry& candidate : candidates) {
    const std::string_view ssid(candidate.ssid.data());
    if (ssid.empty()) continue;

    auto existing = std::find_if(
        output.begin(), output.begin() + selected,
        [&](const WifiScanEntry& entry) {
          return ssid == std::string_view(entry.ssid.data());
        });
    if (existing != output.begin() + selected) {
      if (scan_entry_better(candidate, *existing)) *existing = candidate;
      continue;
    }

    if (selected < output.size()) {
      output[selected++] = candidate;
      continue;
    }
    if (selected == 0) continue;

    std::size_t weakest = 0;
    for (std::size_t index = 1; index < selected; ++index) {
      if (scan_entry_better(output[weakest], output[index])) {
        weakest = index;
      }
    }
    if (scan_entry_better(candidate, output[weakest])) {
      output[weakest] = candidate;
    }
  }
  std::sort(output.begin(), output.begin() + selected, scan_entry_better);
  return selected;
}

bool WifiEventMailbox::push(WifiPendingEvent event) {
  const uint8_t write = write_.load(std::memory_order_relaxed);
  const uint8_t next =
      static_cast<uint8_t>((write + 1) % kWifiEventMailboxCapacity);
  if (next == read_.load(std::memory_order_acquire)) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  events_[write] = event;
  write_.store(next, std::memory_order_release);
  return true;
}

std::optional<WifiPendingEvent> WifiEventMailbox::pop() {
  const uint8_t read = read_.load(std::memory_order_relaxed);
  if (read == write_.load(std::memory_order_acquire)) {
    return std::nullopt;
  }
  const WifiPendingEvent event = events_[read];
  read_.store(
      static_cast<uint8_t>((read + 1) % kWifiEventMailboxCapacity),
      std::memory_order_release);
  return event;
}

void WifiScanScheduler::request() {
  requested_.store(true, std::memory_order_release);
  failure_pending_.store(false, std::memory_order_release);
}

WifiScanAction WifiScanScheduler::tick(uint64_t now_ms) {
  if (failure_pending_.exchange(false, std::memory_order_acq_rel)) {
    return WifiScanAction::report_failure;
  }
  if (!requested_.load(std::memory_order_acquire) ||
      in_flight_.load(std::memory_order_acquire) ||
      now_ms < retry_at_ms_.load(std::memory_order_acquire)) {
    return WifiScanAction::none;
  }
  bool expected = false;
  if (!starting_.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel)) {
    return WifiScanAction::none;
  }
  return WifiScanAction::start;
}

void WifiScanScheduler::on_start_result(
    WifiScanStartResult result,
    uint64_t now_ms
) {
  switch (result) {
    case WifiScanStartResult::started:
      requested_.store(false, std::memory_order_release);
      in_flight_.store(true, std::memory_order_release);
      retry_at_ms_.store(0, std::memory_order_release);
      break;
    case WifiScanStartResult::transient_failure:
      retry_at_ms_.store(
          now_ms + kWifiScanRetryBackoffMs, std::memory_order_release);
      break;
    case WifiScanStartResult::permanent_failure:
      requested_.store(false, std::memory_order_release);
      failure_pending_.store(true, std::memory_order_release);
      break;
  }
  starting_.store(false, std::memory_order_release);
}

void WifiScanScheduler::completed() {
  in_flight_.store(false, std::memory_order_release);
}

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
    connect_attempts_ = 1;
    return WifiCommand::connect_runtime;
  }
  selected_ = credentials_.load_private();
  if (selected_.has_value()) {
    state_ = WifiState::connecting;
    connect_started_ms_ = now_ms;
    connect_attempts_ = 1;
    return WifiCommand::connect_private;
  }
  selected_ = runtime;
  if (selected_.has_value()) {
    state_ = WifiState::connecting;
    connect_started_ms_ = now_ms;
    connect_attempts_ = 1;
    return WifiCommand::connect_runtime;
  }
  state_ = WifiState::provisioning;
  return WifiCommand::start_onboarding_station;
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
    if (connect_attempts_ < kWifiConnectAttemptLimit) {
      ++connect_attempts_;
      connect_started_ms_ = now_ms;
      return WifiCommand::retry_selected;
    }
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
  connect_attempts_ = 1;
}

WifiCommand WifiStateMachine::on_connected() {
  const WifiState prior = state_;
  state_ = WifiState::online;
  connect_attempts_ = 0;
  if (prior == WifiState::candidate_connecting) {
    return WifiCommand::persist_candidate;
  }
  if (prior == WifiState::rollback_connecting) {
    previous_.reset();
    return WifiCommand::rollback_restored;
  }
  return WifiCommand::none;
}

WifiCommand WifiStateMachine::on_disconnected(uint64_t now_ms) {
  if (!selected_.has_value()) {
    state_ = WifiState::offline;
    return WifiCommand::stop_and_offline;
  }
  state_ = WifiState::connecting;
  connect_started_ms_ = now_ms;
  connect_attempts_ = 1;
  return WifiCommand::retry_selected;
}

#ifdef ESP_PLATFORM
#include <atomic>
#include <cstdio>
#include <cstring>

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
WifiEventMailbox g_wifi_events;
WifiScanScheduler g_scan_scheduler;
std::array<wifi_ap_record_t, 48> g_scan_records{};
std::array<WifiScanEntry, 48> g_scan_candidates{};
std::array<WifiScanEntry, 12> g_scan_results{};
esp_netif_t* g_sta_netif = nullptr;
esp_netif_t* g_ap_netif = nullptr;
std::array<char, 16> g_ipv4{"0.0.0.0"};
std::array<char, 13> g_ap_password{};
StaticTask_t g_wifi_task_storage{};
std::array<StackType_t, kWifiStateTaskStackBytes> g_wifi_task_stack{};

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
  count = std::min<uint16_t>(count, g_scan_records.size());
  if (esp_wifi_scan_get_ap_records(&count, g_scan_records.data()) != ESP_OK) {
    if (g_scan_handler != nullptr) g_scan_handler({});
    return;
  }
  std::size_t candidate_count = 0;
  for (uint16_t index = 0; index < count; ++index) {
    const auto& record = g_scan_records[index];
    const std::string_view ssid(
        reinterpret_cast<const char*>(record.ssid),
        strnlen(reinterpret_cast<const char*>(record.ssid),
                sizeof(record.ssid)));
    if (ssid.empty()) continue;
    WifiScanEntry entry;
    std::copy_n(ssid.begin(), std::min(ssid.size(), entry.ssid.size() - 1),
                entry.ssid.begin());
    entry.rssi = record.rssi;
    entry.secured = record.authmode != WIFI_AUTH_OPEN;
    g_scan_candidates[candidate_count++] = entry;
  }
  const std::size_t published = select_wifi_scan_entries(
      std::span(g_scan_candidates).first(candidate_count), g_scan_results);
  ESP_LOGI(
      kTag, "Wi-Fi scan complete: raw=%u published=%u",
      static_cast<unsigned>(count), static_cast<unsigned>(published));
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

void start_onboarding_station() {
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();
  notify(WifiState::provisioning, nullptr);
}

void event_handler(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const auto* event = static_cast<ip_event_got_ip_t*>(data);
    g_wifi_events.push({
        .kind = WifiPendingEventKind::got_ip,
        .ipv4 = event->ip_info.ip.addr,
    });
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
    g_wifi_events.push({
        .kind = WifiPendingEventKind::scan_done,
    });
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    g_wifi_events.push({
        .kind = WifiPendingEventKind::disconnected,
    });
  }
}

void wifi_timeout_task(void*) {
  while (true) {
    const uint64_t now_ms =
        static_cast<uint64_t>(esp_timer_get_time()) / 1000;
    while (const auto event = g_wifi_events.pop()) {
      switch (event->kind) {
        case WifiPendingEventKind::got_ip: {
          esp_ip4_addr_t address{};
          address.addr = event->ipv4;
          std::snprintf(g_ipv4.data(), g_ipv4.size(), IPSTR,
                        IP2STR(&address));
          const WifiCommand connected = g_machine.on_connected();
          if (connected == WifiCommand::persist_candidate) {
            const auto& selected = g_machine.selected();
            const esp_err_t persisted =
                selected.has_value()
                    ? persist_runtime_credentials(*selected)
                    : ESP_ERR_INVALID_STATE;
            if (persisted == ESP_OK) {
              g_machine.on_persisted();
              notify(WifiState::online, g_ipv4.data());
            } else {
              g_machine.on_persist_failed(now_ms);
              esp_wifi_disconnect();
              connect_selected();
            }
          } else {
            notify(WifiState::online, g_ipv4.data());
          }
          break;
        }
        case WifiPendingEventKind::disconnected:
          if (g_machine.state() == WifiState::online) {
            const WifiCommand disconnected =
                g_machine.on_disconnected(now_ms);
            notify(WifiState::connecting, nullptr);
            if (disconnected == WifiCommand::retry_selected) {
              connect_selected();
            }
          }
          break;
        case WifiPendingEventKind::scan_done:
          g_scan_scheduler.completed();
          publish_scan_results();
          break;
      }
    }
    const WifiScanAction scan_action = g_scan_scheduler.tick(now_ms);
    if (scan_action == WifiScanAction::start) {
      const esp_err_t result = esp_wifi_scan_start(nullptr, false);
      g_scan_scheduler.on_start_result(
          result == ESP_OK
              ? WifiScanStartResult::started
              : result == ESP_ERR_WIFI_STATE
                    ? WifiScanStartResult::transient_failure
                    : WifiScanStartResult::permanent_failure,
          now_ms);
    } else if (scan_action == WifiScanAction::report_failure &&
               g_scan_handler != nullptr) {
      g_scan_handler({});
    }
    const WifiCommand command = g_machine.tick(now_ms);
    if (command == WifiCommand::reconnect_previous ||
        command == WifiCommand::retry_selected) {
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
  } else if (command == WifiCommand::start_onboarding_station) {
    start_onboarding_station();
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
  if (!wifi_credentials_valid(ssid, password)) {
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
  g_scan_scheduler.request();
  return ESP_OK;
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

bool product_wifi_has_saved_credentials() {
  if (init_optional_wifi_config_partition() != ESP_OK) return false;
  return g_credentials.load_runtime().has_value() ||
         g_credentials.load_private().has_value();
}

WifiState product_wifi_state() {
  return g_machine.state();
}

const char* product_wifi_ipv4() {
  return g_ipv4.data();
}
#endif
