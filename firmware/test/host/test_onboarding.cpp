#include <algorithm>
#include <array>
#include <cassert>
#include <span>
#include <string>
#include <string_view>

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

OnboardingInputResult press(
    OnboardingController& controller,
    uint8_t key,
    bool shift = false
) {
  const OnboardingInputResult result =
      controller.on_key(key, true, shift);
  const OnboardingInputResult released =
      controller.on_key(key, false, shift);
  assert(result.captured);
  assert(released.captured);
  return result;
}

bool content_contains(
    const OnboardingContent& content,
    std::string_view value
) {
  for (uint8_t index = 0; index < content.count; ++index) {
    if (content.lines[index].find(value) != std::string::npos) return true;
  }
  return false;
}

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

  MemoryOnboardingBackend interrupted_wifi_setup;
  OnboardingStateMachine recovered_after_wifi_save(
      interrupted_wifi_setup);
  assert(
      recovered_after_wifi_save.load(false) ==
      OnboardingLoadResult::first_run
  );
  assert(recovered_after_wifi_save.step() == OnboardingStep::wifi_scan);
  assert(
      recovered_after_wifi_save.on_wifi_connected() ==
      OnboardingResult::ok
  );
  assert(
      recovered_after_wifi_save.step() ==
      OnboardingStep::ble_pair_guide
  );
  assert(interrupted_wifi_setup.commits == 1);

  OnboardingController ble_guide(first_boot);
  const OnboardingContent ble_content = ble_guide.content();
  assert(ble_content.count == 5);
  assert(content_contains(ble_content, "ON COMPUTER: BLUETOOTH"));
  assert(content_contains(ble_content, "SEARCH: CARDPUTER CODEX"));
  assert(content_contains(ble_content, "SELECT PAIR / CONNECT"));
  assert(content_contains(ble_content, "TYPE COMPUTER CODE HERE"));

  OnboardingStateMachine after_wifi_reboot(blank);
  assert(after_wifi_reboot.load(false) == OnboardingLoadResult::loaded);
  assert(after_wifi_reboot.step() == OnboardingStep::ble_pair_guide);
  assert(
      after_wifi_reboot.on_ble_state(true, false) ==
      OnboardingResult::ignored
  );
  assert(
      after_wifi_reboot.on_ble_state(false, true) ==
      OnboardingResult::ignored
  );
  assert(after_wifi_reboot.step() == OnboardingStep::ble_pair_guide);
  assert(
      after_wifi_reboot.on_ble_state(true, true) == OnboardingResult::ok
  );
  assert(after_wifi_reboot.step() == OnboardingStep::agent_install_guide);
  OnboardingController agent_guide(after_wifi_reboot);
  const OnboardingContent agent_content =
      agent_guide.content("192.168.1.195", "12345678");
  assert(agent_content.count == 5);
  assert(content_contains(
      agent_content, "IP:192.168.1.195 PIN:12345678"));
  assert(content_contains(agent_content, "MAC: ./install.sh install"));
  assert(content_contains(agent_content, "WIN: RUN 1.3.4 SETUP.EXE"));
  assert(content_contains(agent_content, "WAITING HEARTBEAT..."));

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
  assert(after_ble_reboot.step() == OnboardingStep::complete_guide);
  assert(!after_ble_reboot.completed());
  OnboardingController complete_guide(after_ble_reboot);
  const OnboardingContent complete_content =
      complete_guide.content("192.168.1.195", "12345678");
  assert(complete_content.count == 5);
  assert(content_contains(complete_content, "SETUP COMPLETE"));
  assert(content_contains(complete_content, "SETTINGS: FN+/ X4"));
  assert(content_contains(
      complete_content, "WEB: HTTPS://192.168.1.195/"));
  assert(content_contains(complete_content, "PIN:12345678"));
  assert(content_contains(complete_content, "PRESS ANY KEY"));
  assert(complete_guide.on_key(30, true, false).captured);
  assert(after_ble_reboot.step() == OnboardingStep::complete);
  assert(complete_guide.on_key(30, false, false).captured);
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

  MemoryOnboardingBackend failed_agent_backend;
  OnboardingStateMachine failed_agent(failed_agent_backend);
  assert(failed_agent.load(false) == OnboardingLoadResult::first_run);
  assert(failed_agent.on_scan_complete(true) == OnboardingResult::ok);
  assert(failed_agent.on_network_selected(false) == OnboardingResult::ok);
  assert(failed_agent.on_wifi_connected() == OnboardingResult::ok);
  assert(failed_agent.on_ble_state(true, true) == OnboardingResult::ok);
  failed_agent_backend.fail_commit = true;
  assert(
      failed_agent.on_agent_heartbeat(true) ==
      OnboardingResult::storage_error
  );
  assert(failed_agent.step() == OnboardingStep::agent_install_guide);

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

  MemoryOnboardingBackend ui_backend;
  OnboardingStateMachine ui_machine(ui_backend);
  assert(ui_machine.load(false) == OnboardingLoadResult::first_run);
  OnboardingController controller(ui_machine);
  assert(controller.begin().command == OnboardingCommandKind::scan_wifi);
  constexpr std::array<OnboardingNetwork, 3> networks{{
      {.ssid = "Weak", .rssi = -80, .secured = true},
      {.ssid = "Strong", .rssi = -40, .secured = true},
      {.ssid = "Weak", .rssi = -50, .secured = true},
  }};
  controller.set_networks(networks);
  OnboardingContent content = controller.content();
  assert(content.count == 4);
  assert(content.lines[0].find("Strong") != std::string::npos);
  assert(content.lines[1].find("Weak") != std::string::npos);
  assert(content.lines[2].find("HIDDEN NETWORK") != std::string::npos);
  assert(content.lines[3].find("RESCAN") != std::string::npos);
  assert(press(controller, 41).command == OnboardingCommandKind::none);
  assert(ui_machine.step() == OnboardingStep::wifi_password);
  for (int index = 0; index < 7; ++index) press(controller, 30);
  assert(press(controller, 41).command == OnboardingCommandKind::none);
  assert(ui_machine.step() == OnboardingStep::wifi_password);
  assert(content_contains(controller.content(), "8-63"));
  press(controller, 30);
  const OnboardingInputResult connect = press(controller, 41);
  assert(connect.command == OnboardingCommandKind::connect_wifi);
  assert(controller.ssid_value() == "Strong");
  assert(controller.password_value() == "aaaaaaaa");
  content = controller.content();
  assert(content_contains(content, "********"));
  assert(!content_contains(content, "aaaaaaaa"));
  controller.wifi_failed("AUTH FAILED");
  assert(ui_machine.step() == OnboardingStep::wifi_password);
  assert(content_contains(controller.content(), "AUTH FAILED"));
  assert(!content_contains(controller.content(), "aaaaaaaa"));
  assert(press(controller, 41).command ==
         OnboardingCommandKind::connect_wifi);
  assert(controller.wifi_connected() == OnboardingResult::ok);
  assert(ui_machine.step() == OnboardingStep::ble_pair_guide);

  MemoryOnboardingBackend open_backend;
  OnboardingStateMachine open_machine(open_backend);
  assert(open_machine.load(false) == OnboardingLoadResult::first_run);
  OnboardingController open_controller(open_machine);
  open_controller.begin();
  constexpr std::array<OnboardingNetwork, 1> open_network{{
      {.ssid = "Guest", .rssi = -30, .secured = false},
  }};
  open_controller.set_networks(open_network);
  assert(press(open_controller, 41).command ==
         OnboardingCommandKind::connect_wifi);
  assert(open_controller.ssid_value() == "Guest");
  assert(open_controller.password_value().empty());

  MemoryOnboardingBackend hidden_backend;
  OnboardingStateMachine hidden_machine(hidden_backend);
  assert(hidden_machine.load(false) == OnboardingLoadResult::first_run);
  OnboardingController hidden_controller(hidden_machine);
  hidden_controller.begin();
  hidden_controller.set_networks(open_network);
  press(hidden_controller, 53);
  press(hidden_controller, 41);
  assert(content_contains(hidden_controller.content(), "WIFI SSID"));
  press(hidden_controller, 30);
  press(hidden_controller, 41);
  assert(content_contains(hidden_controller.content(), "WIFI PASSWORD"));
  assert(press(hidden_controller, 41).command ==
         OnboardingCommandKind::connect_wifi);
  assert(hidden_controller.ssid_value() == "a");
  assert(hidden_controller.password_value().empty());

  MemoryOnboardingBackend rescan_backend;
  OnboardingStateMachine rescan_machine(rescan_backend);
  assert(rescan_machine.load(false) == OnboardingLoadResult::first_run);
  OnboardingController rescan_controller(rescan_machine);
  rescan_controller.begin();
  rescan_controller.set_networks(open_network);
  press(rescan_controller, 53);
  press(rescan_controller, 53);
  assert(press(rescan_controller, 41).command ==
         OnboardingCommandKind::scan_wifi);
  assert(rescan_machine.step() == OnboardingStep::wifi_scan);
  assert(rescan_controller.on_key(30, true, false).captured);
  assert(rescan_controller.on_key(30, false, false).captured);

  MemoryOnboardingBackend back_backend;
  OnboardingStateMachine back_machine(back_backend);
  assert(back_machine.load(false) == OnboardingLoadResult::first_run);
  assert(back_machine.on_scan_complete(true) == OnboardingResult::ok);
  assert(back_machine.on_network_selected(false) == OnboardingResult::ok);
  assert(back_machine.on_wifi_connected() == OnboardingResult::ok);
  OnboardingController back_controller(back_machine);
  assert(press(back_controller, 0).command ==
         OnboardingCommandKind::scan_wifi);
  assert(back_machine.step() == OnboardingStep::wifi_scan);
  assert(back_machine.on_scan_complete(true) == OnboardingResult::ok);
  assert(back_machine.on_network_selected(false) == OnboardingResult::ok);
  assert(back_machine.on_wifi_connected() == OnboardingResult::ok);
  assert(back_machine.on_ble_state(true, true) == OnboardingResult::ok);
  assert(press(back_controller, 0).command ==
         OnboardingCommandKind::none);
  assert(back_machine.step() == OnboardingStep::ble_pair_guide);

  return 0;
}
