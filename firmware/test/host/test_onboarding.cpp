#include <algorithm>
#include <array>
#include <cassert>
#include <span>

#include "product/onboarding.hpp"

struct MemoryOnboardingBackend final : OnboardingBackend {
  OnboardingRecordBytes bytes{};
  bool has_value = false;
  bool fail_commit = false;
  int commits = 0;

  bool load(std::span<uint8_t> output) override {
    if (!has_value || output.size() != bytes.size()) return false;
    std::copy(bytes.begin(), bytes.end(), output.begin());
    return true;
  }

  bool commit(std::span<const uint8_t> input) override {
    ++commits;
    if (fail_commit || input.size() != bytes.size()) return false;
    std::copy(input.begin(), input.end(), bytes.begin());
    has_value = true;
    return true;
  }
};

int main() {
  static_assert(kOnboardingStorageKey.size() <= 15);

  MemoryOnboardingBackend blank;
  OnboardingStateMachine first_boot(blank);
  assert(first_boot.load(false) == OnboardingLoadResult::first_run);
  assert(first_boot.step() == OnboardingStep::wifi_scan);
  assert(!first_boot.completed());
  assert(blank.commits == 0);

  assert(first_boot.on_scan_complete(false) == OnboardingResult::ignored);
  assert(first_boot.step() == OnboardingStep::wifi_scan);
  assert(first_boot.on_scan_complete(true) == OnboardingResult::ok);
  assert(first_boot.step() == OnboardingStep::wifi_select);
  assert(first_boot.on_network_selected(true) == OnboardingResult::ok);
  assert(first_boot.step() == OnboardingStep::wifi_password);
  assert(first_boot.on_credentials_submitted() == OnboardingResult::ok);
  assert(first_boot.step() == OnboardingStep::wifi_connect_verify);
  first_boot.on_wifi_failed();
  assert(first_boot.step() == OnboardingStep::wifi_password);
  assert(first_boot.on_credentials_submitted() == OnboardingResult::ok);
  assert(first_boot.on_wifi_connected() == OnboardingResult::ok);
  assert(first_boot.step() == OnboardingStep::ble_pair_guide);
  assert(blank.commits == 1);

  OnboardingStateMachine after_wifi_reboot(blank);
  assert(after_wifi_reboot.load(false) == OnboardingLoadResult::loaded);
  assert(after_wifi_reboot.step() == OnboardingStep::ble_pair_guide);
  assert(
      after_wifi_reboot.on_ble_state(true, false) ==
      OnboardingResult::ignored
  );
  assert(after_wifi_reboot.step() == OnboardingStep::ble_pair_guide);
  assert(
      after_wifi_reboot.on_ble_state(true, true) == OnboardingResult::ok
  );
  assert(after_wifi_reboot.step() == OnboardingStep::agent_install_guide);

  OnboardingStateMachine after_ble_reboot(blank);
  assert(after_ble_reboot.load(false) == OnboardingLoadResult::loaded);
  assert(after_ble_reboot.step() == OnboardingStep::agent_install_guide);
  assert(
      after_ble_reboot.on_agent_heartbeat(false) ==
      OnboardingResult::ignored
  );
  assert(!after_ble_reboot.completed());
  assert(
      after_ble_reboot.on_agent_heartbeat(true) == OnboardingResult::ok
  );
  assert(after_ble_reboot.completed());

  OnboardingStateMachine completed_reboot(blank);
  assert(completed_reboot.load(false) == OnboardingLoadResult::loaded);
  assert(completed_reboot.step() == OnboardingStep::complete);
  assert(
      completed_reboot.on_ble_state(false, false) ==
      OnboardingResult::ignored
  );
  assert(
      completed_reboot.on_agent_heartbeat(false) ==
      OnboardingResult::ignored
  );
  assert(completed_reboot.completed());

  assert(completed_reboot.restart_setup() == OnboardingResult::ok);
  assert(completed_reboot.step() == OnboardingStep::wifi_scan);
  OnboardingStateMachine rerun_reboot(blank);
  assert(rerun_reboot.load(false) == OnboardingLoadResult::loaded);
  assert(rerun_reboot.step() == OnboardingStep::wifi_scan);

  MemoryOnboardingBackend legacy;
  OnboardingStateMachine migration(legacy);
  assert(migration.load(true) == OnboardingLoadResult::migrated);
  assert(migration.completed());
  assert(legacy.commits == 1);
  OnboardingStateMachine migrated_reboot(legacy);
  assert(migrated_reboot.load(false) == OnboardingLoadResult::loaded);
  assert(migrated_reboot.completed());

  MemoryOnboardingBackend failed_migration;
  failed_migration.fail_commit = true;
  OnboardingStateMachine ephemeral_migration(failed_migration);
  assert(
      ephemeral_migration.load(true) ==
      OnboardingLoadResult::storage_error
  );
  assert(ephemeral_migration.completed());

  MemoryOnboardingBackend failed_gate;
  OnboardingStateMachine gate(failed_gate);
  assert(gate.load(false) == OnboardingLoadResult::first_run);
  assert(gate.on_scan_complete(true) == OnboardingResult::ok);
  assert(gate.on_network_selected(false) == OnboardingResult::ok);
  assert(gate.step() == OnboardingStep::wifi_connect_verify);
  failed_gate.fail_commit = true;
  assert(gate.on_wifi_connected() == OnboardingResult::storage_error);
  assert(gate.step() == OnboardingStep::wifi_connect_verify);

  MemoryOnboardingBackend corrupt;
  corrupt.has_value = true;
  corrupt.bytes.fill(0xa5);
  OnboardingStateMachine recovered(corrupt);
  assert(recovered.load(false) == OnboardingLoadResult::first_run);
  assert(recovered.step() == OnboardingStep::wifi_scan);

  const OnboardingRecord complete_record{
      .schema_version = 1,
      .checkpoint = OnboardingCheckpoint::complete,
  };
  const OnboardingRecordBytes encoded =
      encode_onboarding_record(complete_record);
  OnboardingRecord decoded;
  assert(decode_onboarding_record(encoded, &decoded));
  assert(decoded == complete_record);
  auto bad_crc = encoded;
  bad_crc.back() ^= 1;
  assert(!decode_onboarding_record(bad_crc, &decoded));

  return 0;
}
