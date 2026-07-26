#include "product/display.hpp"

#include <algorithm>
#include <array>

#include "M5Unified.h"

namespace {
constexpr uint32_t kBackground = 0x05080d;
constexpr uint32_t kForeground = 0xe7edf5;
constexpr uint32_t kAccent = 0x4fd1c5;
constexpr uint32_t kMicrophoneLive = 0xff4040;
constexpr uint32_t kMicrophoneDegraded = 0xffa726;
constexpr uint8_t kDisplayTitleTextSize = 1;
constexpr uint8_t kDisplayBodyTextSize = 2;
constexpr int32_t kPetX = 72;
constexpr int32_t kPetY = 20;
constexpr int32_t kPetWidth = 96;
constexpr int32_t kPetHeight = 104;

bool draw_pet_row(
    void*, std::size_t row,
    std::span<const uint16_t, kPetFrameWidth> pixels) {
  M5.Display.pushImage(kPetX, kPetY + static_cast<int32_t>(row),
                       kPetFrameWidth, 1, pixels.data());
  return true;
}

void begin_page(const char* title) {
  M5.Display.startWrite();
  M5.Display.fillScreen(kBackground);
  M5.Display.setTextColor(kAccent, kBackground);
  M5.Display.setTextSize(kDisplayTitleTextSize);
  M5.Display.setCursor(6, 5);
  M5.Display.print(title);
  M5.Display.drawFastHLine(6, 17, 228, kAccent);
  M5.Display.setTextColor(kForeground, kBackground);
  M5.Display.setTextSize(kDisplayBodyTextSize);
  M5.Display.setCursor(0, 20);
}

const char* pet_state_name(PetState state) {
  switch (state) {
    case PetState::idle: return "IDLE";
    case PetState::working: return "WORKING";
    case PetState::waiting: return "WAITING";
    case PetState::review: return "REVIEW";
    case PetState::failed: return "FAILED";
  }
  return "IDLE";
}

const char* page_title(UiPage page) {
  switch (page) {
    case UiPage::pet: return "PET";
    case UiPage::device_status: return "DEVICE";
    case UiPage::codex_status: return "CODEX";
    case UiPage::sync_status: return "SYNC";
    case UiPage::settings: return "SETTINGS";
  }
  return "PET";
}

void draw_page_dots(UiPage active) {
  constexpr int centers[] = {100, 110, 120, 130, 140};
  for (int index = 0; index < 5; ++index) {
    const uint32_t color =
        index == static_cast<int>(active) ? kAccent : 0x39505a;
    M5.Display.fillCircle(centers[index], 131, 2, color);
  }
}

void draw_microphone_status(const UiModel& model, uint32_t background) {
  uint32_t color = kForeground;
  if (model.microphone_state() == UiMicrophoneState::live24 ||
      model.microphone_state() == UiMicrophoneState::error) {
    color = kMicrophoneLive;
  } else if (model.microphone_state() == UiMicrophoneState::live16) {
    color = kMicrophoneDegraded;
  }
  M5.Display.setTextColor(color, background);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(181, 5);
  M5.Display.print(model.microphone_indicator().data());
}

void draw_microphone_error(const UiModel& model) {
  if (model.microphone_error().empty()) return;
  M5.Display.fillRect(0, 116, 240, 12, kBackground);
  M5.Display.setTextColor(kMicrophoneLive, kBackground);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(5, 118);
  M5.Display.print(model.microphone_error().data());
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
  M5.Display.startWrite();
  M5.Display.fillScreen(kBackground);
  M5.Display.setTextColor(kAccent, kBackground);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(6, 3);
  M5.Display.print("Cardputer Codex Companion");
  M5.Display.setCursor(6, 13);
  M5.Display.printf("v%.*s", static_cast<int>(kProductVersion.size()),
                    kProductVersion.data());
  M5.Display.drawFastHLine(6, 24, 228, kAccent);
  M5.Display.setTextColor(kForeground, kBackground);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(6, 27);
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

void display_render_page(const UiModel& model) {
  if (model.page() == UiPage::pet) {
    M5.Display.startWrite();
    M5.Display.fillScreen(kBackground);
    M5.Display.fillRect(0, 0, 240, 18, 0x0d1820);
    M5.Display.setTextColor(kAccent, 0x0d1820);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(5, 5);
    const PetState effective =
        model.companion() == ServiceState::ok
            ? model.pet_state()
            : PetState::waiting;
    M5.Display.print(pet_state_name(effective));
    M5.Display.setTextColor(kForeground, 0x0d1820);
    M5.Display.setCursor(118, 5);
    M5.Display.printf(
        "B%sW%sM%s",
        model.ble() == ServiceState::ok ? "+" : "-",
        model.wifi() == ServiceState::ok ? "+" : "-",
        model.companion() == ServiceState::ok ? "+" : "-");
    draw_microphone_status(model, 0x0d1820);
    if (model.microphone_live()) {
      M5.Display.fillCircle(174, 8, 3, kMicrophoneLive);
    }
    draw_microphone_error(model);
    draw_page_dots(model.page());
    M5.Display.endWrite();
    display_render_placeholder(effective);
    return;
  }
  begin_page(page_title(model.page()));
  draw_microphone_status(model, kBackground);
  const UiPageContent content = model.page_content();
  const uint8_t visible = model.page() == UiPage::settings ? 5 : 6;
  const uint8_t end = std::min<uint8_t>(
      content.count, model.scroll_offset() + visible);
  for (uint8_t index = model.scroll_offset(); index < end; ++index) {
    M5.Display.println(content.lines[index].c_str());
  }
  if (model.scroll_offset() > 0) {
    M5.Display.fillTriangle(228, 23, 223, 29, 233, 29, kAccent);
  }
  if (end < content.count) {
    M5.Display.fillTriangle(223, 116, 233, 116, 228, 122, kAccent);
  }
  draw_page_dots(model.page());
  draw_microphone_error(model);
  M5.Display.endWrite();
}

bool display_render_pet_frame(PetStore& store, PetState state,
                              uint8_t frame_index) {
  M5.Display.startWrite();
  const bool previous_swap = M5.Display.getSwapBytes();
  M5.Display.setSwapBytes(true);
  const bool decoded =
      store.decode_rows(state, frame_index, draw_pet_row, nullptr);
  M5.Display.setSwapBytes(previous_swap);
  M5.Display.endWrite();
  return decoded;
}

void display_render_placeholder(PetState state) {
  M5.Display.startWrite();
  M5.Display.fillRect(kPetX, kPetY, kPetWidth, kPetHeight, kBackground);
  const uint32_t color =
      state == PetState::failed ? 0xff6b6b : kAccent;
  M5.Display.fillRoundRect(kPetX + 25, kPetY + 28, 46, 46, 12, color);
  M5.Display.fillCircle(kPetX + 40, kPetY + 47, 3, kBackground);
  M5.Display.fillCircle(kPetX + 56, kPetY + 47, 3, kBackground);
  M5.Display.drawFastHLine(kPetX + 41, kPetY + 61, 14, kBackground);
  M5.Display.endWrite();
}
