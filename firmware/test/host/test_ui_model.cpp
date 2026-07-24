#include <cassert>
#include <string>

#include "product/ui_model.hpp"

int main() {
  UiModel model;
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

  model.set_mode(InputMode::codex_remote);
  model.set_ble(ServiceState::ok);
  model.set_wifi(ServiceState::offline);
  model.set_companion(ServiceState::ok);
  model.set_profile("CODEX");
  model.set_web("192.168.1.88", "12345678");
  model.set_session("agent-loop", "Cardputer_Codex_Companion", "WAITING", 2, 1);
  const uint32_t runtime_revision = model.revision();
  model.set_mode(InputMode::codex_remote);
  model.set_ble(ServiceState::ok);
  model.set_wifi(ServiceState::offline);
  model.set_companion(ServiceState::ok);
  model.set_profile("CODEX");
  model.set_web("192.168.1.88", "12345678");
  model.set_session("agent-loop", "Cardputer_Codex_Companion", "WAITING", 2, 1);
  assert(model.revision() == runtime_revision);

  const std::string page = model.runtime_text();
  assert(page.find("BLE:OK") != std::string::npos);
  assert(page.find("WIFI:OFFLINE") != std::string::npos);
  assert(page.find("MAC:OK") != std::string::npos);
  assert(page.find("CODEX") != std::string::npos);
  assert(page.find("192.168.1.88") != std::string::npos);
  assert(page.find("PIN:12345678") != std::string::npos);
  assert(page.find("agent-loop") != std::string::npos);
  assert(page.find("WAITING A:2 I:1") != std::string::npos);
  return 0;
}
