#include <cassert>
#include <string>

#include "product/ui_model.hpp"

int main() {
  UiModel model;
  assert(model.page() == UiPage::pet);
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
  assert(page.find("B:OK W:OFF M:OK") != std::string::npos);
  assert(page.find("CODEX / CODEX") != std::string::npos);
  assert(page.find("IP:192.168.1.88") != std::string::npos);
  assert(page.find("PIN:12345678") != std::string::npos);
  assert(page.find("agent-loop") != std::string::npos);
  assert(page.find("WAITING A:2 I:1") != std::string::npos);
  assert(page.find("Cardputer_Codex_Companion") == std::string::npos);

  model.navigate(UiNavAction::next_page);
  assert(model.page() == UiPage::connection);
  model.navigate(UiNavAction::next_page);
  assert(model.page() == UiPage::session);
  model.navigate(UiNavAction::next_page);
  assert(model.page() == UiPage::device);
  model.navigate(UiNavAction::next_page);
  assert(model.page() == UiPage::pet);
  model.navigate(UiNavAction::previous_page);
  assert(model.page() == UiPage::device);

  model.set_pet("rocky", "0123456789abcdef", PetState::working, "ok");
  model.set_pet_storage(25772, 1);
  const UiPageContent device = model.page_content();
  assert(device.count >= 5);
  std::string joined;
  for (uint8_t index = 0; index < device.count; ++index) {
    joined.append(device.lines[index]).push_back('\n');
  }
  assert(joined.find("FW:1.0.28") != std::string::npos);
  assert(joined.find("PET:rocky") != std::string::npos);
  assert(joined.find("FMT:1") != std::string::npos);
  model.navigate(UiNavAction::scroll_down);
  assert(model.scroll_offset() <= device.count);
  return 0;
}
