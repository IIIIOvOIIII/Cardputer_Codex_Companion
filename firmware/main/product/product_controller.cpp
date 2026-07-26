#include "product/product_controller.hpp"

void ProductController::start() {
  struct StartupStep {
    BootStage stage;
    bool (ProductStartupBackend::*run)();
  };
  const std::array<StartupStep, 7> steps{{
      {BootStage::display, &ProductStartupBackend::display},
      {BootStage::config, &ProductStartupBackend::config},
      {BootStage::keyboard, &ProductStartupBackend::keyboard},
      {BootStage::ble, &ProductStartupBackend::ble},
      {BootStage::wifi, &ProductStartupBackend::wifi},
      {BootStage::web, &ProductStartupBackend::web},
      {BootStage::companion, &ProductStartupBackend::companion},
  }};
  for (const StartupStep& step : steps) {
    const bool ok = (backend_.*step.run)();
    states_[static_cast<std::size_t>(step.stage)] =
        ok ? ServiceState::ok
           : (step.stage == BootStage::wifi ||
                      step.stage == BootStage::companion
                  ? ServiceState::offline
                  : ServiceState::error);
  }
}

#ifdef ESP_PLATFORM

#include <algorithm>
#include <atomic>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "M5Unified.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "probe/ble_services.hpp"
#include "probe/keyboard_probe.hpp"
#include "product/companion_protocol.hpp"
#include "product/display.hpp"
#include "product/device_settings.hpp"
#include "product/input_router.hpp"
#include "product/keyboard_matrix.hpp"
#include "product/macro_engine.hpp"
#include "product/pet_store.hpp"
#include "product/profile_catalog.hpp"
#include "product/product_web.hpp"
#include "product/settings_menu.hpp"
#include "product/ui_model.hpp"
#include "product/ui_navigation.hpp"
#include "product/wifi_manager.hpp"

namespace {
constexpr char kTag[] = "cardputer-product";
constexpr std::size_t kMacroQueueDepth = 16;
constexpr std::size_t kSettingsQueueDepth = 4;
constexpr char kSettingsNvsNamespace[] = "product";

struct MacroInvocation {
  uint8_t layer = 0;
  uint8_t physical_key = 0;
};

struct SettingsInvocation {
  SettingsCommandKind command = SettingsCommandKind::none;
  uint8_t selected = 0;
  DeviceSettings device_settings{};
  std::array<char, 9> pin{};
  std::array<char, 9> profile_id{};
  std::array<char, 33> ssid{};
  std::array<char, 65> password{};
};

class NvsDeviceSettingsBackend final : public DeviceSettingsBackend {
 public:
  bool load(std::span<uint8_t> output) override {
    nvs_handle_t handle;
    if (nvs_open(kSettingsNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
      return false;
    }
    size_t size = output.size();
    const esp_err_t result =
        nvs_get_blob(handle, kDeviceSettingsStorageKey.data(),
                     output.data(), &size);
    nvs_close(handle);
    return result == ESP_OK && size == output.size();
  }

  bool commit(std::span<const uint8_t> input) override {
    nvs_handle_t handle;
    if (nvs_open(kSettingsNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
      return false;
    }
    esp_err_t result =
        nvs_set_blob(handle, kDeviceSettingsStorageKey.data(),
                     input.data(), input.size());
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result != ESP_OK) {
      ESP_LOGE(kTag, "device settings NVS commit failed: %s",
               esp_err_to_name(result));
    }
    return result == ESP_OK;
  }
};

UiModel g_ui;
InputRouter g_input_router;
UiNavigation g_ui_navigation;
CompanionProtocol g_companion_protocol;
PetStore g_pet_store;
EspProfileCatalogBackend g_profile_catalog_backend;
ProfileCatalogStore g_profile_catalog(g_profile_catalog_backend);
NvsDeviceSettingsBackend g_device_settings_backend;
DeviceSettingsStore g_device_settings_store(g_device_settings_backend);
SettingsMenu g_settings;
std::optional<KeyboardProbe> g_keyboard;
std::array<bool, kPhysicalKeyCount> g_pressed{};
StaticSemaphore_t g_ui_mutex_storage{};
SemaphoreHandle_t g_ui_mutex = nullptr;
StaticSemaphore_t g_input_mutex_storage{};
SemaphoreHandle_t g_input_mutex = nullptr;
StaticSemaphore_t g_companion_mutex_storage{};
SemaphoreHandle_t g_companion_mutex = nullptr;
StaticQueue_t g_macro_queue_storage{};
std::array<uint8_t, kMacroQueueDepth * sizeof(MacroInvocation)>
    g_macro_queue_buffer{};
QueueHandle_t g_macro_queue = nullptr;
StaticQueue_t g_settings_queue_storage{};
std::array<uint8_t, kSettingsQueueDepth * sizeof(SettingsInvocation)>
    g_settings_queue_buffer{};
QueueHandle_t g_settings_queue = nullptr;
StaticTask_t g_macro_task_storage{};
std::array<StackType_t, 6144> g_macro_task_stack{};
StaticTask_t g_ui_task_storage{};
std::array<StackType_t, 4096> g_ui_task_stack{};
TaskHandle_t g_profile_catalog_task_handle = nullptr;
StaticSemaphore_t g_profile_catalog_initialization_done_storage{};
SemaphoreHandle_t g_profile_catalog_initialization_done = nullptr;
std::atomic<bool> g_profile_catalog_initialization_complete{false};
ServiceState g_ble_state = ServiceState::offline;
ServiceState g_wifi_state = ServiceState::offline;
std::atomic<ServiceState> g_companion_state{ServiceState::offline};
std::atomic<uint64_t> g_last_companion_ms{0};
std::atomic<uint64_t> g_last_ui_input_ms{0};
std::atomic<uint32_t> g_pet_frame_interval_ms{400};
std::atomic<uint32_t> g_return_to_pet_ms{30000};

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
  product_web_set_status(g_ble_state, g_wifi_state, g_companion_state.load());
}

void copy_setting(std::string_view value, std::span<char> output) {
  if (output.empty()) return;
  const std::size_t length = std::min(value.size(), output.size() - 1);
  std::copy_n(value.data(), length, output.data());
  output[length] = '\0';
}

void update_settings_ui_locked() {
  const SettingsMenuContent content = g_settings.content();
  std::array<std::string_view, 12> rows{};
  for (uint8_t index = 0; index < content.count; ++index) {
    rows[index] = content.lines[index];
  }
  SemaphoreLock ui_lock(g_ui_mutex);
  if (ui_lock.locked()) {
    g_ui.set_settings_content(
        std::span<const std::string_view>(rows.data(), content.count),
        content.selected, content.scroll);
  }
}

void refresh_profile_choices_locked() {
  std::array<ProfileSummary, 5> profiles{};
  std::size_t count = 0;
  if (!product_web_profile_summaries(profiles, &count)) return;
  std::array<std::string_view, 5> ids{};
  std::array<std::string_view, 5> names{};
  for (std::size_t index = 0; index < count; ++index) {
    ids[index] = profiles[index].id.data();
    names[index] = profiles[index].name.data();
  }
  g_settings.set_profile_choices(
      std::span<const std::string_view>(ids.data(), count),
      std::span<const std::string_view>(names.data(), count));
}

bool queue_settings_command(const SettingsInputResult& result) {
  if (result.command == SettingsCommandKind::none ||
      result.command == SettingsCommandKind::release_hid) {
    return true;
  }
  if (g_settings_queue == nullptr) {
    ESP_LOGE(kTag, "settings command queue unavailable");
    return false;
  }
  SettingsInvocation invocation{
      .command = result.command,
      .selected = g_settings.selected(),
      .device_settings = result.device_settings,
  };
  copy_setting(g_settings.pin_value(), invocation.pin);
  copy_setting(g_settings.profile_id(), invocation.profile_id);
  copy_setting(g_settings.ssid_value(), invocation.ssid);
  copy_setting(g_settings.password_value(), invocation.password);
  if (xQueueSend(g_settings_queue, &invocation, 0) != pdTRUE) {
    ESP_LOGE(kTag, "settings command queue full: command=%u",
             static_cast<unsigned>(result.command));
    return false;
  }
  return true;
}

void wifi_scan_finished(std::span<const WifiScanEntry> entries) {
  SemaphoreLock lock(g_input_mutex);
  if (!lock.locked()) return;
  std::array<std::string_view, 12> ssids{};
  const std::size_t count = std::min(entries.size(), ssids.size());
  for (std::size_t index = 0; index < count; ++index) {
    ssids[index] = entries[index].ssid.data();
  }
  g_settings.set_wifi_choices(
      std::span<const std::string_view>(ssids.data(), count));
  update_settings_ui_locked();
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

  void delay_hid(uint16_t delay_ms) override {
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
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
        product_web_cycle_profile(true);
        break;
      case DeviceAction::previous_profile:
        product_web_cycle_profile(false);
        break;
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

void apply_runtime_settings(const DeviceSettings& settings) {
  M5.Display.setBrightness(device_brightness(settings));
  g_pet_frame_interval_ms.store(
      device_pet_frame_interval_ms(settings));
  g_return_to_pet_ms.store(device_return_timeout_ms(settings));
}

void process_settings_command(const SettingsInvocation& invocation) {
  bool success = false;
  const char* result_text = "FAILED";
  switch (invocation.command) {
    case SettingsCommandKind::activate_profile:
      success = product_web_activate_profile(invocation.profile_id.data());
      if (success) {
        {
          SemaphoreLock input_lock(g_input_mutex);
          if (input_lock.locked()) g_input_router.leave_safe_profile();
        }
        result_text = "PROFILE ACTIVATED";
      } else {
        result_text = "PROFILE ACTIVATE FAILED";
        ESP_LOGE(kTag, "settings profile activation failed");
      }
      break;
    case SettingsCommandKind::scan_wifi: {
      const esp_err_t result = product_wifi_scan(wifi_scan_finished);
      success = result == ESP_OK;
      if (success) return;
      result_text = "WIFI SCAN FAILED";
      ESP_LOGE(kTag, "settings Wi-Fi scan failed: %s",
               esp_err_to_name(result));
      break;
    }
    case SettingsCommandKind::rotate_pin: {
      const esp_err_t result =
          product_web_rotate_pin(invocation.pin.data());
      success = result == ESP_OK;
      result_text = success ? "PIN UPDATED" : "PIN UPDATE FAILED";
      if (!success) {
        ESP_LOGE(kTag, "settings PIN update failed: %s",
                 esp_err_to_name(result));
      }
      break;
    }
    case SettingsCommandKind::stage_wifi: {
      const esp_err_t result =
          product_wifi_save(invocation.ssid.data(),
                            invocation.password.data());
      success = result == ESP_OK;
      result_text = success ? "WIFI CONNECTING" : "WIFI UPDATE FAILED";
      if (!success) {
        ESP_LOGE(kTag, "settings Wi-Fi update failed: %s",
                 esp_err_to_name(result));
      }
      break;
    }
    case SettingsCommandKind::apply_display_settings: {
      const DeviceSettingsResult apply_result =
          g_device_settings_store.apply(invocation.device_settings);
      success = apply_result == DeviceSettingsResult::ok;
      if (success) {
        apply_runtime_settings(invocation.device_settings);
        result_text = "SETTING SAVED";
      } else {
        result_text = "SETTING SAVE FAILED";
        ESP_LOGE(kTag, "settings display save failed: result=%u",
                 static_cast<unsigned>(apply_result));
      }
      break;
    }
    case SettingsCommandKind::return_to_pet: {
      SemaphoreLock input_lock(g_input_mutex);
      if (input_lock.locked()) {
        g_settings.leave();
        if (g_keyboard.has_value()) g_keyboard->on_mode_changed();
      }
      SemaphoreLock ui_lock(g_ui_mutex);
      if (ui_lock.locked()) g_ui.return_to_pet();
      return;
    }
    case SettingsCommandKind::none:
    case SettingsCommandKind::release_hid:
      return;
  }
  SemaphoreLock lock(g_input_mutex);
  if (!lock.locked()) return;
  g_settings.set_result(result_text);
  update_settings_ui_locked();
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

  UiPage page = UiPage::pet;
  {
    SemaphoreLock ui_lock(g_ui_mutex);
    if (ui_lock.locked()) page = g_ui.page();
  }
  if (page == UiPage::settings) {
    if (!g_settings.active()) {
      g_settings.enter();
      refresh_profile_choices_locked();
      if (g_keyboard.has_value()) g_keyboard->on_mode_changed();
      update_settings_ui_locked();
    }
    const SettingsInputResult settings_result = g_settings.on_key(
        event.physical_key, event.pressed, g_pressed[kFnPhysicalKey],
        static_cast<uint64_t>(esp_timer_get_time()) / 1000);
    if (settings_result.captured) {
      if (event.pressed) {
        g_last_ui_input_ms.store(
            static_cast<uint64_t>(esp_timer_get_time()) / 1000);
      }
      if (!queue_settings_command(settings_result)) {
        g_settings.set_result("COMMAND QUEUE FAILED");
      }
      update_settings_ui_locked();
      return;
    }
  }

  const UiNavigationResult navigation = g_ui_navigation.on_key(
      event.physical_key, event.pressed, g_pressed[kFnPhysicalKey]);
  if (navigation.captured) {
    if (event.pressed) {
      g_last_ui_input_ms.store(
          static_cast<uint64_t>(esp_timer_get_time()) / 1000);
    }
    UiPage next_page = page;
    if (navigation.action != UiNavAction::none) {
      SemaphoreLock ui_lock(g_ui_mutex);
      if (ui_lock.locked()) {
        g_ui.navigate(navigation.action);
        next_page = g_ui.page();
      }
    }
    if (next_page != page) {
      if (g_keyboard.has_value()) g_keyboard->on_mode_changed();
      g_pressed.fill(false);
    }
    if (next_page == UiPage::settings && !g_settings.active()) {
      g_settings.enter();
      refresh_profile_choices_locked();
      update_settings_ui_locked();
    } else if (next_page != UiPage::settings && g_settings.active()) {
      g_settings.leave();
    }
    return;
  }

  if (!ui_page_allows_host_input(page)) {
    if (event.pressed) {
      g_last_ui_input_ms.store(
          static_cast<uint64_t>(esp_timer_get_time()) / 1000);
    }
    return;
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
  g_ble_state = connected ? ServiceState::ok : ServiceState::starting;
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
    case WifiState::candidate_connecting:
    case WifiState::rollback_connecting:
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
  CompanionMessageResult result = CompanionMessageResult::invalid;
  CompanionSnapshot snapshot;
  {
    SemaphoreLock lock(g_companion_mutex);
    if (!lock.locked()) return;
    result = g_companion_protocol.apply(json, now_ms);
    if (result == CompanionMessageResult::snapshot) {
      snapshot = g_companion_protocol.snapshot();
    }
  }
  ESP_LOGI(kTag, "companion snapshot result=%u",
           static_cast<unsigned>(result));
  if (result != CompanionMessageResult::snapshot) return;
  g_last_companion_ms.store(now_ms);
  g_companion_state.store(ServiceState::ok);
  {
    SemaphoreLock lock(g_ui_mutex);
    if (lock.locked()) {
      g_ui.set_companion(ServiceState::ok);
      g_ui.set_session(snapshot.title, snapshot.cwd, snapshot.state,
                       snapshot.approvals, snapshot.inputs);
      g_ui.set_codex(
          snapshot.model, snapshot.thinking_level, snapshot.fast,
          std::span<const CodexLimitUsage>(
              snapshot.limits.data(), snapshot.limit_count));
      const PetStoreStatus pet = g_pet_store.status();
      g_ui.set_pet(
          snapshot.pet_id.empty() ? pet.pet_id : snapshot.pet_id,
          snapshot.pet_digest.empty() ? pet.digest : snapshot.pet_digest,
          snapshot.pet_state, pet.last_result);
    }
  }
  update_web_status();
}

void companion_heartbeat() {
  if (g_companion_state.load() != ServiceState::ok) return;
  const uint64_t now_ms =
      static_cast<uint64_t>(esp_timer_get_time()) / 1000;
  SemaphoreLock lock(g_companion_mutex);
  if (!lock.locked()) return;
  g_companion_protocol.heartbeat(now_ms);
  g_last_companion_ms.store(now_ms);
}

void ui_task(void*) {
  bool long_press_handled = false;
  TickType_t wake = xTaskGetTickCount();
  uint32_t rendered_revision = UINT32_MAX;
  uint64_t next_frame_ms = 0;
  uint8_t frame_index = 0;
  PetState rendered_pet_state = PetState::idle;
  std::string last_pet_digest;
  uint64_t last_pet_sync_ms = 0;
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
    SettingsInvocation settings_invocation;
    while (g_settings_queue != nullptr &&
           xQueueReceive(g_settings_queue, &settings_invocation, 0) ==
               pdTRUE) {
      process_settings_command(settings_invocation);
    }
    const uint32_t return_timeout = g_return_to_pet_ms.load();
    bool timeout_suspended = false;
    {
      SemaphoreLock lock(g_input_mutex);
      timeout_suspended =
          lock.locked() && g_settings.return_timeout_suspended();
    }
    if (return_timeout != 0 && !timeout_suspended &&
        g_last_ui_input_ms.load() != 0 &&
        now_ms - g_last_ui_input_ms.load() >= return_timeout) {
      {
        SemaphoreLock lock(g_input_mutex);
        if (lock.locked()) {
          g_settings.leave();
          if (g_keyboard.has_value()) g_keyboard->on_mode_changed();
        }
      }
      {
        SemaphoreLock lock(g_ui_mutex);
        if (lock.locked()) g_ui.return_to_pet();
      }
      g_last_ui_input_ms.store(0);
    }
    bool companion_stale = false;
    {
      SemaphoreLock lock(g_companion_mutex);
      companion_stale =
          lock.locked() && g_companion_protocol.stale(now_ms);
    }
    if (g_companion_state.load() == ServiceState::ok && companion_stale) {
      g_companion_state.store(ServiceState::offline);
      SemaphoreLock lock(g_ui_mutex);
      if (lock.locked()) {
        g_ui.set_companion(ServiceState::offline);
        g_ui.set_session("NO ACTIVE CODEX", "-", "STALE", 0, 0);
        const PetStoreStatus pet = g_pet_store.status();
        g_ui.set_pet(pet.pet_id, pet.digest, PetState::waiting,
                     pet.last_result);
      }
      update_web_status();
    }

    const PetStoreStatus pet_status = g_pet_store.status();
    if (!pet_status.digest.empty() &&
        pet_status.digest != last_pet_digest) {
      last_pet_digest = pet_status.digest;
      last_pet_sync_ms = now_ms;
    }
    UiPage current_page = UiPage::pet;
    PetState current_pet_state = PetState::waiting;
    {
      SemaphoreLock lock(g_ui_mutex);
      if (lock.locked()) {
        std::array<char, 21> profile_name{};
        if (product_web_profile_name(profile_name.data(),
                                     profile_name.size())) {
          g_ui.set_profile(profile_name.data());
        }
        g_ui.set_web(product_wifi_ipv4(), product_web_pairing_code());
        g_ui.set_pet_storage(
            static_cast<uint32_t>(pet_status.storage_used),
            pet_status.format_version);
        const uint64_t last_companion = g_last_companion_ms.load();
        g_ui.set_heartbeat_age(
            last_companion == 0 || now_ms < last_companion
                ? 0
                : static_cast<uint32_t>(
                      std::min<uint64_t>(
                          (now_ms - last_companion) / 1000, UINT32_MAX)));
        g_ui.set_pet_sync_age(
            last_pet_sync_ms == 0 || now_ms < last_pet_sync_ms
                ? 0
                : static_cast<uint32_t>(
                      std::min<uint64_t>(
                          (now_ms - last_pet_sync_ms) / 1000, UINT32_MAX)));
        if (!pet_status.pet_id.empty() &&
            g_companion_state.load() != ServiceState::ok) {
          g_ui.set_pet(pet_status.pet_id, pet_status.digest,
                       PetState::waiting, pet_status.last_result);
        }
        current_page = g_ui.page();
        current_pet_state =
            effective_pet_state(
                g_companion_state.load() != ServiceState::ok,
                g_ui.pet_state());
        const uint32_t revision = g_ui.revision();
        if (revision != rendered_revision) {
          display_render_page(g_ui);
          rendered_revision = revision;
        }
      }
    }
    if (current_page == UiPage::pet && now_ms >= next_frame_ms) {
      if (current_pet_state != rendered_pet_state) {
        frame_index = 0;
        rendered_pet_state = current_pet_state;
      }
      if (!display_render_pet_frame(
              g_pet_store, current_pet_state, frame_index)) {
        display_render_placeholder(current_pet_state);
      }
      frame_index = static_cast<uint8_t>((frame_index + 1) % 8);
      const uint32_t interval = g_pet_frame_interval_ms.load();
      next_frame_ms = now_ms > next_frame_ms + interval * 2
                          ? now_ms + interval
                          : next_frame_ms + interval;
      if (next_frame_ms == interval) next_frame_ms = now_ms + interval;
    }
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(200));
  }
}

void profile_catalog_task(void*) {
  const esp_err_t result =
      product_web_prepare_profile_catalog(&g_profile_catalog);
  g_profile_catalog.release_scratch();
  g_profile_catalog_initialization_complete.store(true);
  ESP_LOGI(kTag, "heap after profile catalog: free=%u largest=%u",
           static_cast<unsigned>(
               heap_caps_get_free_size(MALLOC_CAP_8BIT)),
           static_cast<unsigned>(
               heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "profile catalog unavailable: %s",
             esp_err_to_name(result));
  } else {
    ESP_LOGI(kTag, "profile catalog ready");
  }
  g_profile_catalog_task_handle = nullptr;
  xSemaphoreGive(g_profile_catalog_initialization_done);
  vTaskDelete(nullptr);
}

class EspProductStartup final : public ProductStartupBackend {
 public:
  bool display() override {
    const bool ok = display_start(&g_ui) == ESP_OK;
    display_ready_ = ok;
    if (ok) apply_runtime_settings(g_device_settings_store.current());
    return ok;
  }

  bool config() override {
    g_profile_catalog_initialization_complete.store(false);
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      result = nvs_flash_erase();
      if (result == ESP_OK) result = nvs_flash_init();
    }
    if (result == ESP_OK) {
      g_device_settings_store.load();
      const DeviceSettings settings = g_device_settings_store.current();
      g_settings.set_device_settings(settings);
      const esp_err_t pet_result = g_pet_store.start();
      if (pet_result == ESP_OK) {
        product_web_set_pet_store(&g_pet_store);
      } else {
        ESP_LOGW(kTag, "pet store unavailable: %s",
                 esp_err_to_name(pet_result));
      }
      g_profile_catalog_initialization_done = xSemaphoreCreateBinaryStatic(
          &g_profile_catalog_initialization_done_storage);
      if (g_profile_catalog_initialization_done == nullptr ||
          !g_profile_catalog_backend.start() ||
          !g_profile_catalog.reserve_scratch()) {
        result = ESP_ERR_NO_MEM;
        g_profile_catalog_initialization_complete.store(true);
        ESP_LOGE(kTag, "profile catalog storage initialization failed");
      } else {
        const BaseType_t catalog_created =
            xTaskCreate(profile_catalog_task, "profile-catalog-init", 32768,
                        nullptr, tskIDLE_PRIORITY,
                        &g_profile_catalog_task_handle);
        if (catalog_created != pdPASS ||
            g_profile_catalog_task_handle == nullptr) {
          g_profile_catalog.release_scratch();
          g_profile_catalog_initialization_complete.store(true);
          result = ESP_ERR_NO_MEM;
          ESP_LOGE(kTag, "profile catalog task initialization failed");
        } else if (xSemaphoreTake(
                       g_profile_catalog_initialization_done,
                       portMAX_DELAY) != pdTRUE) {
          result = ESP_ERR_TIMEOUT;
          ESP_LOGE(kTag, "profile catalog initialization wait failed");
        } else {
          vTaskDelay(1);
        }
      }
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
    g_settings_queue = xQueueCreateStatic(
        kSettingsQueueDepth, sizeof(SettingsInvocation),
        g_settings_queue_buffer.data(), &g_settings_queue_storage);
    if (g_settings_queue == nullptr) {
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
      g_ble_state = ServiceState::starting;
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
    for (uint16_t attempt = 0; attempt < 80; ++attempt) {
      const WifiState state = product_wifi_state();
      if (state != WifiState::idle && state != WifiState::connecting &&
          state != WifiState::candidate_connecting &&
          state != WifiState::rollback_connecting) {
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(250));
    }
    vTaskDelay(pdMS_TO_TICKS(250));
    for (uint16_t attempt = 0;
         attempt < 240 &&
         !g_profile_catalog_initialization_complete.load();
         ++attempt) {
      vTaskDelay(pdMS_TO_TICKS(250));
    }
    product_web_set_companion_snapshot_handler(companion_snapshot);
    product_web_set_companion_heartbeat_handler(companion_heartbeat);
    ESP_LOGI(kTag, "heap before HTTPS: free=%u largest=%u",
             static_cast<unsigned>(
                 heap_caps_get_free_size(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
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
    g_companion_state.store(ServiceState::offline);
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
    if (display_ready_) display_render_boot(g_ui);
  }

  bool display_ready_ = false;
};
}  // namespace

void product_runtime_start() {
  g_ui_mutex = xSemaphoreCreateMutexStatic(&g_ui_mutex_storage);
  g_input_mutex = xSemaphoreCreateMutexStatic(&g_input_mutex_storage);
  g_companion_mutex =
      xSemaphoreCreateMutexStatic(&g_companion_mutex_storage);
  if (g_ui_mutex == nullptr || g_input_mutex == nullptr ||
      g_companion_mutex == nullptr) {
    ESP_LOGE(kTag, "runtime mutex initialization failed");
    return;
  }
  static EspProductStartup startup;
  static ProductController controller(startup);
  controller.start();
  ESP_LOGI(kTag, "product runtime started");
}

#endif
