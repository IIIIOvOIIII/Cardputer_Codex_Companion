#include "product/onboarding.hpp"

#include <algorithm>
#include <cstdio>

#include "product/keymap.hpp"

namespace {
constexpr uint8_t kMagic0 = 'O';
constexpr uint8_t kMagic1 = 'B';
constexpr uint8_t kKeyBacktick = 0;
constexpr uint8_t kKeyBackspace = 13;
constexpr uint8_t kKeyUp = 39;
constexpr uint8_t kKeyEnter = 41;
constexpr uint8_t kKeyDown = 53;
constexpr uint8_t kVisibleRows = 5;

uint32_t crc32(std::span<const uint8_t> bytes) {
  uint32_t crc = 0xffffffffu;
  for (const uint8_t byte : bytes) {
    crc ^= byte;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1) ^ (0xedb88320u & mask);
    }
  }
  return crc ^ 0xffffffffu;
}

uint32_t get_u32(const uint8_t* value) {
  return static_cast<uint32_t>(value[0]) |
         (static_cast<uint32_t>(value[1]) << 8) |
         (static_cast<uint32_t>(value[2]) << 16) |
         (static_cast<uint32_t>(value[3]) << 24);
}

void put_u32(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
  output[2] = static_cast<uint8_t>(value >> 16);
  output[3] = static_cast<uint8_t>(value >> 24);
}

bool valid(const OnboardingRecord& record) {
  return record.schema_version == 1 &&
         record.checkpoint <= OnboardingCheckpoint::complete;
}

OnboardingStep step_for(OnboardingCheckpoint checkpoint) {
  switch (checkpoint) {
    case OnboardingCheckpoint::needs_wifi:
      return OnboardingStep::wifi_scan;
    case OnboardingCheckpoint::needs_ble:
      return OnboardingStep::ble_pair_guide;
    case OnboardingCheckpoint::needs_agent:
      return OnboardingStep::agent_install_guide;
    case OnboardingCheckpoint::complete:
      return OnboardingStep::complete;
  }
  return OnboardingStep::wifi_scan;
}
}  // namespace

OnboardingRecordBytes encode_onboarding_record(
    const OnboardingRecord& record
) {
  OnboardingRecordBytes bytes{};
  bytes[0] = kMagic0;
  bytes[1] = kMagic1;
  bytes[2] = record.schema_version;
  bytes[3] = static_cast<uint8_t>(record.checkpoint);
  put_u32(bytes.data() + 8, crc32(std::span(bytes).first(8)));
  return bytes;
}

bool decode_onboarding_record(
    std::span<const uint8_t> bytes,
    OnboardingRecord* output
) {
  if (output == nullptr || bytes.size() != kOnboardingRecordBytes ||
      bytes[0] != kMagic0 || bytes[1] != kMagic1 ||
      get_u32(bytes.data() + 8) != crc32(bytes.first(8))) {
    return false;
  }
  const OnboardingRecord record{
      .schema_version = bytes[2],
      .checkpoint = static_cast<OnboardingCheckpoint>(bytes[3]),
  };
  if (!valid(record)) return false;
  *output = record;
  return true;
}

OnboardingLoadResult OnboardingStateMachine::load(
    bool legacy_commissioned
) {
  OnboardingRecordBytes bytes{};
  OnboardingRecord record;
  if (backend_.load(bytes) && decode_onboarding_record(bytes, &record)) {
    checkpoint_ = record.checkpoint;
    step_ = step_for(checkpoint_);
    return OnboardingLoadResult::loaded;
  }
  if (!legacy_commissioned) {
    checkpoint_ = OnboardingCheckpoint::needs_wifi;
    step_ = OnboardingStep::wifi_scan;
    return OnboardingLoadResult::first_run;
  }
  checkpoint_ = OnboardingCheckpoint::complete;
  step_ = OnboardingStep::complete;
  const OnboardingRecord migrated{
      .schema_version = 1,
      .checkpoint = OnboardingCheckpoint::complete,
  };
  if (!backend_.commit(encode_onboarding_record(migrated))) {
    return OnboardingLoadResult::storage_error;
  }
  return OnboardingLoadResult::migrated;
}

OnboardingResult OnboardingStateMachine::on_scan_complete(
    bool networks_found
) {
  if (step_ != OnboardingStep::wifi_scan || !networks_found) {
    return OnboardingResult::ignored;
  }
  step_ = OnboardingStep::wifi_select;
  return OnboardingResult::ok;
}

OnboardingResult OnboardingStateMachine::on_network_selected(bool secured) {
  if (step_ != OnboardingStep::wifi_select) {
    return OnboardingResult::ignored;
  }
  selected_network_secured_ = secured;
  step_ = secured ? OnboardingStep::wifi_password
                  : OnboardingStep::wifi_connect_verify;
  return OnboardingResult::ok;
}

OnboardingResult OnboardingStateMachine::on_credentials_submitted() {
  if (step_ != OnboardingStep::wifi_password) {
    return OnboardingResult::ignored;
  }
  step_ = OnboardingStep::wifi_connect_verify;
  return OnboardingResult::ok;
}

OnboardingResult OnboardingStateMachine::persist(
    OnboardingCheckpoint checkpoint,
    OnboardingStep step
) {
  const OnboardingRecord record{
      .schema_version = 1,
      .checkpoint = checkpoint,
  };
  if (!backend_.commit(encode_onboarding_record(record))) {
    return OnboardingResult::storage_error;
  }
  checkpoint_ = checkpoint;
  step_ = step;
  return OnboardingResult::ok;
}

OnboardingResult OnboardingStateMachine::on_wifi_connected() {
  if (step_ != OnboardingStep::wifi_connect_verify) {
    return OnboardingResult::ignored;
  }
  return persist(
      OnboardingCheckpoint::needs_ble,
      OnboardingStep::ble_pair_guide
  );
}

void OnboardingStateMachine::on_wifi_failed() {
  if (step_ != OnboardingStep::wifi_connect_verify) return;
  step_ = selected_network_secured_ ? OnboardingStep::wifi_password
                                    : OnboardingStep::wifi_select;
}

OnboardingResult OnboardingStateMachine::on_ble_state(
    bool bonded,
    bool hid_connected
) {
  if (step_ != OnboardingStep::ble_pair_guide ||
      !bonded || !hid_connected) {
    return OnboardingResult::ignored;
  }
  return persist(
      OnboardingCheckpoint::needs_agent,
      OnboardingStep::agent_install_guide
  );
}

OnboardingResult OnboardingStateMachine::on_agent_heartbeat(
    bool authenticated
) {
  if (step_ != OnboardingStep::agent_install_guide || !authenticated) {
    return OnboardingResult::ignored;
  }
  return persist(
      OnboardingCheckpoint::complete,
      OnboardingStep::complete
  );
}

OnboardingResult OnboardingStateMachine::restart_setup() {
  return persist(
      OnboardingCheckpoint::needs_wifi,
      OnboardingStep::wifi_scan
  );
}

OnboardingResult OnboardingStateMachine::previous_step() {
  if (step_ == OnboardingStep::ble_pair_guide) {
    return persist(
        OnboardingCheckpoint::needs_wifi,
        OnboardingStep::wifi_scan);
  }
  if (step_ == OnboardingStep::agent_install_guide) {
    return persist(
        OnboardingCheckpoint::needs_ble,
        OnboardingStep::ble_pair_guide);
  }
  return OnboardingResult::ignored;
}

void OnboardingStateMachine::request_wifi_scan() {
  if (checkpoint_ != OnboardingCheckpoint::needs_wifi) return;
  step_ = OnboardingStep::wifi_scan;
}

void OnboardingStateMachine::return_to_wifi_select() {
  if (checkpoint_ != OnboardingCheckpoint::needs_wifi) return;
  step_ = OnboardingStep::wifi_select;
}

OnboardingInputResult OnboardingController::begin() {
  error_.clear();
  if (state_.step() == OnboardingStep::wifi_scan) {
    return {
        .captured = false,
        .command = OnboardingCommandKind::scan_wifi,
    };
  }
  return {};
}

void OnboardingController::set_networks(
    std::span<const OnboardingNetwork> networks
) {
  networks_ = {};
  network_count_ = 0;
  for (const OnboardingNetwork& candidate : networks) {
    if (candidate.ssid.empty()) continue;
    const auto existing = std::find_if(
        networks_.begin(), networks_.begin() + network_count_,
        [&](const StoredNetwork& stored) {
          return stored.ssid == candidate.ssid;
        });
    if (existing != networks_.begin() + network_count_) {
      if (candidate.rssi > existing->rssi) {
        existing->rssi = candidate.rssi;
        existing->secured = candidate.secured;
      }
      continue;
    }
    if (network_count_ >= networks_.size()) continue;
    networks_[network_count_++] = {
        .ssid = std::string(candidate.ssid.substr(0, 32)),
        .rssi = candidate.rssi,
        .secured = candidate.secured,
    };
  }
  std::sort(
      networks_.begin(), networks_.begin() + network_count_,
      [](const StoredNetwork& lhs, const StoredNetwork& rhs) {
        if (lhs.rssi != rhs.rssi) return lhs.rssi > rhs.rssi;
        return lhs.ssid < rhs.ssid;
      });
  selected_ = 0;
  scroll_ = 0;
  editor_ = Editor::none;
  error_.clear();
  state_.on_scan_complete(true);
}

OnboardingInputResult OnboardingController::on_key(
    uint8_t physical_key,
    bool pressed,
    bool shift
) {
  if (physical_key >= captured_.size()) {
    return {.captured = true};
  }
  if (!pressed) {
    const bool was_captured = captured_[physical_key];
    captured_[physical_key] = false;
    return {.captured = was_captured};
  }
  captured_[physical_key] = true;
  if (state_.completed()) return {.captured = true};
  if (editor_ != Editor::none) {
    return edit_key(physical_key, shift);
  }
  return browse_key(physical_key);
}

OnboardingInputResult OnboardingController::browse_key(
    uint8_t physical_key
) {
  if (physical_key == kKeyBacktick &&
      state_.step() == OnboardingStep::ble_pair_guide) {
    const OnboardingResult result = state_.previous_step();
    if (result != OnboardingResult::ok) {
      error_ = "SETUP SAVE FAILED";
      return {.captured = true};
    }
    return {
        .captured = true,
        .command = OnboardingCommandKind::scan_wifi,
    };
  }
  if (physical_key == kKeyBacktick &&
      state_.step() == OnboardingStep::agent_install_guide) {
    if (state_.previous_step() != OnboardingResult::ok) {
      error_ = "SETUP SAVE FAILED";
    }
    return {.captured = true};
  }
  if (state_.step() != OnboardingStep::wifi_select) {
    return {.captured = true};
  }
  const uint8_t row_count = static_cast<uint8_t>(network_count_ + 2);
  if (physical_key == kKeyUp && selected_ > 0) {
    --selected_;
    update_scroll();
    return {.captured = true};
  }
  if (physical_key == kKeyDown && selected_ + 1 < row_count) {
    ++selected_;
    update_scroll();
    return {.captured = true};
  }
  if (physical_key == kKeyBacktick) {
    state_.request_wifi_scan();
    return {
        .captured = true,
        .command = OnboardingCommandKind::scan_wifi,
    };
  }
  if (physical_key != kKeyEnter) return {.captured = true};

  error_.clear();
  password_.clear();
  if (selected_ < network_count_) {
    const StoredNetwork& network = networks_[selected_];
    selected_ssid_ = network.ssid;
    allow_empty_password_ = !network.secured;
    state_.on_network_selected(network.secured);
    if (network.secured) {
      editor_ = Editor::password;
      editor_value_.clear();
      return {.captured = true};
    }
    return {
        .captured = true,
        .command = OnboardingCommandKind::connect_wifi,
    };
  }
  if (selected_ == network_count_) {
    selected_ssid_.clear();
    editor_value_.clear();
    allow_empty_password_ = true;
    editor_ = Editor::hidden_ssid;
    state_.on_network_selected(true);
    return {.captured = true};
  }
  state_.request_wifi_scan();
  return {
      .captured = true,
      .command = OnboardingCommandKind::scan_wifi,
  };
}

OnboardingInputResult OnboardingController::edit_key(
    uint8_t physical_key,
    bool shift
) {
  if (physical_key == kKeyBacktick) {
    editor_ = Editor::none;
    editor_value_.clear();
    password_.clear();
    error_.clear();
    state_.return_to_wifi_select();
    return {.captured = true};
  }
  if (physical_key == kKeyBackspace) {
    if (!editor_value_.empty()) editor_value_.pop_back();
    return {.captured = true};
  }
  if (physical_key == kKeyEnter) {
    if (editor_ == Editor::hidden_ssid) {
      if (editor_value_.empty()) {
        error_ = "SSID REQUIRED";
        return {.captured = true};
      }
      selected_ssid_ = editor_value_;
      editor_value_.clear();
      editor_ = Editor::password;
      return {.captured = true};
    }
    if ((!allow_empty_password_ && editor_value_.size() < 8) ||
        (!editor_value_.empty() && editor_value_.size() < 8) ||
        editor_value_.size() > 63) {
      error_ = "PASSWORD MUST BE 8-63";
      return {.captured = true};
    }
    password_ = editor_value_;
    if (state_.on_credentials_submitted() != OnboardingResult::ok) {
      return {.captured = true};
    }
    error_.clear();
    return {
        .captured = true,
        .command = OnboardingCommandKind::connect_wifi,
    };
  }
  const char character = key_character(physical_key, shift);
  if (character != '\0') {
    const std::size_t maximum =
        editor_ == Editor::hidden_ssid ? 32 : 63;
    if (editor_value_.size() < maximum) editor_value_.push_back(character);
    error_.clear();
  }
  return {.captured = true};
}

OnboardingResult OnboardingController::wifi_connected() {
  const OnboardingResult result = state_.on_wifi_connected();
  if (result == OnboardingResult::ok) {
    editor_ = Editor::none;
    editor_value_.clear();
    password_.clear();
    error_.clear();
  } else if (result == OnboardingResult::storage_error) {
    error_ = "SETUP SAVE FAILED";
  }
  return result;
}

void OnboardingController::wifi_failed(std::string_view reason) {
  state_.on_wifi_failed();
  editor_ = state_.step() == OnboardingStep::wifi_password
                ? Editor::password
                : Editor::none;
  if (editor_ == Editor::password) editor_value_ = password_;
  error_ = std::string(reason.substr(0, 32));
}

void OnboardingController::update_scroll() {
  if (selected_ < scroll_) {
    scroll_ = selected_;
  } else if (selected_ >= scroll_ + kVisibleRows) {
    scroll_ = selected_ - kVisibleRows + 1;
  }
}

char OnboardingController::key_character(
    uint8_t physical_key,
    bool shift
) {
  if (physical_key >= kPhysicalKeymap.size()) return '\0';
  const uint8_t usage = kPhysicalKeymap[physical_key].usage;
  if (usage >= 0x04 && usage <= 0x1d) {
    const char lower = static_cast<char>('a' + usage - 0x04);
    return shift ? static_cast<char>(lower - 'a' + 'A') : lower;
  }
  constexpr std::array<char, 10> plain_digits{
      '1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
  constexpr std::array<char, 10> shifted_digits{
      '!', '@', '#', '$', '%', '^', '&', '*', '(', ')'};
  if (usage >= 0x1e && usage <= 0x27) {
    const std::size_t index = usage - 0x1e;
    return shift ? shifted_digits[index] : plain_digits[index];
  }
  switch (usage) {
    case 0x2c: return ' ';
    case 0x2d: return shift ? '_' : '-';
    case 0x2e: return shift ? '+' : '=';
    case 0x2f: return shift ? '{' : '[';
    case 0x30: return shift ? '}' : ']';
    case 0x31: return shift ? '|' : '\\';
    case 0x33: return shift ? ':' : ';';
    case 0x34: return shift ? '"' : '\'';
    case 0x35: return shift ? '~' : '`';
    case 0x36: return shift ? '<' : ',';
    case 0x37: return shift ? '>' : '.';
    case 0x38: return shift ? '?' : '/';
    default: return '\0';
  }
}

OnboardingContent OnboardingController::content() const {
  OnboardingContent output;
  auto add = [&](std::string value) {
    if (output.count < output.lines.size()) {
      output.lines[output.count++] = std::move(value);
    }
  };
  switch (state_.step()) {
    case OnboardingStep::wifi_scan:
      add("SETUP 1/3 WIFI");
      add("SCANNING NETWORKS...");
      add("`=BACK");
      break;
    case OnboardingStep::wifi_select: {
      for (uint8_t index = 0; index < network_count_; ++index) {
        char row[64]{};
        std::snprintf(
            row, sizeof(row), "%c %s %ddBm%s",
            index == selected_ ? '>' : ' ', networks_[index].ssid.c_str(),
            static_cast<int>(networks_[index].rssi),
            networks_[index].secured ? " *" : "");
        add(row);
      }
      add(std::string(selected_ == network_count_ ? "> " : "  ") +
          "HIDDEN NETWORK");
      add(std::string(selected_ == network_count_ + 1 ? "> " : "  ") +
          "RESCAN");
      output.selected = selected_;
      output.scroll = scroll_;
      break;
    }
    case OnboardingStep::wifi_password:
      add(editor_ == Editor::hidden_ssid ? "WIFI SSID" : "WIFI PASSWORD");
      add(editor_ == Editor::password
              ? std::string(editor_value_.size(), '*')
              : editor_value_);
      if (!error_.empty()) add(error_);
      add("ENTER=OK `=BACK");
      break;
    case OnboardingStep::wifi_connect_verify:
      add("SETUP 1/3 WIFI");
      add("CONNECTING...");
      add(std::string("SSID:") + selected_ssid_);
      add(std::string(password_.size(), '*'));
      if (!error_.empty()) add(error_);
      break;
    case OnboardingStep::ble_pair_guide:
      add("SETUP 2/3 BLUETOOTH");
      add("PAIR FROM THIS COMPUTER");
      add("CONFIRM CODE ON DEVICE");
      break;
    case OnboardingStep::agent_install_guide:
      add("SETUP 3/3 AGENT");
      add("INSTALL MACHINE AGENT");
      add("WAITING FOR HEARTBEAT");
      break;
    case OnboardingStep::complete:
      add("SETUP COMPLETE");
      break;
  }
  return output;
}

#ifdef ESP_PLATFORM
#include "nvs.h"

namespace {
constexpr char kProductNvsNamespace[] = "product";
}

bool EspOnboardingBackend::load(std::span<uint8_t> output) {
  nvs_handle_t handle;
  if (nvs_open(kProductNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return false;
  }
  std::size_t size = output.size();
  const esp_err_t result = nvs_get_blob(
      handle, kOnboardingStorageKey.data(), output.data(), &size);
  nvs_close(handle);
  return result == ESP_OK && size == output.size();
}

bool EspOnboardingBackend::commit(std::span<const uint8_t> input) {
  nvs_handle_t handle;
  if (nvs_open(kProductNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
    return false;
  }
  esp_err_t result = nvs_set_blob(
      handle, kOnboardingStorageKey.data(), input.data(), input.size());
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result == ESP_OK;
}
#endif
