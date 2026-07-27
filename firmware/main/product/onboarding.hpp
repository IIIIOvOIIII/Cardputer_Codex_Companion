#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "product/product_types.hpp"

enum class OnboardingCheckpoint : uint8_t {
  needs_wifi,
  needs_ble,
  needs_agent,
  complete,
};

enum class OnboardingStep : uint8_t {
  wifi_scan,
  wifi_select,
  wifi_password,
  wifi_connect_verify,
  ble_pair_guide,
  agent_install_guide,
  complete,
};

struct OnboardingRecord {
  uint8_t schema_version = 1;
  OnboardingCheckpoint checkpoint = OnboardingCheckpoint::needs_wifi;

  bool operator==(const OnboardingRecord&) const = default;
};

inline constexpr std::size_t kOnboardingRecordBytes = 12;
inline constexpr std::string_view kOnboardingStorageKey = "onboarding";
static_assert(kOnboardingStorageKey.size() <= 15);
using OnboardingRecordBytes =
    std::array<uint8_t, kOnboardingRecordBytes>;

OnboardingRecordBytes encode_onboarding_record(
    const OnboardingRecord& record
);
bool decode_onboarding_record(
    std::span<const uint8_t> bytes,
    OnboardingRecord* output
);

class OnboardingBackend {
 public:
  virtual ~OnboardingBackend() = default;
  virtual bool load(std::span<uint8_t> output) = 0;
  virtual bool commit(std::span<const uint8_t> input) = 0;
};

enum class OnboardingLoadResult : uint8_t {
  loaded,
  first_run,
  migrated,
  storage_error,
};

enum class OnboardingResult : uint8_t {
  ok,
  ignored,
  storage_error,
};

class OnboardingStateMachine {
 public:
  explicit OnboardingStateMachine(OnboardingBackend& backend)
      : backend_(backend) {}

  OnboardingLoadResult load(bool legacy_commissioned);
  OnboardingResult on_scan_complete(bool networks_found);
  OnboardingResult on_network_selected(bool secured);
  OnboardingResult on_credentials_submitted();
  OnboardingResult on_wifi_connected();
  void on_wifi_failed();
  OnboardingResult on_ble_state(bool bonded, bool hid_connected);
  OnboardingResult on_agent_heartbeat(bool authenticated);
  OnboardingResult restart_setup();
  OnboardingResult previous_step();
  void request_wifi_scan();
  void return_to_wifi_select();

  [[nodiscard]] OnboardingStep step() const { return step_; }
  [[nodiscard]] bool completed() const {
    return step_ == OnboardingStep::complete;
  }

 private:
  OnboardingResult persist(
      OnboardingCheckpoint checkpoint,
      OnboardingStep step
  );

  OnboardingBackend& backend_;
  OnboardingCheckpoint checkpoint_ = OnboardingCheckpoint::needs_wifi;
  OnboardingStep step_ = OnboardingStep::wifi_scan;
  bool selected_network_secured_ = true;
};

struct OnboardingNetwork {
  std::string_view ssid;
  int8_t rssi = 0;
  bool secured = false;
};

enum class OnboardingCommandKind : uint8_t {
  none,
  scan_wifi,
  connect_wifi,
};

struct OnboardingInputResult {
  bool captured = false;
  OnboardingCommandKind command = OnboardingCommandKind::none;
};

struct OnboardingContent {
  std::array<std::string, 12> lines{};
  uint8_t count = 0;
  uint8_t selected = 0;
  uint8_t scroll = 0;
};

class OnboardingController {
 public:
  explicit OnboardingController(OnboardingStateMachine& state)
      : state_(state) {}

  OnboardingInputResult begin();
  void set_networks(std::span<const OnboardingNetwork> networks);
  OnboardingInputResult on_key(
      uint8_t physical_key,
      bool pressed,
      bool shift
  );
  OnboardingResult wifi_connected();
  void wifi_failed(std::string_view reason);
  [[nodiscard]] OnboardingContent content() const;
  [[nodiscard]] std::string_view ssid_value() const {
    return selected_ssid_;
  }
  [[nodiscard]] std::string_view password_value() const {
    return password_;
  }
  [[nodiscard]] bool active() const { return !state_.completed(); }

 private:
  enum class Editor : uint8_t {
    none,
    hidden_ssid,
    password,
  };

  struct StoredNetwork {
    std::string ssid;
    int8_t rssi = 0;
    bool secured = false;
  };

  OnboardingInputResult browse_key(uint8_t physical_key);
  OnboardingInputResult edit_key(uint8_t physical_key, bool shift);
  static char key_character(uint8_t physical_key, bool shift);
  void update_scroll();

  OnboardingStateMachine& state_;
  std::array<StoredNetwork, 10> networks_{};
  uint8_t network_count_ = 0;
  uint8_t selected_ = 0;
  uint8_t scroll_ = 0;
  Editor editor_ = Editor::none;
  std::array<bool, kPhysicalKeyCount> captured_{};
  std::string selected_ssid_;
  std::string editor_value_;
  std::string password_;
  std::string error_;
  bool allow_empty_password_ = false;
};

#ifdef ESP_PLATFORM
class EspOnboardingBackend final : public OnboardingBackend {
 public:
  bool load(std::span<uint8_t> output) override;
  bool commit(std::span<const uint8_t> input) override;
};
#endif
