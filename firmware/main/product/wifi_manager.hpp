#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

inline constexpr uint64_t kWifiConnectTimeoutMs = 15000;

struct WifiCredentials {
  std::string ssid;
  std::string password;
};

class WifiCredentialSource {
 public:
  virtual ~WifiCredentialSource() = default;
  virtual std::optional<WifiCredentials> load_private() = 0;
  virtual std::optional<WifiCredentials> load_runtime() = 0;
  virtual bool runtime_override_enabled() = 0;
};

enum class WifiState : uint8_t {
  idle,
  connecting,
  candidate_connecting,
  rollback_connecting,
  online,
  offline,
  provisioning,
};

enum class WifiCommand : uint8_t {
  none,
  connect_private,
  connect_runtime,
  stop_and_offline,
  start_provisioning_ap,
  connect_candidate,
  persist_candidate,
  reconnect_previous,
  rollback_restored,
  rollback_failed,
};

class WifiStateMachine {
 public:
  explicit WifiStateMachine(WifiCredentialSource& credentials)
      : credentials_(credentials) {}
  WifiCommand begin(uint64_t now_ms, bool recovery_mode);
  WifiCommand tick(uint64_t now_ms);
  WifiCommand stage(WifiCredentials candidate, uint64_t now_ms);
  WifiCommand on_candidate_timeout(uint64_t now_ms);
  WifiCommand on_persist_failed(uint64_t now_ms);
  void on_persisted();
  void connect_runtime(WifiCredentials credentials, uint64_t now_ms);
  WifiCommand on_connected();
  void on_disconnected();
  [[nodiscard]] WifiState state() const { return state_; }
  [[nodiscard]] const std::optional<WifiCredentials>& selected() const {
    return selected_;
  }

 private:
  WifiCredentialSource& credentials_;
  std::optional<WifiCredentials> selected_;
  std::optional<WifiCredentials> previous_;
  WifiState state_ = WifiState::idle;
  uint64_t connect_started_ms_ = 0;
};

#ifdef ESP_PLATFORM
#include "esp_err.h"

struct WifiScanEntry {
  std::array<char, 33> ssid{};
  int8_t rssi = 0;
  bool secured = false;
};

using WifiStatusHandler = void (*)(WifiState state, const char* detail);
using WifiScanHandler = void (*)(std::span<const WifiScanEntry> entries);
esp_err_t product_wifi_start(bool recovery_mode, WifiStatusHandler handler);
esp_err_t product_wifi_save(std::string_view ssid, std::string_view password);
esp_err_t product_wifi_scan(WifiScanHandler handler);
esp_err_t product_wifi_reconnect();
WifiState product_wifi_state();
const char* product_wifi_ipv4();
#endif
