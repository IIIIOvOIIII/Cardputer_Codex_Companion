#include <cassert>
#include <optional>

#include "product/wifi_manager.hpp"

struct FakeCredentials final : WifiCredentialSource {
  std::optional<WifiCredentials> private_config;
  std::optional<WifiCredentials> runtime_config;
  std::optional<WifiCredentials> load_private() override {
    return private_config;
  }
  std::optional<WifiCredentials> load_runtime() override {
    return runtime_config;
  }
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

  machine.on_connected();
  assert(machine.state() == WifiState::online);
  machine.on_disconnected();
  assert(machine.state() == WifiState::offline);
  machine.connect_runtime(
      WifiCredentials{.ssid = "replacement", .password = "new-password"},
      50000);
  assert(machine.state() == WifiState::connecting);
  assert(machine.selected()->ssid == "replacement");

  WifiStateMachine recovery(source);
  assert(recovery.begin(0, true) == WifiCommand::start_provisioning_ap);
  assert(recovery.state() == WifiState::provisioning);

  FakeCredentials empty;
  WifiStateMachine first_boot(empty);
  assert(first_boot.begin(0, false) == WifiCommand::start_provisioning_ap);
  return 0;
}
