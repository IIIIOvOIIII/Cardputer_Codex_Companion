#pragma once

#include <cstdint>
#include <optional>
#include <string>

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
};

enum class WifiState : uint8_t {
  idle,
  connecting,
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
};

class WifiStateMachine {
 public:
  explicit WifiStateMachine(WifiCredentialSource& credentials)
      : credentials_(credentials) {}
  WifiCommand begin(uint64_t now_ms, bool recovery_mode);
  WifiCommand tick(uint64_t now_ms);
  void connect_runtime(WifiCredentials credentials, uint64_t now_ms);
  void on_connected();
  void on_disconnected();
  [[nodiscard]] WifiState state() const { return state_; }
  [[nodiscard]] const std::optional<WifiCredentials>& selected() const {
    return selected_;
  }

 private:
  WifiCredentialSource& credentials_;
  std::optional<WifiCredentials> selected_;
  WifiState state_ = WifiState::idle;
  uint64_t connect_started_ms_ = 0;
};

#ifdef ESP_PLATFORM
#include "esp_err.h"

using WifiStatusHandler = void (*)(WifiState state, const char* detail);
esp_err_t product_wifi_start(bool recovery_mode, WifiStatusHandler handler);
esp_err_t product_wifi_save(std::string_view ssid, std::string_view password);
esp_err_t product_wifi_reconnect();
WifiState product_wifi_state();
const char* product_wifi_ipv4();
#endif
