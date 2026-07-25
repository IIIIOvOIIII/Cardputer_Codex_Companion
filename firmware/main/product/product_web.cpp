#include "product/product_web.hpp"

#ifdef ESP_PLATFORM
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "cJSON.h"
#include "esp_https_server.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "product/companion_protocol.hpp"
#include "product/device_identity.hpp"
#include "product/profile.hpp"
#include "product/profile_codec.hpp"
#include "product/web_assets.hpp"
#include "product/wifi_manager.hpp"

namespace {
constexpr std::size_t kRequestLimit = 16384;
constexpr char kPairingHeader[] = "X-Cardputer-Pairing";
constexpr char kProductNvsNamespace[] = "product";
constexpr char kProfileNvsKey[] = "profile";
constexpr char kWebPinNvsKey[] = "web_pin";
httpd_handle_t g_server = nullptr;
std::array<char, 9> g_pairing_code{};
Profile g_profile = safe_profile();
StaticSemaphore_t g_profile_mutex_storage{};
SemaphoreHandle_t g_profile_mutex = nullptr;
std::atomic<ServiceState> g_ble{ServiceState::offline};
std::atomic<ServiceState> g_wifi{ServiceState::offline};
std::atomic<ServiceState> g_companion{ServiceState::offline};
std::atomic<CodexAction> g_pending_codex_action{CodexAction::none};
std::atomic<uint32_t> g_action_sequence{0};
ProductCompanionSnapshotHandler g_snapshot_handler = nullptr;
ProductCompanionHeartbeatHandler g_heartbeat_handler = nullptr;
PetStore* g_pet_store = nullptr;

class ProfileLock {
 public:
  ProfileLock() {
    locked_ = g_profile_mutex != nullptr &&
              xSemaphoreTake(g_profile_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
  }
  ~ProfileLock() {
    if (locked_) xSemaphoreGive(g_profile_mutex);
  }
  [[nodiscard]] bool locked() const { return locked_; }

 private:
  bool locked_ = false;
};

bool authorized(httpd_req_t* request) {
  const size_t length = httpd_req_get_hdr_value_len(request, kPairingHeader);
  if (length != kProductWebPinLength) return false;
  std::array<char, 9> supplied{};
  return httpd_req_get_hdr_value_str(request, kPairingHeader, supplied.data(),
                                     supplied.size()) == ESP_OK &&
         std::memcmp(supplied.data(), g_pairing_code.data(),
                     kProductWebPinLength) == 0;
}

esp_err_t json_response(httpd_req_t* request, const char* json,
                        const char* status = "200 OK") {
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_sendstr(request, json);
}

esp_err_t reject_pairing(httpd_req_t* request) {
  return json_response(request, "{\"error\":\"pairing_required\"}",
                       "401 Unauthorized");
}

std::string read_body(httpd_req_t* request) {
  if (request->content_len <= 0 ||
      request->content_len > static_cast<int>(kRequestLimit)) {
    return {};
  }
  std::string body(static_cast<std::size_t>(request->content_len), '\0');
  std::size_t received = 0;
  while (received < body.size()) {
    const int count = httpd_req_recv(
        request, body.data() + received, body.size() - received);
    if (count <= 0) return {};
    received += static_cast<std::size_t>(count);
  }
  return body;
}

std::unique_ptr<uint8_t[]> read_binary_body(httpd_req_t* request,
                                            std::size_t length) {
  auto body = std::unique_ptr<uint8_t[]>(
      new (std::nothrow) uint8_t[length]);
  if (body == nullptr) return {};
  std::size_t received = 0;
  while (received < length) {
    const int count = httpd_req_recv(
        request, reinterpret_cast<char*>(body.get() + received),
        length - received);
    if (count <= 0) return {};
    received += static_cast<std::size_t>(count);
  }
  return body;
}

bool read_header(httpd_req_t* request, const char* name,
                 std::string* output) {
  const std::size_t length = httpd_req_get_hdr_value_len(request, name);
  if (length == 0 || length > 128) return false;
  std::string value(length + 1, '\0');
  if (httpd_req_get_hdr_value_str(request, name, value.data(),
                                  value.size()) != ESP_OK) {
    return false;
  }
  value.resize(length);
  *output = std::move(value);
  return true;
}

bool parse_sha256(std::string_view text, std::array<uint8_t, 32>* output) {
  if (text.size() != 64 || output == nullptr) return false;
  auto nibble = [](char value) -> int {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
  };
  for (std::size_t index = 0; index < output->size(); ++index) {
    const int high = nibble(text[index * 2]);
    const int low = nibble(text[index * 2 + 1]);
    if (high < 0 || low < 0) return false;
    (*output)[index] = static_cast<uint8_t>((high << 4) | low);
  }
  return true;
}

esp_err_t persist_profile(const char* json) {
  nvs_handle_t handle;
  esp_err_t result =
      nvs_open(kProductNvsNamespace, NVS_READWRITE, &handle);
  if (result != ESP_OK) return result;
  result = nvs_set_str(handle, kProfileNvsKey, json);
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result;
}

void load_profile() {
  nvs_handle_t handle;
  if (nvs_open(kProductNvsNamespace, NVS_READONLY, &handle) != ESP_OK) return;
  size_t size = 0;
  if (nvs_get_str(handle, kProfileNvsKey, nullptr, &size) != ESP_OK ||
      size == 0 || size > kRequestLimit) {
    nvs_close(handle);
    return;
  }
  std::string json(size, '\0');
  if (nvs_get_str(handle, kProfileNvsKey, json.data(), &size) == ESP_OK) {
    auto loaded = std::make_unique<Profile>();
    json.resize(std::strlen(json.c_str()));
    if (decode_profile(json, *loaded) == ProfileCodecResult::ok) {
      g_profile = std::move(*loaded);
    }
  }
  nvs_close(handle);
}

void set_pairing_code(std::string_view pin) {
  if (!product_web_pin_is_valid(pin)) return;
  std::memset(g_pairing_code.data(), 0, g_pairing_code.size());
  std::memcpy(g_pairing_code.data(), pin.data(), kProductWebPinLength);
}

void generate_pairing_code() {
  std::snprintf(g_pairing_code.data(), g_pairing_code.size(), "%08lu",
                static_cast<unsigned long>(esp_random() % 100000000u));
}

void load_pairing_code() {
  nvs_handle_t handle;
  const esp_err_t open_result =
      nvs_open(kProductNvsNamespace, NVS_READWRITE, &handle);
  std::array<char, kProductWebPinLength + 1> stored{};
  bool stored_found = false;
  bool stored_valid = false;
  if (open_result == ESP_OK) {
    size_t size = stored.size();
    stored_found =
        nvs_get_str(handle, kWebPinNvsKey, stored.data(), &size) == ESP_OK;
    stored_valid = stored_found && product_web_pin_is_valid(stored.data());
  }

  switch (product_web_pin_load_action(open_result == ESP_OK,
                                      stored_found,
                                      stored_valid)) {
    case ProductWebPinLoadAction::use_stored:
      set_pairing_code(stored.data());
      break;
    case ProductWebPinLoadAction::generate_and_persist:
      generate_pairing_code();
      if (nvs_set_str(handle, kWebPinNvsKey, g_pairing_code.data()) ==
          ESP_OK) {
        nvs_commit(handle);
      }
      break;
    case ProductWebPinLoadAction::generate_ephemeral:
      generate_pairing_code();
      break;
  }
  if (open_result == ESP_OK) nvs_close(handle);
}

esp_err_t persist_pairing_code(std::string_view pin) {
  if (!product_web_pin_is_valid(pin)) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  esp_err_t result =
      nvs_open(kProductNvsNamespace, NVS_READWRITE, &handle);
  if (result != ESP_OK) return result;
  const std::string pin_copy(pin);
  result = nvs_set_str(handle, kWebPinNvsKey, pin_copy.c_str());
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  if (result == ESP_OK) set_pairing_code(pin);
  return result;
}

esp_err_t root_handler(httpd_req_t* request) {
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request, kProductWebHtml, kProductWebHtmlSize);
}

esp_err_t status_handler(httpd_req_t* request) {
  char json[192]{};
  const ServiceState ble = g_ble.load();
  const ServiceState wifi = g_wifi.load();
  const ServiceState companion = g_companion.load();
  std::snprintf(json, sizeof(json),
                "{\"product\":\"Cardputer Codex Companion\","
                "\"version\":\"%.*s\",\"ble\":\"%.*s\","
                "\"wifi\":\"%.*s\",\"companion\":\"%.*s\","
                "\"ip\":\"%s\"}",
                static_cast<int>(kProductVersion.size()),
                kProductVersion.data(),
                static_cast<int>(to_string(ble).size()), to_string(ble).data(),
                static_cast<int>(to_string(wifi).size()), to_string(wifi).data(),
                static_cast<int>(to_string(companion).size()),
                to_string(companion).data(), product_wifi_ipv4());
  return json_response(request, json);
}

esp_err_t get_profile_handler(httpd_req_t* request) {
  if (!authorized(request)) return reject_pairing(request);
  ProfileLock lock;
  if (!lock.locked()) return ESP_ERR_TIMEOUT;
  std::string json;
  return encode_profile(g_profile, json) == ProfileCodecResult::ok
             ? json_response(request, json.c_str())
             : ESP_ERR_NO_MEM;
}

esp_err_t put_profile_handler(httpd_req_t* request) {
  if (!authorized(request)) return reject_pairing(request);
  const std::string body = read_body(request);
  auto candidate = std::make_unique<Profile>();
  if (decode_profile(body, *candidate) != ProfileCodecResult::ok) {
    return json_response(request, "{\"error\":\"invalid_profile\"}",
                         "400 Bad Request");
  }
  ProfileLock lock;
  if (!lock.locked()) return ESP_ERR_TIMEOUT;
  if (candidate->revision != g_profile.revision) {
    return json_response(request, "{\"error\":\"revision_conflict\"}",
                         "409 Conflict");
  }
  candidate->revision += 1;
  std::string json;
  if (encode_profile(*candidate, json) != ProfileCodecResult::ok) {
    return ESP_ERR_NO_MEM;
  }
  const esp_err_t persisted = persist_profile(json.c_str());
  if (product_web_profile_activation(persisted == ESP_OK) ==
      ProductWebProfileActivation::keep_active) {
    return json_response(
        request,
        "{\"error\":\"profile_persist_failed\"}",
        "500 Internal Server Error");
  }
  g_profile = std::move(*candidate);
  return json_response(request, json.c_str());
}

esp_err_t wifi_handler(httpd_req_t* request) {
  if (!authorized(request)) return reject_pairing(request);
  const std::string body = read_body(request);
  cJSON* root = body.empty() ? nullptr : cJSON_ParseWithLength(body.data(), body.size());
  const cJSON* ssid = root == nullptr ? nullptr
      : cJSON_GetObjectItemCaseSensitive(root, "ssid");
  const cJSON* password = root == nullptr ? nullptr
      : cJSON_GetObjectItemCaseSensitive(root, "password");
  if (!cJSON_IsString(ssid) || !cJSON_IsString(password)) {
    cJSON_Delete(root);
    return json_response(request, "{\"error\":\"invalid_wifi\"}",
                         "400 Bad Request");
  }
  const esp_err_t saved =
      product_wifi_save(ssid->valuestring, password->valuestring);
  cJSON_Delete(root);
  return saved == ESP_OK
             ? json_response(request, "{\"saved\":true}")
             : json_response(request, "{\"error\":\"wifi_save_failed\"}",
                             "400 Bad Request");
}

esp_err_t pin_handler(httpd_req_t* request) {
  if (!authorized(request)) return reject_pairing(request);
  const std::string body = read_body(request);
  cJSON* root = body.empty() ? nullptr : cJSON_ParseWithLength(body.data(), body.size());
  const cJSON* pin = root == nullptr ? nullptr
      : cJSON_GetObjectItemCaseSensitive(root, "pin");
  if (!cJSON_IsString(pin) || !product_web_pin_is_valid(pin->valuestring)) {
    cJSON_Delete(root);
    return json_response(request, "{\"error\":\"invalid_pin\"}",
                         "400 Bad Request");
  }
  const esp_err_t saved = persist_pairing_code(pin->valuestring);
  cJSON_Delete(root);
  return saved == ESP_OK
             ? json_response(request, "{\"saved\":true}")
             : json_response(request, "{\"error\":\"pin_save_failed\"}",
                             "400 Bad Request");
}

void note_companion_activity() {
  if (g_heartbeat_handler != nullptr) g_heartbeat_handler();
}

esp_err_t companion_status_handler(httpd_req_t* request) {
  if (!authorized(request)) return reject_pairing(request);
  note_companion_activity();
  const std::string body = read_body(request);
  if (body.empty() || g_snapshot_handler == nullptr) {
    return json_response(request, "{\"error\":\"invalid_snapshot\"}",
                         "400 Bad Request");
  }
  g_snapshot_handler(body);
  return json_response(request, "{\"accepted\":true}");
}

esp_err_t companion_action_handler(httpd_req_t* request) {
  if (!authorized(request)) return reject_pairing(request);
  note_companion_activity();
  const bool needs_snapshot =
      product_web_companion_needs_snapshot(g_companion.load());
  const CodexAction action =
      g_pending_codex_action.exchange(CodexAction::none);
  const uint32_t sequence = g_action_sequence.load();
  const std::string_view name = codex_action_name(action);
  char json[128]{};
  std::snprintf(json, sizeof(json),
                "{\"sequence\":%lu,\"action\":\"%.*s\","
                "\"needs_snapshot\":%s}",
                static_cast<unsigned long>(sequence),
                static_cast<int>(name.size()), name.data(),
                needs_snapshot ? "true" : "false");
  return json_response(request, json);
}

esp_err_t pet_status_response(httpd_req_t* request) {
  if (g_pet_store == nullptr) {
    return json_response(request, "{\"error\":\"pet_store_unavailable\"}",
                         "503 Service Unavailable");
  }
  const PetStoreStatus status = g_pet_store->status();
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "pet_id", status.pet_id.c_str());
  cJSON_AddStringToObject(root, "digest", status.digest.c_str());
  cJSON_AddNumberToObject(root, "format_version", status.format_version);
  cJSON_AddNumberToObject(root, "storage_used", status.storage_used);
  cJSON* transaction = cJSON_AddObjectToObject(root, "transaction");
  cJSON_AddBoolToObject(transaction, "active", status.transaction.active);
  cJSON_AddStringToObject(transaction, "id",
                          status.transaction.transaction_id.c_str());
  cJSON_AddNumberToObject(transaction, "received",
                          status.transaction.received);
  cJSON_AddNumberToObject(transaction, "expected",
                          status.transaction.expected);
  cJSON_AddStringToObject(root, "last_result", status.last_result.c_str());
  char* json = cJSON_PrintUnformatted(root);
  const esp_err_t result =
      json == nullptr ? ESP_ERR_NO_MEM : json_response(request, json);
  cJSON_free(json);
  cJSON_Delete(root);
  return result;
}

esp_err_t pet_status_handler(httpd_req_t* request) {
  if (!authorized(request)) return reject_pairing(request);
  note_companion_activity();
  return pet_status_response(request);
}

esp_err_t pet_begin_handler(httpd_req_t* request) {
  if (!authorized(request)) return reject_pairing(request);
  note_companion_activity();
  if (g_pet_store == nullptr) {
    return json_response(request, "{\"error\":\"pet_store_unavailable\"}",
                         "503 Service Unavailable");
  }
  const std::string body = read_body(request);
  cJSON* root =
      body.empty() ? nullptr : cJSON_ParseWithLength(body.data(), body.size());
  const cJSON* pet_id =
      root == nullptr ? nullptr : cJSON_GetObjectItem(root, "pet_id");
  const cJSON* format =
      root == nullptr ? nullptr : cJSON_GetObjectItem(root, "format_version");
  const cJSON* length =
      root == nullptr ? nullptr : cJSON_GetObjectItem(root, "length");
  const cJSON* sha =
      root == nullptr ? nullptr : cJSON_GetObjectItem(root, "sha256");
  PetUploadBegin begin;
  const bool parsed =
      cJSON_IsString(pet_id) && cJSON_IsNumber(format) &&
      cJSON_IsNumber(length) && cJSON_IsString(sha) &&
      length->valuedouble >= 1 &&
      length->valuedouble <= kPetBundleMaximumBytes &&
      parse_sha256(sha->valuestring, &begin.upload_digest);
  if (parsed) {
    begin.pet_id = pet_id->valuestring;
    begin.format_version = static_cast<uint16_t>(format->valueint);
    begin.length = static_cast<std::size_t>(length->valuedouble);
  }
  cJSON_Delete(root);
  if (!parsed) {
    return json_response(request, "{\"error\":\"invalid_request\"}",
                         "400 Bad Request");
  }
  PetUploadStatus status;
  const esp_err_t result = g_pet_store->begin(begin, &status);
  if (result == ESP_ERR_INVALID_STATE) {
    return json_response(request, "{\"error\":\"upload_in_progress\"}",
                         "409 Conflict");
  }
  if (result != ESP_OK) {
    return json_response(request, "{\"error\":\"pet_store_failed\"}",
                         "500 Internal Server Error");
  }
  char json[96]{};
  std::snprintf(json, sizeof(json),
                "{\"transaction_id\":\"%s\",\"received\":%lu}",
                status.transaction_id.c_str(),
                static_cast<unsigned long>(status.received));
  return json_response(request, json);
}

esp_err_t pet_chunk_handler(httpd_req_t* request) {
  if (!authorized(request)) return reject_pairing(request);
  note_companion_activity();
  if (g_pet_store == nullptr) {
    return json_response(request, "{\"error\":\"pet_store_unavailable\"}",
                         "503 Service Unavailable");
  }
  if (request->content_len <= 0 || request->content_len > 8192) {
    return json_response(request, "{\"error\":\"chunk_too_large\"}",
                         "413 Payload Too Large");
  }
  std::string transaction;
  std::string offset_text;
  std::string digest_text;
  std::array<uint8_t, 32> digest{};
  if (!read_header(request, "X-Pet-Transaction", &transaction) ||
      !read_header(request, "X-Pet-Offset", &offset_text) ||
      !read_header(request, "X-Pet-Chunk-SHA256", &digest_text) ||
      !parse_sha256(digest_text, &digest)) {
    return json_response(request, "{\"error\":\"invalid_request\"}",
                         "400 Bad Request");
  }
  char* end = nullptr;
  const unsigned long long offset =
      std::strtoull(offset_text.c_str(), &end, 10);
  if (end == nullptr || *end != '\0') {
    return json_response(request, "{\"error\":\"invalid_offset\"}",
                         "400 Bad Request");
  }
  const std::size_t body_size =
      static_cast<std::size_t>(request->content_len);
  auto body = read_binary_body(request, body_size);
  if (body == nullptr) {
    return json_response(request, "{\"error\":\"invalid_request\"}",
                         "400 Bad Request");
  }
  const esp_err_t result = g_pet_store->append_owned(
      transaction, static_cast<std::size_t>(offset), std::move(body),
      body_size, digest);
  if (result == ESP_ERR_INVALID_STATE) {
    return json_response(request, "{\"error\":\"transaction_mismatch\"}",
                         "409 Conflict");
  }
  if (result == ESP_ERR_INVALID_SIZE) {
    return json_response(request, "{\"error\":\"invalid_offset\"}",
                         "400 Bad Request");
  }
  if (result == ESP_ERR_INVALID_CRC) {
    return json_response(request, "{\"error\":\"invalid_digest\"}",
                         "400 Bad Request");
  }
  if (result != ESP_OK) {
    return json_response(request, "{\"error\":\"pet_store_failed\"}",
                         "500 Internal Server Error");
  }
  return json_response(request, "{\"accepted\":true}");
}

esp_err_t pet_commit_handler(httpd_req_t* request) {
  if (!authorized(request)) return reject_pairing(request);
  note_companion_activity();
  if (g_pet_store == nullptr) {
    return json_response(request, "{\"error\":\"pet_store_unavailable\"}",
                         "503 Service Unavailable");
  }
  const std::string body = read_body(request);
  cJSON* root =
      body.empty() ? nullptr : cJSON_ParseWithLength(body.data(), body.size());
  const cJSON* transaction =
      root == nullptr ? nullptr : cJSON_GetObjectItem(root, "transaction_id");
  const std::string id =
      cJSON_IsString(transaction) ? transaction->valuestring : "";
  cJSON_Delete(root);
  if (id.empty()) {
    return json_response(request, "{\"error\":\"invalid_request\"}",
                         "400 Bad Request");
  }
  const esp_err_t result = g_pet_store->commit(id);
  if (result == ESP_ERR_INVALID_STATE) {
    return json_response(request, "{\"error\":\"transaction_mismatch\"}",
                         "409 Conflict");
  }
  if (result == ESP_ERR_INVALID_CRC) {
    return json_response(request, "{\"error\":\"invalid_bundle\"}",
                         "400 Bad Request");
  }
  if (result != ESP_OK) {
    return json_response(request, "{\"error\":\"pet_store_failed\"}",
                         "500 Internal Server Error");
  }
  return pet_status_response(request);
}
}  // namespace

esp_err_t product_web_start() {
  if (g_server != nullptr) return ESP_ERR_INVALID_STATE;
  if (g_profile_mutex == nullptr) {
    g_profile_mutex = xSemaphoreCreateMutexStatic(&g_profile_mutex_storage);
  }
  if (g_profile_mutex == nullptr) return ESP_ERR_NO_MEM;
  load_pairing_code();
  load_profile();
  DeviceTlsIdentity identity;
  esp_err_t result = load_or_create_device_tls_identity(&identity);
  if (result != ESP_OK) return result;
  httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
  config.httpd.server_port = 443;
  config.httpd.max_uri_handlers = kProductWebRoutes.size();
  config.httpd.stack_size = 10240;
  config.servercert = identity.certificate;
  config.servercert_len = identity.certificate_length;
  config.prvtkey_pem = identity.private_key;
  config.prvtkey_len = identity.private_key_length;
  result = httpd_ssl_start(&g_server, &config);
  if (result != ESP_OK) return result;
  const std::array<httpd_uri_t, 12> routes{{
      {.uri = "/", .method = HTTP_GET, .handler = root_handler},
      {.uri = "/api/v1/status", .method = HTTP_GET, .handler = status_handler},
      {.uri = "/api/v1/profile", .method = HTTP_GET,
       .handler = get_profile_handler},
      {.uri = "/api/v1/profile", .method = HTTP_PUT,
       .handler = put_profile_handler},
      {.uri = "/api/v1/wifi", .method = HTTP_POST, .handler = wifi_handler},
      {.uri = "/api/v1/pin", .method = HTTP_POST, .handler = pin_handler},
      {.uri = "/api/v1/companion/status", .method = HTTP_POST,
       .handler = companion_status_handler},
      {.uri = "/api/v1/companion/action", .method = HTTP_GET,
       .handler = companion_action_handler},
      {.uri = "/api/v1/companion/pet/begin", .method = HTTP_POST,
       .handler = pet_begin_handler},
      {.uri = "/api/v1/companion/pet/chunk", .method = HTTP_PUT,
       .handler = pet_chunk_handler},
      {.uri = "/api/v1/companion/pet/commit", .method = HTTP_POST,
       .handler = pet_commit_handler},
      {.uri = "/api/v1/companion/pet", .method = HTTP_GET,
       .handler = pet_status_handler},
  }};
  for (const auto& route : routes) {
    result = httpd_register_uri_handler(g_server, &route);
    if (result != ESP_OK) return result;
  }
  return ESP_OK;
}

const char* product_web_pairing_code() {
  return g_pairing_code.data();
}

void product_web_set_status(ServiceState ble, ServiceState wifi,
                            ServiceState companion) {
  g_ble.store(ble);
  g_wifi.store(wifi);
  g_companion.store(companion);
}

void product_web_set_companion_snapshot_handler(
    ProductCompanionSnapshotHandler handler) {
  g_snapshot_handler = handler;
}

void product_web_set_companion_heartbeat_handler(
    ProductCompanionHeartbeatHandler handler) {
  g_heartbeat_handler = handler;
}

void product_web_set_pet_store(PetStore* store) {
  g_pet_store = store;
}

bool product_web_action(uint8_t layer, uint8_t physical_key,
                        KeyAction* action) {
  if (action == nullptr || layer >= kProfileLayerCount ||
      physical_key >= kPhysicalKeyCount) {
    return false;
  }
  ProfileLock lock;
  if (!lock.locked()) return false;
  *action =
      g_profile.bindings[layer * kPhysicalKeyCount + physical_key].action;
  return true;
}

bool product_web_profile_name(char* output, std::size_t output_size) {
  if (output == nullptr || output_size == 0) return false;
  ProfileLock lock;
  if (!lock.locked()) return false;
  std::snprintf(output, output_size, "%s", g_profile.name.c_str());
  return true;
}

void product_web_queue_codex_action(CodexAction action) {
  if (action == CodexAction::none) return;
  g_pending_codex_action.store(action);
  g_action_sequence.fetch_add(1);
}
#endif
