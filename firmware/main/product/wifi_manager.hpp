#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

inline constexpr uint64_t kWifiConnectTimeoutMs = 15000;
inline constexpr uint8_t kWifiConnectAttemptLimit = 3;

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
  start_onboarding_station,
  start_provisioning_ap,
  connect_candidate,
  persist_candidate,
  reconnect_previous,
  rollback_restored,
  rollback_failed,
  retry_selected,
};

inline bool wifi_credentials_valid(
    std::string_view ssid,
    std::string_view password
) {
  return !ssid.empty() && ssid.size() <= 32 &&
         (password.empty() ||
          (password.size() >= 8 && password.size() <= 63));
}

struct WifiScanEntry {
  std::array<char, 33> ssid{};
  int8_t rssi = 0;
  bool secured = false;
};

enum class WifiPendingEventKind : uint8_t {
  got_ip,
  disconnected,
  scan_done,
};

struct WifiPendingEvent {
  WifiPendingEventKind kind = WifiPendingEventKind::disconnected;
  uint32_t ipv4 = 0;
};

inline constexpr std::size_t kWifiEventMailboxCapacity = 8;

class WifiEventMailbox {
 public:
  bool push(WifiPendingEvent event);
  std::optional<WifiPendingEvent> pop();
  [[nodiscard]] uint32_t dropped() const {
    return dropped_.load(std::memory_order_relaxed);
  }

 private:
  std::array<WifiPendingEvent, kWifiEventMailboxCapacity> events_{};
  std::atomic<uint8_t> read_{0};
  std::atomic<uint8_t> write_{0};
  std::atomic<uint32_t> dropped_{0};
};

enum class WifiScanAction : uint8_t {
  none,
  start,
  report_failure,
};

enum class WifiScanStartResult : uint8_t {
  started,
  transient_failure,
  permanent_failure,
};

inline constexpr uint64_t kWifiScanRetryBackoffMs = 250;

class WifiScanScheduler {
 public:
  void request();
  WifiScanAction tick(uint64_t now_ms);
  void on_start_result(WifiScanStartResult result, uint64_t now_ms);
  void completed();
  [[nodiscard]] bool in_flight() const {
    return in_flight_.load(std::memory_order_acquire);
  }

 private:
  std::atomic<bool> requested_{false};
  std::atomic<bool> starting_{false};
  std::atomic<bool> in_flight_{false};
  std::atomic<bool> failure_pending_{false};
  std::atomic<uint64_t> retry_at_ms_{0};
};

constexpr uint32_t kWifiStateTaskStackBytes = 4608;

std::size_t select_wifi_scan_entries(
    std::span<const WifiScanEntry> candidates,
    std::span<WifiScanEntry> output
);

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
  WifiCommand on_disconnected(uint64_t now_ms);
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
  uint8_t connect_attempts_ = 0;
};

#ifdef ESP_PLATFORM
#include "esp_err.h"

using WifiStatusHandler = void (*)(WifiState state, const char* detail);
using WifiScanHandler = void (*)(std::span<const WifiScanEntry> entries);
esp_err_t product_wifi_start(bool recovery_mode, WifiStatusHandler handler);
esp_err_t product_wifi_save(std::string_view ssid, std::string_view password);
esp_err_t product_wifi_scan(WifiScanHandler handler);
esp_err_t product_wifi_reconnect();
bool product_wifi_has_saved_credentials();
WifiState product_wifi_state();
const char* product_wifi_ipv4();
#endif
