#include <cassert>
#include <array>
#include <span>
#include <string>

#include "product/ui_model.hpp"

int main() {
  UiModel incompatible;
  incompatible.set_storage_compatibility(
      evaluate_storage_compatibility(false, false, false, 0));
  UiPageContent partition_error = incompatible.page_content();
  assert(partition_error.count == 4);
  assert(partition_error.lines[0] == "PARTITION ERROR");
  assert(partition_error.lines[1] == "MISSING");
  assert(partition_error.lines[2] == "USE FACTORY 1.3.0");
  assert(partition_error.lines[3] == "OR LAUNCHER 2.8+");
  incompatible.navigate(UiNavAction::next_page);
  UiPageContent incompatible_device = incompatible.page_content();
  assert(incompatible_device.lines[2] == "STORAGE:MISSING");

  UiModel model;
  assert(model.page() == UiPage::pet);
  UiModel onboarding_model;
  const std::array<std::string_view, 3> onboarding_rows{
      "SETUP 1/3", "> Strong -40 *", "  HIDDEN NETWORK"};
  onboarding_model.show_onboarding(onboarding_rows, 0, 0);
  assert(onboarding_model.page() == UiPage::onboarding);
  assert(!ui_page_allows_host_input(onboarding_model.page()));
  assert(onboarding_model.page_content().lines[1] == "> Strong -40 *");
  onboarding_model.navigate(UiNavAction::next_page);
  assert(onboarding_model.page() == UiPage::onboarding);
  onboarding_model.finish_onboarding();
  assert(onboarding_model.page() == UiPage::pet);
  assert(model.revision() == 0);
  assert(model.boot_line(BootStage::display) == "DISPLAY ...");
  model.set_profile("SAFE");
  assert(model.revision() == 0);
  model.set_web("0.0.0.0", "--------");
  assert(model.revision() == 0);
  model.set_stage(BootStage::display, ServiceState::ok);
  const uint32_t first_revision = model.revision();
  assert(first_revision > 0);
  model.set_stage(BootStage::display, ServiceState::ok);
  assert(model.revision() == first_revision);
  assert(model.boot_line(BootStage::display) == "DISPLAY OK");
  model.set_stage_error(BootStage::wifi, 17);
  const uint32_t error_revision = model.revision();
  assert(error_revision > first_revision);
  model.set_stage_error(BootStage::wifi, 17);
  assert(model.revision() == error_revision);
  assert(model.boot_line(BootStage::wifi) == "WIFI E017");
  model.set_storage_compatibility(
      evaluate_storage_compatibility(true, true, true, 0x1e0000));

  model.set_mode(InputMode::codex_remote);
  model.set_ble(ServiceState::ok);
  model.set_wifi(ServiceState::offline);
  model.set_companion(ServiceState::ok);
  model.set_microphone(
      UiMicrophoneState::ready, 0, 0, "NONE", 1000);
  assert(model.microphone_indicator() == "MIC READY");
  assert(!model.microphone_live());
  model.set_profile("CODEX");
  model.set_web("192.168.1.88", "12345678");
  model.set_session("agent-loop", "Cardputer_Codex_Companion", "WAITING", 2, 1);
  const std::array limits{
      CodexLimitUsage{
          .scope = CodexLimitScope::codex,
          .window = CodexLimitWindow::five_hours,
          .used_percent = 38,
      },
      CodexLimitUsage{
          .scope = CodexLimitScope::spark,
          .window = CodexLimitWindow::weekly,
          .used_percent = 22,
      },
  };
  model.set_codex("gpt-5.6", "high", true, limits);
  const uint32_t pet_chrome_revision = model.pet_revision();
  model.set_heartbeat_age(1);
  assert(model.pet_revision() == pet_chrome_revision);
  model.set_pet("rocky", "0123456789abcdef", PetState::working, "ok");
  assert(model.pet_revision() > pet_chrome_revision);
  const uint32_t runtime_revision = model.revision();
  model.set_mode(InputMode::codex_remote);
  model.set_ble(ServiceState::ok);
  model.set_wifi(ServiceState::offline);
  model.set_companion(ServiceState::ok);
  model.set_profile("CODEX");
  model.set_web("192.168.1.88", "12345678");
  model.set_session("agent-loop", "Cardputer_Codex_Companion", "WAITING", 2, 1);
  model.set_codex("gpt-5.6", "high", true, limits);
  assert(model.revision() == runtime_revision);

  const std::string page = model.runtime_text();
  assert(page.find("B:OK W:OFF M:OK") != std::string::npos);
  assert(page.find("CODEX / CODEX") != std::string::npos);
  assert(page.find("IP:192.168.1.88") != std::string::npos);
  assert(page.find("PIN:12345678") != std::string::npos);
  assert(page.find("agent-loop") != std::string::npos);
  assert(page.find("WAITING A:2 I:1") != std::string::npos);
  assert(page.find("Cardputer_Codex_Companion") == std::string::npos);

  model.navigate(UiNavAction::next_page);
  assert(model.page() == UiPage::device_status);
  model.navigate(UiNavAction::next_page);
  assert(model.page() == UiPage::codex_status);
  UiPageContent codex = model.page_content();
  std::string codex_joined;
  for (uint8_t index = 0; index < codex.count; ++index) {
    codex_joined.append(codex.lines[index]).push_back('\n');
  }
  const auto session_at = codex_joined.find("SESSION:");
  const auto model_at = codex_joined.find("MODEL:");
  const auto fast_at = codex_joined.find("FAST:");
  const auto thinking_at = codex_joined.find("THINKING:");
  const auto five_at = codex_joined.find("5H:");
  const auto spark_at = codex_joined.find("SPARK WEEKLY:");
  assert(session_at < model_at);
  assert(model_at < fast_at);
  assert(fast_at < thinking_at);
  assert(thinking_at < five_at);
  assert(five_at < spark_at);
  model.navigate(UiNavAction::next_page);
  assert(model.page() == UiPage::sync_status);
  model.set_pet("rocky", "0123456789abcdef", PetState::working, "ok");
  model.set_heartbeat_age(3);
  model.set_pet_sync_age(7);
  const UiPageContent sync = model.page_content();
  std::string sync_joined;
  for (uint8_t index = 0; index < sync.count; ++index) {
    sync_joined.append(sync.lines[index]).push_back('\n');
  }
  assert(sync_joined.find("IP:192.168.1.88") != std::string::npos);
  assert(sync_joined.find("HEARTBEAT:3s") != std::string::npos);
  assert(sync_joined.find("PET SYNC:ok 7s") != std::string::npos);
  assert(sync_joined.find("PROFILE:CODEX") != std::string::npos);
  model.navigate(UiNavAction::next_page);
  assert(model.page() == UiPage::settings);
  model.navigate(UiNavAction::next_page);
  assert(model.page() == UiPage::pet);
  model.navigate(UiNavAction::previous_page);
  assert(model.page() == UiPage::settings);
  const std::array<std::string_view, 7> settings_rows{
      "> KEYBOARD PROFILE", "  CHANGE PIN", "  BIND WIFI",
      "  BRIGHTNESS 75%", "  RETURN 30S", "  PET FPS 2.5",
      "  ABOUT"};
  model.set_settings_content(settings_rows, 0, 0);
  const UiPageContent settings = model.page_content();
  assert(settings.count == settings_rows.size());
  assert(settings.lines[0] == "> KEYBOARD PROFILE");
  assert(settings.lines[5] == "  PET FPS 2.5");

  model.set_pet_storage(25772, 1);
  model.navigate(UiNavAction::next_page);
  model.navigate(UiNavAction::next_page);
  const UiPageContent device = model.page_content();
  assert(device.count == 7);
  assert(device.lines[0] == "VERSION:1.3.0");
  assert(device.lines[1] == "PIN:12345678");
  assert(device.lines[2] == "STORAGE:READY");
  assert(device.lines[3] == "BLE:OK");
  assert(device.lines[4] == "WIFI:OFF");
  assert(device.lines[5] == "AGENT:OK");
  assert(device.lines[6] == "MIC:READY");
  std::string joined;
  for (uint8_t index = 0; index < device.count; ++index) {
    joined.append(device.lines[index]).push_back('\n');
  }
  assert(joined.find("VERSION:1.3.0") != std::string::npos);
  assert(joined.find("PIN:12345678") != std::string::npos);
  assert(joined.find("STORAGE:READY") != std::string::npos);
  assert(joined.find("********") == std::string::npos);
  assert(joined.find("BLE:OK") != std::string::npos);
  assert(joined.find("WIFI:OFF") != std::string::npos);
  assert(joined.find("AGENT:OK") != std::string::npos);
  assert(joined.find("MIC:READY") != std::string::npos);
  model.navigate(UiNavAction::scroll_down);
  assert(model.scroll_offset() <= device.count);

  model.set_microphone(
      UiMicrophoneState::live24, 24000, 1, "NONE", 2000);
  assert(model.microphone_indicator() == "MIC 24K");
  assert(model.microphone_live());
  model.set_microphone(
      UiMicrophoneState::live16, 16000, 2, "NONE", 2100);
  assert(model.microphone_indicator() == "MIC 16K");
  model.set_microphone(
      UiMicrophoneState::error, 0, 2, "MIC INIT FAILED", 2200);
  assert(model.microphone_indicator() == "MIC ERR");
  assert(model.microphone_error() == "MIC INIT FAILED");
  model.expire_microphone_error(3199);
  assert(model.microphone_error() == "MIC INIT FAILED");
  model.expire_microphone_error(3200);
  assert(model.microphone_error().empty());
  model.set_microphone(
      UiMicrophoneState::unavailable, 0, 0, "NONE", 3300);
  assert(model.microphone_indicator() == "MIC --");

  model.navigate(UiNavAction::next_page);
  model.set_codex("gpt-5.6", "high", false, {});
  codex = model.page_content();
  codex_joined.clear();
  for (uint8_t index = 0; index < codex.count; ++index) {
    codex_joined.append(codex.lines[index]).push_back('\n');
  }
  assert(codex_joined.find("MODEL:gpt-5.6") != std::string::npos);
  assert(codex_joined.find("FAST:OFF") != std::string::npos);
  assert(codex_joined.find("5H") == std::string::npos);
  assert(codex_joined.find("WEEKLY") == std::string::npos);
  assert(codex_joined.find("N/A") == std::string::npos);
  assert(codex_joined.find("NA") == std::string::npos);
  return 0;
}
