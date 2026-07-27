#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

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

#ifdef ESP_PLATFORM
class EspOnboardingBackend final : public OnboardingBackend {
 public:
  bool load(std::span<uint8_t> output) override;
  bool commit(std::span<const uint8_t> input) override;
};
#endif
