#include "product/display.hpp"

#include <array>

#include "M5Unified.h"

namespace {
constexpr uint32_t kBackground = 0x05080d;
constexpr uint32_t kForeground = 0xe7edf5;
constexpr uint32_t kAccent = 0x4fd1c5;

void begin_page(const char* title) {
  M5.Display.startWrite();
  M5.Display.fillScreen(kBackground);
  M5.Display.setTextColor(kAccent, kBackground);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(6, 5);
  M5.Display.print(title);
  M5.Display.drawFastHLine(6, 18, 228, kAccent);
  M5.Display.setTextColor(kForeground, kBackground);
  M5.Display.setCursor(6, 24);
}
}  // namespace

esp_err_t display_start(UiModel* model) {
  if (model == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  auto config = M5.config();
  config.clear_display = true;
  M5.begin(config);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(128);
  model->set_stage(BootStage::display, ServiceState::ok);
  display_render_boot(*model);
  return ESP_OK;
}

void display_render_boot(const UiModel& model) {
  begin_page("CARDPUTER CODEX 1.0");
  constexpr std::array stages{
      BootStage::display, BootStage::config, BootStage::keyboard,
      BootStage::ble, BootStage::wifi, BootStage::web,
      BootStage::companion,
  };
  for (BootStage stage : stages) {
    M5.Display.println(model.boot_line(stage).c_str());
  }
  M5.Display.endWrite();
}

void display_render_runtime(const UiModel& model) {
  begin_page("CODEX REMOTE");
  M5.Display.print(model.runtime_text().c_str());
  M5.Display.endWrite();
}
