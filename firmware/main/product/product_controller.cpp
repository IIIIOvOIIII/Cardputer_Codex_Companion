#include "product/product_controller.hpp"

void ProductController::start() {
  const std::array<bool (ProductStartupBackend::*)(), 7> steps{{
      &ProductStartupBackend::display,
      &ProductStartupBackend::config,
      &ProductStartupBackend::keyboard,
      &ProductStartupBackend::ble,
      &ProductStartupBackend::wifi,
      &ProductStartupBackend::web,
      &ProductStartupBackend::companion,
  }};
  for (std::size_t index = 0; index < steps.size(); ++index) {
    const bool ok = (backend_.*steps[index])();
    const BootStage stage = static_cast<BootStage>(index);
    states_[index] =
        ok ? ServiceState::ok
           : (stage == BootStage::wifi || stage == BootStage::companion
                  ? ServiceState::offline
                  : ServiceState::error);
  }
}

#ifdef ESP_PLATFORM

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "M5Unified.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "probe/ble_services.hpp"
#include "probe/keyboard_probe.hpp"
#include "product/companion_protocol.hpp"
#include "product/display.hpp"
#include "product/input_router.hpp"
#include "product/keyboard_matrix.hpp"
#include "product/macro_engine.hpp"
#include "product/product_web.hpp"
#include "product/ui_model.hpp"
#include "product/wifi_manager.hpp"

namespace {
constexpr char kTag[] = "cardputer-product";
constexpr std::size_t kMacroQueueDepth = 16;

struct MacroInvocation {
  uint8_t layer = 0;
  uint8_t physical_key = 0;
};

UiModel g_ui;
InputRouter g_input_router;
CompanionProtocol g_companion_protocol;
std::optional<KeyboardProbe> g_keyboard;
std::array<bool, kPhysicalKeyCount> g_pressed{};
StaticSemaphore_t g_ui_mutex_storage{};
SemaphoreHandle_t g_ui_mutex = nullptr;
StaticSemaphore_t g_input_mutex_storage{};
SemaphoreHandle_t g_input_mutex = nullptr;
StaticQueue_t g_macro_queue_storage{};
std::array<uint8_t, kMacroQueueDepth * sizeof(MacroInvocation)>
    g_macro_queue_buffer{};
QueueHandle_t g_macro_queue = nullptr;
StaticTask_t g_macro_task_storage{};
std::array<StackType_t, 6144> g_macro_task_stack{};
StaticTask_t g_ui_task_storage{};
std::array<StackType_t, 4096> g_ui_task_stack{};
ServiceState g_ble_state = ServiceState::offline;
ServiceState g_wifi_state = ServiceState::offline;
ServiceState g_companion_state = ServiceState::offline;

class SemaphoreLock {
 public:
  explicit SemaphoreLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
    locked_ = mutex_ != nullptr &&
              xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) == pdTRUE;
  }
  ~SemaphoreLock() {
    if (locked_) xSemaphoreGive(mutex_);
  }
  [[nodiscard]] bool locked() const { return locked_; }

 private:
  SemaphoreHandle_t mutex_ = nullptr;
  bool locked_ = false;
};

void update_web_status() {
  product_web_set_status(g_ble_state, g_wifi_state, g_companion_state);
}

void release_and_set_mode(bool safe) {
  SemaphoreLock lock(g_input_mutex);
  if (!lock.locked()) return;
  if (g_keyboard.has_value()) g_keyboard->on_mode_changed();
  if (safe) {
    g_input_router.enter_safe_profile();
  } else {
    g_input_router.toggle_mode();
  }
  SemaphoreLock ui_lock(g_ui_mutex);
  if (ui_lock.locked()) {
    g_ui.set_mode(g_input_router.mode());
    if (safe) g_ui.set_profile("SAFE");
  }
}

class ProductMacroSink final : public MacroSink {
 public:
  void send_hid(const HidReport& report) override {
    if (g_keyboard.has_value() && ble_keyboard_ready()) {
      g_keyboard->send_complete_report(report);
    }
  }

  bool send_text(uint32_t operation_id, std::string_view text) override {
    const auto bytes = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(text.data()), text.size());
    return notify_product_utf8(operation_id, bytes) == ESP_OK;
  }

  void device_action(DeviceAction action) override {
    switch (action) {
      case DeviceAction::toggle_mode:
        release_and_set_mode(false);
        break;
      case DeviceAction::open_pairing: {
        SemaphoreLock lock(g_ui_mutex);
        if (lock.locked()) {
          g_ui.set_web(product_wifi_ipv4(), product_web_pairing_code());
        }
        break;
      }
      case DeviceAction::reconnect_wifi:
        product_wifi_reconnect();
        break;
      case DeviceAction::next_profile:
      case DeviceAction::previous_profile:
      case DeviceAction::none:
        break;
    }
  }

  bool codex_action(CodexAction action) override {
    if (action == CodexAction::none) return false;
    product_web_queue_codex_action(action);
    return true;
  }
};

ProductMacroSink g_macro_sink;
MacroEngine g_macro_engine(g_macro_sink);

void macro_task(void*) {
  MacroInvocation invocation;
  while (true) {
    if (xQueueReceive(g_macro_queue, &invocation, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    KeyAction action;
    if (product_web_action(invocation.layer, invocation.physical_key,
                           &action)) {
      g_macro_engine.execute(action);
    }
  }
}

uint8_t current_profile_layer_locked() {
  return active_profile_layer(g_input_router.mode(),
                              g_pressed[kFnPhysicalKey]);
}

void send_passthrough_report_locked() {
  if (!g_keyboard.has_value() || !ble_keyboard_ready()) return;
  std::array<bool, kPhysicalKeyCount> passthrough = g_pressed;
  if (!g_input_router.safe_profile()) {
    const uint8_t layer = current_profile_layer_locked();
    for (uint8_t key = 0; key < kPhysicalKeyCount; ++key) {
      if (!passthrough[key]) continue;
      KeyAction action;
      if (product_web_action(layer, key, &action) &&
          action.kind != ActionKind::passthrough) {
        passthrough[key] = false;
      }
    }
  }
  g_keyboard->send_complete_report(g_input_router.route(passthrough));
}

void keyboard_event(const MatrixKeyEvent& event) {
  SemaphoreLock lock(g_input_mutex);
  if (!lock.locked() || event.physical_key >= g_pressed.size()) return;
  g_pressed[event.physical_key] = event.pressed;

  if (event.pressed && ble_pairing_input_active()) {
    const auto digit =
        ble_pairing_digit_from_hid_usage(kPhysicalKeymap[event.physical_key].usage);
    if (digit.has_value()) {
      ble_pairing_input_digit(*digit);
      SemaphoreLock ui_lock(g_ui_mutex);
      if (ui_lock.locked()) {
        g_ui.set_session("TYPE MAC PIN", "CARDPUTER KEYS",
                         "PAIRING", 0, 0);
      }
      return;
    }
  }

  KeyAction action;
  const uint8_t layer = current_profile_layer_locked();
  const bool mapped =
      !g_input_router.safe_profile() &&
      product_web_action(layer, event.physical_key, &action) &&
      action.kind != ActionKind::passthrough;
  ESP_LOGI(kTag, "keyboard key=%u %s layer=%u mapped=%d",
           static_cast<unsigned>(event.physical_key),
           event.pressed ? "down" : "up",
           static_cast<unsigned>(layer),
           mapped ? 1 : 0);
  if (event.pressed && mapped && g_macro_queue != nullptr) {
    const MacroInvocation invocation{
        .layer = layer,
        .physical_key = event.physical_key,
    };
    xQueueSend(g_macro_queue, &invocation, 0);
  }
  send_passthrough_report_locked();
}

void ble_connection_changed(bool connected) {
  g_ble_state = connected ? ServiceState::ok : ServiceState::offline;
  {
    SemaphoreLock lock(g_ui_mutex);
    if (lock.locked()) g_ui.set_ble(g_ble_state);
  }
  update_web_status();
}

void ble_disconnected() {
  if (g_keyboard.has_value()) g_keyboard->on_ble_disconnected();
}

void wifi_status_changed(WifiState state, const char* detail) {
  switch (state) {
    case WifiState::online:
      g_wifi_state = ServiceState::ok;
      break;
    case WifiState::connecting:
      g_wifi_state = ServiceState::starting;
      break;
    case WifiState::provisioning:
      g_wifi_state = ServiceState::offline;
      break;
    case WifiState::idle:
    case WifiState::offline:
      g_wifi_state = ServiceState::offline;
      break;
  }
  {
    SemaphoreLock lock(g_ui_mutex);
    if (lock.locked()) {
      g_ui.set_wifi(g_wifi_state);
      g_ui.set_web(state == WifiState::online && detail != nullptr
                       ? detail
                       : product_wifi_ipv4(),
                   product_web_pairing_code());
      if (state == WifiState::provisioning && detail != nullptr) {
        g_ui.set_session("WIFI SETUP AP",
                         std::string("PASS:") + detail,
                         "https://192.168.4.1", 0, 0);
      }
    }
  }
  update_web_status();
}

void companion_snapshot(std::string_view json) {
  const uint64_t now_ms =
      static_cast<uint64_t>(esp_timer_get_time()) / 1000;
  const CompanionMessageResult result =
      g_companion_protocol.apply(json, now_ms);
  if (result != CompanionMessageResult::snapshot) return;
  const CompanionSnapshot& snapshot = g_companion_protocol.snapshot();
  g_companion_state = ServiceState::ok;
  {
    SemaphoreLock lock(g_ui_mutex);
    if (lock.locked()) {
      g_ui.set_companion(ServiceState::ok);
      g_ui.set_session(snapshot.title, snapshot.cwd, snapshot.state,
                       snapshot.approvals, snapshot.inputs);
    }
  }
  update_web_status();
}

void ui_task(void*) {
  bool long_press_handled = false;
  TickType_t wake = xTaskGetTickCount();
  uint32_t rendered_revision = UINT32_MAX;
  while (true) {
    M5.update();
    if (!long_press_handled && M5.BtnA.pressedFor(2000)) {
      release_and_set_mode(true);
      long_press_handled = true;
    }
    if (M5.BtnA.wasReleased()) {
      if (!long_press_handled) release_and_set_mode(false);
      long_press_handled = false;
    }

    const uint64_t now_ms =
        static_cast<uint64_t>(esp_timer_get_time()) / 1000;
    if (g_companion_state == ServiceState::ok &&
        g_companion_protocol.stale(now_ms)) {
      g_companion_state = ServiceState::offline;
      SemaphoreLock lock(g_ui_mutex);
      if (lock.locked()) {
        g_ui.set_companion(ServiceState::offline);
        g_ui.set_session("NO ACTIVE CODEX", "-", "STALE", 0, 0);
      }
      update_web_status();
    }

    {
      SemaphoreLock lock(g_ui_mutex);
      if (lock.locked()) {
        std::array<char, 21> profile_name{};
        if (product_web_profile_name(profile_name.data(),
                                     profile_name.size())) {
          g_ui.set_profile(profile_name.data());
        }
        g_ui.set_web(product_wifi_ipv4(), product_web_pairing_code());
        const uint32_t revision = g_ui.revision();
        if (revision != rendered_revision) {
          display_render_runtime(g_ui);
          rendered_revision = revision;
        }
      }
    }
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(200));
  }
}

class EspProductStartup final : public ProductStartupBackend {
 public:
  bool display() override {
    const bool ok = display_start(&g_ui) == ESP_OK;
    return ok;
  }

  bool config() override {
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      result = nvs_flash_erase();
      if (result == ESP_OK) result = nvs_flash_init();
    }
    set_stage(BootStage::config, result);
    return result == ESP_OK;
  }

  bool keyboard() override {
    g_macro_queue = xQueueCreateStatic(
        kMacroQueueDepth, sizeof(MacroInvocation), g_macro_queue_buffer.data(),
        &g_macro_queue_storage);
    if (g_macro_queue == nullptr) {
      set_stage(BootStage::keyboard, ESP_ERR_NO_MEM);
      return false;
    }
    const TaskHandle_t macro = xTaskCreateStatic(
        macro_task, "product-macro", g_macro_task_stack.size(), nullptr,
        tskIDLE_PRIORITY + 2, g_macro_task_stack.data(), &g_macro_task_storage);
    esp_err_t result = macro == nullptr ? ESP_ERR_NO_MEM
                                        : keyboard_matrix_start(keyboard_event);
    set_stage(BootStage::keyboard, result);
    return result == ESP_OK;
  }

  bool ble() override {
    DeviceId device_id{};
    esp_err_t result = load_or_create_device_id(&device_id);
    esp_hidd_dev_t* hid_device = nullptr;
    if (result == ESP_OK) result = initialize_ble(device_id, &hid_device);
    if (result == ESP_OK) {
      g_keyboard.emplace(hid_device);
      enable_product_companion_mode();
      set_ble_disconnect_handler(ble_disconnected);
      set_ble_connection_handler(ble_connection_changed);
      g_ble_state = ServiceState::offline;
      SemaphoreLock lock(g_ui_mutex);
      if (lock.locked()) {
        g_ui.set_ble(g_ble_state);
        g_ui.set_session("WAITING FOR MAC", "CONNECT BLE", "PAIRING", 0, 0);
      }
    }
    set_stage(BootStage::ble, result);
    return result == ESP_OK;
  }

  bool wifi() override {
    const bool recovery_mode = M5.BtnA.isPressed();
    const esp_err_t result =
        product_wifi_start(recovery_mode, wifi_status_changed);
    set_stage(BootStage::wifi, result);
    return result == ESP_OK;
  }

  bool web() override {
    product_web_set_companion_snapshot_handler(companion_snapshot);
    const esp_err_t result = product_web_start();
    if (result == ESP_OK) {
      SemaphoreLock lock(g_ui_mutex);
      if (lock.locked()) {
        g_ui.set_web(product_wifi_ipv4(), product_web_pairing_code());
      }
    }
    set_stage(BootStage::web, result);
    return result == ESP_OK;
  }

  bool companion() override {
    g_companion_state = ServiceState::offline;
    {
      SemaphoreLock lock(g_ui_mutex);
      if (lock.locked()) {
        g_ui.set_companion(ServiceState::offline);
        g_ui.set_stage(BootStage::companion, ServiceState::offline);
        display_render_boot(g_ui);
      }
    }
    const TaskHandle_t ui = xTaskCreateStatic(
        ui_task, "product-ui", g_ui_task_stack.size(), nullptr,
        tskIDLE_PRIORITY + 1, g_ui_task_stack.data(), &g_ui_task_storage);
    update_web_status();
    return ui != nullptr;
  }

 private:
  void set_stage(BootStage stage, esp_err_t result) {
    SemaphoreLock lock(g_ui_mutex);
    if (!lock.locked()) return;
    if (result == ESP_OK) {
      g_ui.set_stage(stage, ServiceState::ok);
    } else {
      g_ui.set_stage_error(stage,
                           static_cast<uint16_t>(result < 0 ? -result : result));
    }
    display_render_boot(g_ui);
  }
};
}  // namespace

void product_runtime_start() {
  g_ui_mutex = xSemaphoreCreateMutexStatic(&g_ui_mutex_storage);
  g_input_mutex = xSemaphoreCreateMutexStatic(&g_input_mutex_storage);
  if (g_ui_mutex == nullptr || g_input_mutex == nullptr) {
    ESP_LOGE(kTag, "runtime mutex initialization failed");
    return;
  }
  static EspProductStartup startup;
  static ProductController controller(startup);
  controller.start();
  ESP_LOGI(kTag, "product runtime started");
}

#endif
