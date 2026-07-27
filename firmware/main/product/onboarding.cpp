#include "product/onboarding.hpp"

namespace {
constexpr uint8_t kMagic0 = 'O';
constexpr uint8_t kMagic1 = 'B';

uint32_t crc32(std::span<const uint8_t> bytes) {
  uint32_t crc = 0xffffffffu;
  for (const uint8_t byte : bytes) {
    crc ^= byte;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1) ^ (0xedb88320u & mask);
    }
  }
  return crc ^ 0xffffffffu;
}

uint32_t get_u32(const uint8_t* value) {
  return static_cast<uint32_t>(value[0]) |
         (static_cast<uint32_t>(value[1]) << 8) |
         (static_cast<uint32_t>(value[2]) << 16) |
         (static_cast<uint32_t>(value[3]) << 24);
}

void put_u32(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
  output[2] = static_cast<uint8_t>(value >> 16);
  output[3] = static_cast<uint8_t>(value >> 24);
}

bool valid(const OnboardingRecord& record) {
  return record.schema_version == 1 &&
         record.checkpoint <= OnboardingCheckpoint::complete;
}

OnboardingStep step_for(OnboardingCheckpoint checkpoint) {
  switch (checkpoint) {
    case OnboardingCheckpoint::needs_wifi:
      return OnboardingStep::wifi_scan;
    case OnboardingCheckpoint::needs_ble:
      return OnboardingStep::ble_pair_guide;
    case OnboardingCheckpoint::needs_agent:
      return OnboardingStep::agent_install_guide;
    case OnboardingCheckpoint::complete:
      return OnboardingStep::complete;
  }
  return OnboardingStep::wifi_scan;
}
}  // namespace

OnboardingRecordBytes encode_onboarding_record(
    const OnboardingRecord& record
) {
  OnboardingRecordBytes bytes{};
  bytes[0] = kMagic0;
  bytes[1] = kMagic1;
  bytes[2] = record.schema_version;
  bytes[3] = static_cast<uint8_t>(record.checkpoint);
  put_u32(bytes.data() + 8, crc32(std::span(bytes).first(8)));
  return bytes;
}

bool decode_onboarding_record(
    std::span<const uint8_t> bytes,
    OnboardingRecord* output
) {
  if (output == nullptr || bytes.size() != kOnboardingRecordBytes ||
      bytes[0] != kMagic0 || bytes[1] != kMagic1 ||
      get_u32(bytes.data() + 8) != crc32(bytes.first(8))) {
    return false;
  }
  const OnboardingRecord record{
      .schema_version = bytes[2],
      .checkpoint = static_cast<OnboardingCheckpoint>(bytes[3]),
  };
  if (!valid(record)) return false;
  *output = record;
  return true;
}

OnboardingLoadResult OnboardingStateMachine::load(
    bool legacy_commissioned
) {
  OnboardingRecordBytes bytes{};
  OnboardingRecord record;
  if (backend_.load(bytes) && decode_onboarding_record(bytes, &record)) {
    checkpoint_ = record.checkpoint;
    step_ = step_for(checkpoint_);
    return OnboardingLoadResult::loaded;
  }
  if (!legacy_commissioned) {
    checkpoint_ = OnboardingCheckpoint::needs_wifi;
    step_ = OnboardingStep::wifi_scan;
    return OnboardingLoadResult::first_run;
  }
  checkpoint_ = OnboardingCheckpoint::complete;
  step_ = OnboardingStep::complete;
  const OnboardingRecord migrated{
      .schema_version = 1,
      .checkpoint = OnboardingCheckpoint::complete,
  };
  if (!backend_.commit(encode_onboarding_record(migrated))) {
    return OnboardingLoadResult::storage_error;
  }
  return OnboardingLoadResult::migrated;
}

OnboardingResult OnboardingStateMachine::on_scan_complete(
    bool networks_found
) {
  if (step_ != OnboardingStep::wifi_scan || !networks_found) {
    return OnboardingResult::ignored;
  }
  step_ = OnboardingStep::wifi_select;
  return OnboardingResult::ok;
}

OnboardingResult OnboardingStateMachine::on_network_selected(bool secured) {
  if (step_ != OnboardingStep::wifi_select) {
    return OnboardingResult::ignored;
  }
  selected_network_secured_ = secured;
  step_ = secured ? OnboardingStep::wifi_password
                  : OnboardingStep::wifi_connect_verify;
  return OnboardingResult::ok;
}

OnboardingResult OnboardingStateMachine::on_credentials_submitted() {
  if (step_ != OnboardingStep::wifi_password) {
    return OnboardingResult::ignored;
  }
  step_ = OnboardingStep::wifi_connect_verify;
  return OnboardingResult::ok;
}

OnboardingResult OnboardingStateMachine::persist(
    OnboardingCheckpoint checkpoint,
    OnboardingStep step
) {
  const OnboardingRecord record{
      .schema_version = 1,
      .checkpoint = checkpoint,
  };
  if (!backend_.commit(encode_onboarding_record(record))) {
    return OnboardingResult::storage_error;
  }
  checkpoint_ = checkpoint;
  step_ = step;
  return OnboardingResult::ok;
}

OnboardingResult OnboardingStateMachine::on_wifi_connected() {
  if (step_ != OnboardingStep::wifi_connect_verify) {
    return OnboardingResult::ignored;
  }
  return persist(
      OnboardingCheckpoint::needs_ble,
      OnboardingStep::ble_pair_guide
  );
}

void OnboardingStateMachine::on_wifi_failed() {
  if (step_ != OnboardingStep::wifi_connect_verify) return;
  step_ = selected_network_secured_ ? OnboardingStep::wifi_password
                                    : OnboardingStep::wifi_select;
}

OnboardingResult OnboardingStateMachine::on_ble_state(
    bool bonded,
    bool hid_connected
) {
  if (step_ != OnboardingStep::ble_pair_guide ||
      !bonded || !hid_connected) {
    return OnboardingResult::ignored;
  }
  return persist(
      OnboardingCheckpoint::needs_agent,
      OnboardingStep::agent_install_guide
  );
}

OnboardingResult OnboardingStateMachine::on_agent_heartbeat(
    bool authenticated
) {
  if (step_ != OnboardingStep::agent_install_guide || !authenticated) {
    return OnboardingResult::ignored;
  }
  return persist(
      OnboardingCheckpoint::complete,
      OnboardingStep::complete
  );
}

OnboardingResult OnboardingStateMachine::restart_setup() {
  return persist(
      OnboardingCheckpoint::needs_wifi,
      OnboardingStep::wifi_scan
  );
}

#ifdef ESP_PLATFORM
#include "nvs.h"

namespace {
constexpr char kProductNvsNamespace[] = "product";
}

bool EspOnboardingBackend::load(std::span<uint8_t> output) {
  nvs_handle_t handle;
  if (nvs_open(kProductNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return false;
  }
  std::size_t size = output.size();
  const esp_err_t result = nvs_get_blob(
      handle, kOnboardingStorageKey.data(), output.data(), &size);
  nvs_close(handle);
  return result == ESP_OK && size == output.size();
}

bool EspOnboardingBackend::commit(std::span<const uint8_t> input) {
  nvs_handle_t handle;
  if (nvs_open(kProductNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
    return false;
  }
  esp_err_t result = nvs_set_blob(
      handle, kOnboardingStorageKey.data(), input.data(), input.size());
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result == ESP_OK;
}
#endif
