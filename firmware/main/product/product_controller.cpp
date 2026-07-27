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
      {BootStage::wifi, &ProductStartupBackend::wifi},
      {BootStage::web, &ProductStartupBackend::web},
      {BootStage::ble, &ProductStartupBackend::ble},
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
#include <cinttypes>
#include <cstdio>
#include <memory>
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
#include "hal/usb_serial_jtag_ll.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "probe/ble_services.hpp"
#include "probe/keyboard_probe.hpp"
#include "product/companion_protocol.hpp"
#include "product/audio_capture.hpp"
#include "product/ble_audio_transport.hpp"
#include "product/display.hpp"
#include "product/display_policy.hpp"
#include "product/device_settings.hpp"
#include "product/hil_serial_control.hpp"
#include "product/input_router.hpp"
#include "product/keyboard_matrix.hpp"
#include "product/macro_engine.hpp"
#include "product/microphone_controller.hpp"
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
constexpr std::size_t kMicrophoneQueueDepth = 8;
constexpr char kSettingsNvsNamespace[] = "product";
constexpr UBaseType_t kAudioTaskPriority = tskIDLE_PRIORITY + 6;

struct MacroInvocation {
  uint8_t layer = 0;
  uint8_t physical_key = 0;
};

struct SettingsInvocation {
  SettingsCommandKind command = SettingsCommandKind::none;
  uint8_t selected = 0;
  InputMode input_mode = InputMode::keyboard;
  DeviceSettings device_settings{};
  std::array<char, 9> pin{};
  std::array<char, 9> profile_id{};
  std::array<char, 33> ssid{};
  std::array<char, 65> password{};
};

enum class MicrophoneRuntimeEvent : uint8_t {
  sink_ready,
  sink_lost,
  g0_click,
  g0_ignored,
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
StaticQueue_t g_microphone_queue_storage{};
std::array<uint8_t,
           kMicrophoneQueueDepth * sizeof(MicrophoneRuntimeEvent)>
    g_microphone_queue_buffer{};
QueueHandle_t g_microphone_queue = nullptr;
StaticTask_t g_macro_task_storage{};
std::array<StackType_t, 1920> g_macro_task_stack{};
StaticTask_t g_ui_task_storage{};
std::array<StackType_t, 4096> g_ui_task_stack{};
StaticTask_t g_audio_task_storage{};
std::array<StackType_t, 3584> g_audio_task_stack{};
TaskHandle_t g_audio_task_handle = nullptr;
TaskHandle_t g_macro_task_handle = nullptr;
TaskHandle_t g_ui_task_handle = nullptr;
void* g_runtime_heap_reserve = nullptr;
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
std::atomic<bool> g_audio_data_ready{false};
std::atomic<bool> g_audio_sink_declared{false};
std::atomic<uint32_t> g_allocation_failures{0};
std::unique_ptr<IAudioCapture> g_audio_capture;
std::unique_ptr<IBleAudioTransport> g_audio_transport;
std::unique_ptr<MicrophoneController> g_microphone;
HilSerialCommandParser g_hil_serial_parser;
HilHidBurst g_hil_hid_burst;

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

void allocation_failed(size_t, uint32_t, const char*) {
  uint32_t current = g_allocation_failures.load();
  while (current != UINT32_MAX &&
         !g_allocation_failures.compare_exchange_weak(
             current, current + 1)) {
  }
}

uint32_t task_stack_free_bytes(TaskHandle_t task) {
  return task == nullptr
             ? 0
             : static_cast<uint32_t>(
                   uxTaskGetStackHighWaterMark(task) *
                   sizeof(StackType_t));
}

void emit_runtime_metrics(uint64_t now_us) {
  const HidRuntimeSummary hid =
      g_keyboard.has_value()
          ? g_keyboard->hid_runtime_summary()
          : HidRuntimeSummary{};
  const MicrophoneSnapshot audio =
      g_microphone != nullptr
          ? g_microphone->snapshot()
          : MicrophoneSnapshot{};
  constexpr uint32_t kScannerStackBytes =
      kKeyboardScannerTaskStackBytes * sizeof(StackType_t);
  constexpr uint32_t kHidStackBytes =
      kHidSenderTaskStackBytes * sizeof(StackType_t);
  const uint32_t capabilities =
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  const std::string_view scenario = product_web_resource_scenario(
      product_web_tls_resource_window_active() ? 1 : 0);
  std::printf(
      "{\"scenario\":\"%s\",\"monotonic_us\":%" PRIu64
      ",\"free_internal_heap\":%u,\"largest_internal_block\":%u,"
      "\"allocation_failures\":%u,"
      "\"hid\":{\"generated\":%u,\"queued\":%u,"
      "\"queue_failures\":%u,\"p95_upper_bound_us\":%u},"
      "\"audio\":{\"captured_frames\":%u,\"source_overruns\":%u,"
      "\"transport_drops\":%u,\"fallback_count\":%u,"
      "\"pcm_peak\":%u,\"pcm_mean_abs\":%u},"
      "\"tasks\":["
      "{\"name\":\"scanner\",\"configured\":%u,"
      "\"high_water_free_bytes\":%u},"
      "{\"name\":\"keyboard-hid\",\"configured\":%u,"
      "\"high_water_free_bytes\":%u},"
      "{\"name\":\"macro\",\"configured\":%zu,"
      "\"high_water_free_bytes\":%u},"
      "{\"name\":\"audio\",\"configured\":%zu,"
      "\"high_water_free_bytes\":%u},"
      "{\"name\":\"ui\",\"configured\":%zu,"
      "\"high_water_free_bytes\":%u},"
      "{\"name\":\"https\",\"configured\":%zu,"
      "\"high_water_free_bytes\":%u},"
      "{\"name\":\"pet-upload\",\"configured\":%u,"
      "\"high_water_free_bytes\":%u},"
      "{\"name\":\"ble-watchdog\",\"configured\":%u,"
      "\"high_water_free_bytes\":%u},"
      "{\"name\":\"wifi-state\",\"configured\":%u,"
      "\"high_water_free_bytes\":%u}]}\n",
      scenario.data(),
      now_us,
      static_cast<unsigned>(
          heap_caps_get_free_size(capabilities)),
      static_cast<unsigned>(
          heap_caps_get_largest_free_block(capabilities)),
      static_cast<unsigned>(g_allocation_failures.load()),
      static_cast<unsigned>(hid.generated),
      static_cast<unsigned>(hid.queued),
      static_cast<unsigned>(hid.queue_failures),
      static_cast<unsigned>(hid.p95_upper_bound_us),
      static_cast<unsigned>(audio.captured_frames),
      static_cast<unsigned>(audio.source_overruns),
      static_cast<unsigned>(audio.transport_drops),
      static_cast<unsigned>(audio.fallback_count),
      static_cast<unsigned>(audio.pcm_peak),
      static_cast<unsigned>(audio.pcm_mean_abs),
      static_cast<unsigned>(kScannerStackBytes),
      static_cast<unsigned>(
          task_stack_free_bytes(keyboard_matrix_task())),
      static_cast<unsigned>(kHidStackBytes),
      static_cast<unsigned>(task_stack_free_bytes(
          g_keyboard.has_value()
              ? g_keyboard->hid_sender_task()
              : nullptr)),
      sizeof(g_macro_task_stack),
      static_cast<unsigned>(
          task_stack_free_bytes(g_macro_task_handle)),
      sizeof(g_audio_task_stack),
      static_cast<unsigned>(
          task_stack_free_bytes(g_audio_task_handle)),
      sizeof(g_ui_task_stack),
      static_cast<unsigned>(
          task_stack_free_bytes(g_ui_task_handle)),
      kProductWebTaskStackBytes,
      static_cast<unsigned>(
          task_stack_free_bytes(xTaskGetHandle("httpd"))),
      7552U,
      static_cast<unsigned>(
          task_stack_free_bytes(xTaskGetHandle("pet-upload"))),
      1920U,
      static_cast<unsigned>(
          task_stack_free_bytes(xTaskGetHandle("ble-watchdog"))),
      2304U,
      static_cast<unsigned>(
          task_stack_free_bytes(xTaskGetHandle("wifi-state"))));
  std::fflush(stdout);
}

void update_web_status() {
  product_web_set_status(g_ble_state, g_wifi_state, g_companion_state.load());
}

UiMicrophoneState ui_microphone_state(MicrophoneState state) {
  switch (state) {
    case MicrophoneState::unavailable:
      return UiMicrophoneState::unavailable;
    case MicrophoneState::ready:
    case MicrophoneState::starting:
    case MicrophoneState::stopping:
      return UiMicrophoneState::ready;
    case MicrophoneState::live24:
      return UiMicrophoneState::live24;
    case MicrophoneState::live16:
      return UiMicrophoneState::live16;
    case MicrophoneState::error:
      return UiMicrophoneState::error;
  }
  return UiMicrophoneState::error;
}

ProductWebMicrophoneState web_microphone_state(MicrophoneState state) {
  switch (state) {
    case MicrophoneState::unavailable:
      return ProductWebMicrophoneState::unavailable;
    case MicrophoneState::ready:
    case MicrophoneState::starting:
    case MicrophoneState::stopping:
      return ProductWebMicrophoneState::ready;
    case MicrophoneState::live24:
      return ProductWebMicrophoneState::live24;
    case MicrophoneState::live16:
      return ProductWebMicrophoneState::live16;
    case MicrophoneState::error:
      return ProductWebMicrophoneState::error;
  }
  return ProductWebMicrophoneState::error;
}

uint8_t microphone_drop_percent(const MicrophoneSnapshot& snapshot) {
  const uint64_t drops =
      static_cast<uint64_t>(snapshot.source_overruns) +
      snapshot.transport_drops;
  const uint64_t total =
      static_cast<uint64_t>(snapshot.captured_frames) + drops;
  if (total == 0) return 0;
  return static_cast<uint8_t>(
      std::min<uint64_t>(100, (drops * 100 + total / 2) / total));
}

void publish_microphone_snapshot(uint64_t now_ms) {
  const MicrophoneSnapshot snapshot =
      g_microphone != nullptr
          ? g_microphone->snapshot()
          : MicrophoneSnapshot{};
  const bool live =
      snapshot.state == MicrophoneState::live24 ||
      snapshot.state == MicrophoneState::live16;
  const uint32_t sample_rate =
      !live
          ? 0
          : snapshot.active_rate == AudioSampleRate::hz24000
                ? 24000
                : 16000;
  const uint8_t drop_percent = microphone_drop_percent(snapshot);
  ProductWebMicrophoneError web_error =
      ProductWebMicrophoneError::none;
  if (snapshot.state == MicrophoneState::error) {
    web_error =
        snapshot.failure == MicrophoneFailure::no_signal
            ? ProductWebMicrophoneError::mic_no_signal
            : ProductWebMicrophoneError::mic_init_failed;
  }
  product_web_set_microphone({
      .state = web_microphone_state(snapshot.state),
      .sample_rate_hz = sample_rate,
      .drop_percent = drop_percent,
      .last_error = web_error,
  });
  SemaphoreLock lock(g_ui_mutex);
  if (!lock.locked()) return;
  g_ui.set_microphone(
      ui_microphone_state(snapshot.state), sample_rate, drop_percent,
      web_error == ProductWebMicrophoneError::none
          ? "NONE"
          : web_error == ProductWebMicrophoneError::mic_no_signal
                ? "MIC NO SIGNAL"
                : "MIC INIT FAILED",
      now_ms);
  g_ui.expire_microphone_error(now_ms);
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
      .input_mode = result.input_mode,
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

void release_and_apply_input_mode(InputMode mode) {
  SemaphoreLock lock(g_input_mutex);
  if (!lock.locked()) return;
  if (g_keyboard.has_value()) g_keyboard->on_mode_changed();
  g_input_router.leave_safe_profile();
  g_input_router.set_mode(mode);
  SemaphoreLock ui_lock(g_ui_mutex);
  if (ui_lock.locked()) g_ui.set_mode(mode);
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
    case SettingsCommandKind::apply_input_mode:
      release_and_apply_input_mode(invocation.input_mode);
      success = true;
      result_text = "INPUT MODE SAVED";
      break;
    case SettingsCommandKind::activate_safe_profile:
      success = product_web_activate_profile("SAFE");
      if (success) {
        release_and_set_mode(true);
        result_text = "SAFE PROFILE ACTIVE";
      } else {
        result_text = "SAFE PROFILE FAILED";
      }
      break;
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
  if (g_keyboard.has_value()) {
    g_keyboard->observe_product_hid_event(
        static_cast<int64_t>(event.stable_at_ms * 1000),
        esp_timer_get_time(), true);
  }
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

void enqueue_microphone_event(MicrophoneRuntimeEvent event,
                              bool privacy_critical = false) {
  if (g_microphone_queue == nullptr) return;
  if (xQueueSend(g_microphone_queue, &event, 0) == pdTRUE) return;
  if (privacy_critical) {
    xQueueReset(g_microphone_queue);
    if (xQueueSendToFront(g_microphone_queue, &event, 0) == pdTRUE) {
      ESP_LOGW(kTag, "microphone queue reset for stop event");
      return;
    }
  }
  ESP_LOGW(kTag, "microphone event queue full event=%u",
           static_cast<unsigned>(event));
}

void poll_hil_serial_control() {
  std::array<uint8_t, 32> input{};
  const uint32_t count =
      usb_serial_jtag_ll_read_rxfifo(input.data(), input.size());
  for (uint32_t index = 0; index < count; ++index) {
    const HilMicrophoneCommand command =
        g_hil_serial_parser.consume(input[static_cast<std::size_t>(index)]);
    if (command == HilMicrophoneCommand::none) continue;
    if (command == HilMicrophoneCommand::hid_start) {
      const bool accepted = g_hil_hid_burst.start(
          g_keyboard.has_value() && ble_keyboard_ready());
      std::printf("HIL HID START %s\n",
                  accepted ? "ACCEPTED" : "REJECTED");
      continue;
    }
    if (command == HilMicrophoneCommand::hid_stop) {
      std::printf("HIL HID STOP %s\n",
                  g_hil_hid_burst.stop() ? "STOPPED" : "NOOP");
      continue;
    }
    const MicrophoneState state =
        g_microphone == nullptr
            ? MicrophoneState::unavailable
            : g_microphone->snapshot().state;
    const HilCommandDecision decision =
        hil_command_decision(command, state);
    enqueue_microphone_event(
        decision.event == MicrophoneEventKind::g0_click
            ? MicrophoneRuntimeEvent::g0_click
            : MicrophoneRuntimeEvent::g0_ignored);
    const char* action =
        command == HilMicrophoneCommand::start ? "START" : "STOP";
    const char* result =
        decision.accepted
            ? "ACCEPTED"
            : command == HilMicrophoneCommand::start ? "REJECTED"
                                                     : "NOOP";
    std::printf("HIL MIC %s %s\n", action, result);
  }
}

void advance_hil_hid_burst() {
  if (!g_hil_hid_burst.active() || !g_keyboard.has_value()) return;
  const auto event = g_hil_hid_burst.next(
      static_cast<uint64_t>(esp_timer_get_time()));
  if (!event.has_value()) return;
  g_keyboard->enqueue_stable_key_event(*event);
  if (!g_hil_hid_burst.active()) {
    std::printf("HIL HID COMPLETE %u\n",
                static_cast<unsigned>(g_hil_hid_burst.generated()));
  }
}

void publish_microphone_readiness() {
  const bool ready =
      g_audio_data_ready.load() && g_audio_sink_declared.load();
  enqueue_microphone_event(
      ready ? MicrophoneRuntimeEvent::sink_ready
            : MicrophoneRuntimeEvent::sink_lost,
      !ready);
}

void audio_sink_state_changed(bool ready) {
  g_audio_data_ready.store(ready);
  if (!ready) g_audio_sink_declared.store(false);
  publish_microphone_readiness();
}

void audio_control_received(AudioControlMessage message) {
  switch (message.opcode) {
    case AudioControlOpcode::sink_ready:
      g_audio_sink_declared.store(true);
      publish_microphone_readiness();
      break;
    case AudioControlOpcode::sink_not_ready:
      g_audio_sink_declared.store(false);
      publish_microphone_readiness();
      break;
    case AudioControlOpcode::hello:
    case AudioControlOpcode::set_preferred_rate:
    case AudioControlOpcode::reset_statistics:
      break;
  }
}

void audio_task(void*) {
  MicrophoneState previous_state = MicrophoneState::unavailable;
  uint64_t warmup_ends_ms = 0;
  uint64_t window_started_ms = 0;
  MicrophoneSnapshot window_baseline{};
  while (true) {
    MicrophoneRuntimeEvent event;
    while (g_microphone_queue != nullptr &&
           xQueueReceive(g_microphone_queue, &event, 0) == pdTRUE) {
      if (g_microphone == nullptr) continue;
      switch (event) {
        case MicrophoneRuntimeEvent::sink_ready:
          g_microphone->on_sink_ready(true);
          break;
        case MicrophoneRuntimeEvent::sink_lost:
          g_microphone->stop_for_disconnect();
          break;
        case MicrophoneRuntimeEvent::g0_click:
          g_microphone->on_g0_click();
          break;
        case MicrophoneRuntimeEvent::g0_ignored:
          g_microphone->on_g0_ignored();
          break;
      }
    }

    if (g_microphone == nullptr) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    const MicrophoneSnapshot before = g_microphone->snapshot();
    const bool active =
        before.state == MicrophoneState::live24 ||
        before.state == MicrophoneState::live16;
    if (active) {
      (void)g_microphone->run_once();
    }
    MicrophoneSnapshot after = g_microphone->snapshot();
    const uint64_t now_ms =
        static_cast<uint64_t>(esp_timer_get_time()) / 1000;
    const bool live =
        after.state == MicrophoneState::live24 ||
        after.state == MicrophoneState::live16;
    if (live && previous_state != after.state) {
      warmup_ends_ms = now_ms + 2000;
      window_started_ms = warmup_ends_ms;
      window_baseline = after;
    }
    if (live && active) {
      if (now_ms >= warmup_ends_ms &&
          now_ms - window_started_ms >= 5000) {
        const uint32_t expected_frames =
            5000U / audio_frame_duration_ms(after.active_rate);
        const uint32_t captured =
            after.captured_frames - window_baseline.captured_frames;
        const uint32_t transport_lost =
            after.transport_drops - window_baseline.transport_drops;
        const uint32_t source_lost =
            after.source_overruns - window_baseline.source_overruns;
        const uint32_t schedule_lost =
            captured < expected_frames ? expected_frames - captured : 0;
        const uint32_t lost =
            std::max(source_lost, schedule_lost) + transport_lost;
        const MicrophoneState evaluated_state = after.state;
        g_microphone->on_loss_window(
            static_cast<uint64_t>(lost) * 100U <= expected_frames);
        after = g_microphone->snapshot();
        if (after.state != evaluated_state) {
          warmup_ends_ms = now_ms + 2000;
          window_started_ms = warmup_ends_ms;
        } else {
          window_started_ms = now_ms;
        }
        window_baseline = after;
      }
    } else if (!live) {
      warmup_ends_ms = 0;
      window_started_ms = 0;
    }
    previous_state = after.state;
    if (!live) {
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
}

void ble_connection_changed(bool connected) {
  g_ble_state = connected ? ServiceState::ok : ServiceState::starting;
  if (!connected) {
    g_audio_data_ready.store(false);
    g_audio_sink_declared.store(false);
    enqueue_microphone_event(MicrophoneRuntimeEvent::sink_lost, true);
  }
  {
    SemaphoreLock lock(g_ui_mutex);
    if (lock.locked()) g_ui.set_ble(g_ble_state);
  }
  update_web_status();
}

void ble_disconnected() {
  if (g_keyboard.has_value()) g_keyboard->on_ble_disconnected();
  g_audio_data_ready.store(false);
  g_audio_sink_declared.store(false);
  enqueue_microphone_event(MicrophoneRuntimeEvent::sink_lost, true);
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
  uint64_t g0_pressed_at_ms = 0;
  TickType_t wake = xTaskGetTickCount();
  uint32_t rendered_revision = UINT32_MAX;
  uint32_t rendered_pet_revision = UINT32_MAX;
  uint64_t next_frame_ms = 0;
  uint8_t frame_index = 0;
  PetState rendered_pet_state = PetState::idle;
  std::string last_pet_digest;
  uint64_t last_pet_sync_ms = 0;
  uint64_t last_metrics_ms = 0;
  while (true) {
    M5.update();
    poll_hil_serial_control();
    advance_hil_hid_burst();
    const uint64_t button_now_ms =
        static_cast<uint64_t>(esp_timer_get_time()) / 1000;
    if (M5.BtnA.wasPressed()) {
      g0_pressed_at_ms = button_now_ms;
    }
    if (M5.BtnA.wasReleased()) {
      const uint32_t held_ms =
          g0_pressed_at_ms == 0 || button_now_ms < g0_pressed_at_ms
              ? UINT32_MAX
              : static_cast<uint32_t>(std::min<uint64_t>(
                    UINT32_MAX, button_now_ms - g0_pressed_at_ms));
      enqueue_microphone_event(
          microphone_button_event(held_ms) ==
                  MicrophoneButtonEvent::click
              ? MicrophoneRuntimeEvent::g0_click
              : MicrophoneRuntimeEvent::g0_ignored);
      g0_pressed_at_ms = 0;
    }

    const uint64_t now_ms =
        static_cast<uint64_t>(esp_timer_get_time()) / 1000;
    publish_microphone_snapshot(now_ms);
    if (last_metrics_ms == 0 || now_ms - last_metrics_ms >= 1000) {
      emit_runtime_metrics(now_ms * 1000);
      last_metrics_ms = now_ms;
    }
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
    bool pet_chrome_changed = false;
    const MicrophoneState microphone_state =
        g_microphone == nullptr
            ? MicrophoneState::unavailable
            : g_microphone->snapshot().state;
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
        const uint32_t pet_revision = g_ui.pet_revision();
        const bool render_page =
            current_page == UiPage::pet
                ? pet_revision != rendered_pet_revision
                : revision != rendered_revision;
        if (render_page) {
          display_render_page(g_ui);
          pet_chrome_changed = current_page == UiPage::pet;
          rendered_revision = revision;
          rendered_pet_revision =
              current_page == UiPage::pet
                  ? pet_revision
                  : UINT32_MAX;
        }
      }
    }
    const PetFrameRenderMode frame_mode =
        current_page == UiPage::pet
            ? pet_frame_render_mode(
                  microphone_state, pet_chrome_changed,
                  now_ms >= next_frame_ms)
            : PetFrameRenderMode::none;
    if (frame_mode != PetFrameRenderMode::none) {
      if (current_pet_state != rendered_pet_state) {
        frame_index = 0;
        rendered_pet_state = current_pet_state;
      }
      if (!display_render_pet_frame(
              g_pet_store, current_pet_state, frame_index)) {
        display_render_placeholder(current_pet_state);
      }
      if (frame_mode == PetFrameRenderMode::animated_frame) {
        frame_index = static_cast<uint8_t>((frame_index + 1) % 8);
        const uint32_t interval = g_pet_frame_interval_ms.load();
        next_frame_ms = now_ms > next_frame_ms + interval * 2
                            ? now_ms + interval
                            : next_frame_ms + interval;
        if (next_frame_ms == interval) next_frame_ms = now_ms + interval;
      }
    }
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(50));
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
      g_settings.set_input_mode(g_input_router.mode());
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
    if (result == ESP_OK) {
      g_runtime_heap_reserve = heap_caps_malloc(
          kProductRuntimeHeapReserveBytes,
          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      if (g_runtime_heap_reserve == nullptr) {
        result = ESP_ERR_NO_MEM;
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
    g_macro_task_handle = xTaskCreateStatic(
        macro_task, "product-macro", g_macro_task_stack.size(), nullptr,
        tskIDLE_PRIORITY + 2, g_macro_task_stack.data(), &g_macro_task_storage);
    esp_err_t result =
        g_macro_task_handle == nullptr
            ? ESP_ERR_NO_MEM
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
      g_microphone_queue = xQueueCreateStatic(
          kMicrophoneQueueDepth, sizeof(MicrophoneRuntimeEvent),
          g_microphone_queue_buffer.data(), &g_microphone_queue_storage);
      g_audio_capture = make_product_audio_capture();
      g_audio_transport = make_product_ble_audio_transport();
      if (g_microphone_queue == nullptr || g_audio_capture == nullptr ||
          g_audio_transport == nullptr) {
        result = ESP_ERR_NO_MEM;
      } else {
        g_microphone = std::make_unique<MicrophoneController>(
            *g_audio_capture, *g_audio_transport);
        g_audio_task_handle = xTaskCreateStatic(
            audio_task, "product-audio", g_audio_task_stack.size(), nullptr,
            kAudioTaskPriority, g_audio_task_stack.data(),
            &g_audio_task_storage);
        if (g_microphone == nullptr || g_audio_task_handle == nullptr) {
          result = ESP_ERR_NO_MEM;
        }
      }
    }
    if (result == ESP_OK) {
      set_ble_disconnect_handler(ble_disconnected);
      set_ble_connection_handler(ble_connection_changed);
      set_audio_control_handler(audio_control_received);
      set_audio_sink_state_handler(audio_sink_state_changed);
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
    if (g_runtime_heap_reserve != nullptr) {
      heap_caps_free(g_runtime_heap_reserve);
      g_runtime_heap_reserve = nullptr;
    }
    g_ui_task_handle = xTaskCreateStatic(
        ui_task, "product-ui", g_ui_task_stack.size(), nullptr,
        tskIDLE_PRIORITY + 1, g_ui_task_stack.data(), &g_ui_task_storage);
    update_web_status();
    return g_ui_task_handle != nullptr;
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
  const esp_err_t allocation_hook_result =
      heap_caps_register_failed_alloc_callback(allocation_failed);
  if (allocation_hook_result != ESP_OK) {
    ESP_LOGW(kTag, "allocation failure hook unavailable: %s",
             esp_err_to_name(allocation_hook_result));
  }
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
