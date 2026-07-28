#include <algorithm>
#include <array>
#include <cassert>
#include <optional>
#include <string_view>

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

WifiScanEntry scan_entry(
    std::string_view ssid,
    int8_t rssi,
    bool secured = true
) {
  WifiScanEntry entry;
  std::copy_n(
      ssid.begin(), std::min(ssid.size(), entry.ssid.size() - 1),
      entry.ssid.begin());
  entry.rssi = rssi;
  entry.secured = secured;
  return entry;
}

int main() {
  FakeCredentials source;
  source.private_config = WifiCredentials{.ssid = "preset", .password = "secret"};
  source.runtime_config = WifiCredentials{.ssid = "runtime", .password = "other"};
  WifiStateMachine machine(source);

  assert(machine.begin(100, false) == WifiCommand::connect_private);
  assert(machine.state() == WifiState::connecting);
  assert(machine.tick(15099) == WifiCommand::none);
  assert(machine.tick(15100) == WifiCommand::retry_selected);
  assert(machine.state() == WifiState::connecting);
  assert(machine.tick(30099) == WifiCommand::none);
  assert(machine.tick(30100) == WifiCommand::retry_selected);
  assert(machine.tick(45100) == WifiCommand::stop_and_offline);
  assert(machine.state() == WifiState::offline);
  assert(machine.tick(30000) == WifiCommand::none);

  assert(machine.on_connected() == WifiCommand::none);
  assert(machine.state() == WifiState::online);
  assert(machine.on_disconnected(31000) == WifiCommand::retry_selected);
  assert(machine.state() == WifiState::connecting);
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
  assert(first_boot.begin(0, false) ==
         WifiCommand::start_onboarding_station);
  assert(wifi_credentials_valid("Guest", ""));
  assert(!wifi_credentials_valid("", ""));
  assert(!wifi_credentials_valid("Secure", "short"));
  assert(wifi_credentials_valid("Secure", "12345678"));
  assert(wifi_credentials_valid(
      "Secure", std::string(63, 'x')));
  assert(!wifi_credentials_valid(
      "Secure", std::string(64, 'x')));

  const std::array candidates{
      scan_entry("duplicate", -80, false),
      scan_entry("net-01", -21),
      scan_entry("net-02", -22),
      scan_entry("net-03", -23),
      scan_entry("net-04", -24),
      scan_entry("net-05", -25),
      scan_entry("net-06", -26),
      scan_entry("net-07", -27),
      scan_entry("net-08", -28),
      scan_entry("net-09", -29),
      scan_entry("net-10", -30),
      scan_entry("net-11", -31),
      scan_entry("weak", -90),
      scan_entry("duplicate", -20, true),
      scan_entry("replacement", -19),
      scan_entry("", -1),
  };
  std::array<WifiScanEntry, 12> selected{};
  const std::size_t selected_count =
      select_wifi_scan_entries(candidates, selected);
  assert(selected_count == selected.size());
  assert(std::string_view(selected[0].ssid.data()) == "replacement");
  assert(std::string_view(selected[1].ssid.data()) == "duplicate");
  assert(selected[1].rssi == -20);
  assert(selected[1].secured);
  assert(std::none_of(
      selected.begin(), selected.end(), [](const WifiScanEntry& entry) {
        return std::string_view(entry.ssid.data()) == "weak";
      }));

  const std::array tied{
      scan_entry("charlie", -40),
      scan_entry("alpha", -40),
      scan_entry("bravo", -40),
  };
  std::array<WifiScanEntry, 12> tied_selected{};
  assert(select_wifi_scan_entries(tied, tied_selected) == tied.size());
  assert(std::string_view(tied_selected[0].ssid.data()) == "alpha");
  assert(std::string_view(tied_selected[1].ssid.data()) == "bravo");
  assert(std::string_view(tied_selected[2].ssid.data()) == "charlie");
  return 0;
}
