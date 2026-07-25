#include <cassert>
#include <optional>

#include "product/wifi_manager.hpp"

struct FakeCredentials final : WifiCredentialSource {
  std::optional<WifiCredentials> private_config;
  std::optional<WifiCredentials> runtime_config;
  bool override_enabled = false;
  std::optional<WifiCredentials> load_private() override {
    return private_config;
  }
  std::optional<WifiCredentials> load_runtime() override {
    return runtime_config;
  }
  bool runtime_override_enabled() override { return override_enabled; }
};

int main() {
  FakeCredentials source;
  source.private_config = WifiCredentials{.ssid = "preset", .password = "secret"};
  source.runtime_config = WifiCredentials{.ssid = "runtime", .password = "other"};
  WifiStateMachine machine(source);

  assert(machine.begin(100, false) == WifiCommand::connect_private);
  assert(machine.state() == WifiState::connecting);
  assert(machine.tick(15099) == WifiCommand::none);
  assert(machine.tick(15100) == WifiCommand::stop_and_offline);
  assert(machine.state() == WifiState::offline);
  assert(machine.tick(30000) == WifiCommand::none);

  assert(machine.on_connected() == WifiCommand::none);
  assert(machine.state() == WifiState::online);
  machine.on_disconnected();
  assert(machine.state() == WifiState::offline);
  assert(machine.stage(
      WifiCredentials{.ssid = "replacement", .password = "new-password"},
      50000) == WifiCommand::connect_candidate);
  assert(machine.state() == WifiState::candidate_connecting);
  assert(machine.selected()->ssid == "replacement");
  assert(machine.on_connected() == WifiCommand::persist_candidate);
  assert(machine.state() == WifiState::online);

  WifiStateMachine rollback(source);
  assert(rollback.begin(0, false) == WifiCommand::connect_private);
  assert(rollback.on_connected() == WifiCommand::none);
  assert(rollback.stage(
      WifiCredentials{.ssid = "bad", .password = "candidate"}, 1000) ==
      WifiCommand::connect_candidate);
  assert(rollback.tick(15999) == WifiCommand::none);
  assert(rollback.on_candidate_timeout(16000) ==
         WifiCommand::reconnect_previous);
  assert(rollback.state() == WifiState::rollback_connecting);
  assert(rollback.selected()->ssid == "preset");
  assert(rollback.on_connected() == WifiCommand::rollback_restored);
  assert(rollback.state() == WifiState::online);

  source.override_enabled = true;
  WifiStateMachine overridden(source);
  assert(overridden.begin(0, false) == WifiCommand::connect_runtime);
  assert(overridden.selected()->ssid == "runtime");

  WifiStateMachine recovery(source);
  assert(recovery.begin(0, true) == WifiCommand::start_provisioning_ap);
  assert(recovery.state() == WifiState::provisioning);

  FakeCredentials empty;
  WifiStateMachine first_boot(empty);
  assert(first_boot.begin(0, false) == WifiCommand::start_provisioning_ap);
  return 0;
}
