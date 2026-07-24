#include "probe/web_handlers.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "probe/web_async_dispatch.hpp"
#include "probe/web_route_manifest.hpp"

namespace {

constexpr size_t kPairingBodyLimit = 512;
constexpr size_t kStreamChunkBytes = 1024;
constexpr size_t kWebSocketFrameLimit = 16384;
constexpr char kTag[] = "web-handlers";

uint64_t now_ms() {
  return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}

WebHandlerContext* context(httpd_req_t* request) {
  return static_cast<WebHandlerContext*>(request->user_ctx);
}

bool normalize_source(httpd_req_t* request, SourceKey& source) {
  sockaddr_storage peer{};
  socklen_t peer_length = sizeof(peer);
  const int socket = httpd_req_to_sockfd(request);
  if (socket < 0 ||
      getpeername(socket, reinterpret_cast<sockaddr*>(&peer), &peer_length) !=
          0) {
    return false;
  }
  if (peer.ss_family == AF_INET) {
    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(&peer);
    source.bytes.fill(0);
    source.bytes[10] = 0xff;
    source.bytes[11] = 0xff;
    std::memcpy(source.bytes.data() + 12, &ipv4->sin_addr, 4);
    return true;
  }
  if (peer.ss_family == AF_INET6) {
    const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(&peer);
    std::memcpy(source.bytes.data(), &ipv6->sin6_addr, source.bytes.size());
    return true;
  }
  return false;
}

template <size_t Capacity>
bool read_header(httpd_req_t* request, const char* name,
                 std::array<char, Capacity>& storage,
                 std::string_view& value, uint32_t& header_bytes) {
  const size_t length = httpd_req_get_hdr_value_len(request, name);
  if (length == 0) {
    value = {};
    return true;
  }
  if (length >= storage.size() ||
      header_bytes > UINT32_MAX - static_cast<uint32_t>(length)) {
    return false;
  }
  if (httpd_req_get_hdr_value_str(request, name, storage.data(),
                                  storage.size()) != ESP_OK) {
    return false;
  }
  header_bytes += static_cast<uint32_t>(length);
  value = std::string_view(storage.data(), length);
  return true;
}

std::string_view cookie_token(std::string_view cookie) {
  constexpr std::string_view prefix = "cp_admin=";
  size_t start = 0;
  while (start < cookie.size()) {
    while (start < cookie.size() &&
           (cookie[start] == ' ' || cookie[start] == ';')) {
      ++start;
    }
    const size_t end = cookie.find(';', start);
    const size_t field_end = end == std::string_view::npos ? cookie.size() : end;
    const std::string_view field = cookie.substr(start, field_end - start);
    if (field.starts_with(prefix)) {
      return field.substr(prefix.size());
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return {};
}

HttpMethod method_for(int method) {
  switch (method) {
    case HTTP_POST:
      return HttpMethod::post;
    case HTTP_PUT:
      return HttpMethod::put;
    case HTTP_DELETE:
      return HttpMethod::delete_;
    default:
      return HttpMethod::get;
  }
}

const char* reject_name(WebReject reason) {
  switch (reason) {
    case WebReject::none:
      return "none";
    case WebReject::rate_limited:
      return "rate_limited";
    case WebReject::header_too_large:
      return "header_too_large";
    case WebReject::body_too_large:
      return "body_too_large";
    case WebReject::json_too_deep:
      return "json_too_deep";
    case WebReject::frame_too_large:
      return "frame_too_large";
    case WebReject::unauthenticated:
      return "unauthenticated";
    case WebReject::session_expired:
      return "session_expired";
    case WebReject::host_mismatch:
      return "host_mismatch";
    case WebReject::origin_mismatch:
      return "origin_mismatch";
    case WebReject::csrf_mismatch:
      return "csrf_mismatch";
    case WebReject::pairing_required:
      return "pairing_required";
  }
  return "rejected";
}

const char* status_line(uint16_t status_code) {
  switch (status_code) {
    case 200:
      return "200 OK";
    case 401:
      return "401 Unauthorized";
    case 403:
      return "403 Forbidden";
    case 408:
      return "408 Request Timeout";
    case 413:
      return "413 Payload Too Large";
    case 429:
      return "429 Too Many Requests";
    case 503:
      return "503 Service Unavailable";
    default:
      return "400 Bad Request";
  }
}

esp_err_t send_json(httpd_req_t* request, uint16_t status_code,
                    std::string_view body) {
  httpd_resp_set_status(request, status_line(status_code));
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request, body.data(), body.size());
}

class HttpdPairingAsyncBackend final : public PairingAsyncBackend {
 public:
  explicit HttpdPairingAsyncBackend(QueueHandle_t queue) : queue_(queue) {}

  bool begin(void* request, void** async_request) override {
    httpd_req_t* copy = nullptr;
    if (httpd_req_async_handler_begin(static_cast<httpd_req_t*>(request),
                                      &copy) != ESP_OK) {
      return false;
    }
    *async_request = copy;
    return true;
  }

  bool enqueue(void* async_request) override {
    auto* request = static_cast<httpd_req_t*>(async_request);
    return xQueueSend(queue_, &request, 0) == pdTRUE;
  }

  bool send_unavailable(void* async_request) override {
    return send_json(static_cast<httpd_req_t*>(async_request), 503,
                     "{\"error\":\"pairing_worker\"}") == ESP_OK;
  }

  void complete(void* async_request) override {
    httpd_req_async_handler_complete(
        static_cast<httpd_req_t*>(async_request));
  }

 private:
  QueueHandle_t queue_;
};

esp_err_t send_reject(httpd_req_t* request, WebDecision decision) {
  std::array<char, 96> body{};
  const int length =
      std::snprintf(body.data(), body.size(), "{\"error\":\"%s\"}",
                    reject_name(decision.reason));
  if (length < 0 || static_cast<size_t>(length) >= body.size()) {
    return ESP_FAIL;
  }
  return send_json(request, decision.http_status_code,
                   std::string_view(body.data(), static_cast<size_t>(length)));
}

esp_err_t send_pairing_resolution(
    httpd_req_t* request, PairingResolutionResult resolution) {
  if (resolution.resolution == PairingResolution::accepted &&
      resolution.credential.has_value()) {
    const AdminCredential& credential = *resolution.credential;
    std::array<char, 160> cookie{};
    const int cookie_length =
        std::snprintf(cookie.data(), cookie.size(),
                      "cp_admin=%s; Secure; HttpOnly; SameSite=Strict; Path=/",
                      credential.cookie_token.c_str());
    if (cookie_length < 0 ||
        static_cast<size_t>(cookie_length) >= cookie.size() ||
        httpd_resp_set_hdr(request, "Set-Cookie", cookie.data()) != ESP_OK) {
      return ESP_FAIL;
    }

    std::array<char, 96> response{};
    const int length =
        std::snprintf(response.data(), response.size(),
                      "{\"csrf_token\":\"%s\"}",
                      credential.csrf_token.c_str());
    if (length < 0 || static_cast<size_t>(length) >= response.size()) {
      return ESP_FAIL;
    }
    return send_json(
        request, 200,
        std::string_view(response.data(), static_cast<size_t>(length)));
  }
  if (resolution.resolution == PairingResolution::rejected) {
    return send_json(request, 403, "{\"error\":\"physical_rejected\"}");
  }
  if (resolution.resolution == PairingResolution::capacity) {
    return send_json(request, 503, "{\"error\":\"session_capacity\"}");
  }
  return send_json(request, 408, "{\"error\":\"physical_timeout\"}");
}

bool authorize(httpd_req_t* request, WebDecision& decision) {
  WebHandlerContext* current = context(request);
  if (current == nullptr || request->content_len > UINT32_MAX) {
    decision = {.reason = WebReject::body_too_large,
                .http_status_code = 413};
    return false;
  }

  std::array<char, 256> host_storage{};
  std::array<char, 256> origin_storage{};
  std::array<char, 512> cookie_storage{};
  std::array<char, 128> csrf_storage{};
  std::string_view host;
  std::string_view origin;
  std::string_view cookie;
  std::string_view csrf;
  uint32_t header_bytes = 0;
  if (!read_header(request, "Host", host_storage, host, header_bytes) ||
      !read_header(request, "Origin", origin_storage, origin, header_bytes) ||
      !read_header(request, "Cookie", cookie_storage, cookie, header_bytes) ||
      !read_header(request, "X-CSRF-Token", csrf_storage, csrf,
                   header_bytes)) {
    decision = {.reason = WebReject::header_too_large,
                .http_status_code = 413};
    return false;
  }

  SourceKey source{};
  if (!normalize_source(request, source)) {
    decision = {.reason = WebReject::rate_limited,
                .http_status_code = 429};
    return false;
  }

  const RequestMeta metadata{
      .method = method_for(request->method),
      .path = request->uri,
      .source = source,
      .host = host,
      .origin = origin,
      .cookie_token = cookie_token(cookie),
      .csrf_token = csrf,
      .header_bytes = header_bytes,
      .content_length = static_cast<uint32_t>(request->content_len),
      .now_ms = now_ms(),
  };
  decision = current->authorize(metadata);
  return decision.reason == WebReject::none;
}

bool receive_body(httpd_req_t* request, RequestBudget& budget,
                  JsonDepthTracker* depth) {
  std::array<char, kStreamChunkBytes> chunk{};
  size_t remaining = request->content_len;
  while (remaining > 0) {
    const size_t wanted = std::min(remaining, chunk.size());
    const int received = httpd_req_recv(request, chunk.data(), wanted);
    if (received <= 0 ||
        !budget.consume(static_cast<uint32_t>(received)) ||
        (depth != nullptr &&
         !depth->consume(
             std::string_view(chunk.data(), static_cast<size_t>(received))))) {
      return false;
    }
    remaining -= static_cast<size_t>(received);
  }
  return true;
}

esp_err_t health_handler(httpd_req_t* request) {
  WebDecision decision{};
  if (!authorize(request, decision)) {
    return send_reject(request, decision);
  }
  const bool pairing_required = !context(request)->has_admin_session();
  return send_json(
      request, 200,
      pairing_required
          ? "{\"ok\":true,\"firmware_version\":\"phase0-probe\","
            "\"pairing_required\":true}"
          : "{\"ok\":true,\"firmware_version\":\"phase0-probe\","
            "\"pairing_required\":false}");
}

esp_err_t pairing_submit_handler(httpd_req_t* request) {
  WebDecision decision{};
  if (!authorize(request, decision)) {
    return send_reject(request, decision);
  }
  if (request->content_len == 0 || request->content_len > kPairingBodyLimit) {
    return send_reject(
        request,
        {.reason = WebReject::body_too_large, .http_status_code = 413});
  }

  std::array<char, kPairingBodyLimit + 1> body{};
  RequestBudget budget = RequestBudget::for_request(
      request->uri, 0, static_cast<uint32_t>(request->content_len));
  size_t remaining = request->content_len;
  size_t offset = 0;
  while (remaining > 0) {
    const int received =
        httpd_req_recv(request, body.data() + offset, remaining);
    if (received <= 0 ||
        !budget.consume(static_cast<uint32_t>(received))) {
      return ESP_FAIL;
    }
    offset += static_cast<size_t>(received);
    remaining -= static_cast<size_t>(received);
  }

  cJSON* payload = cJSON_ParseWithLength(body.data(), offset);
  if (payload == nullptr) {
    return send_json(request, 400, "{\"error\":\"invalid_json\"}");
  }
  const cJSON* code = cJSON_GetObjectItemCaseSensitive(payload, "code");
  const cJSON* browser =
      cJSON_GetObjectItemCaseSensitive(payload, "browser_name");
  if (!cJSON_IsString(code) || !cJSON_IsString(browser) ||
      code->valuestring == nullptr || browser->valuestring == nullptr) {
    cJSON_Delete(payload);
    return send_json(request, 400, "{\"error\":\"invalid_pairing_request\"}");
  }
  const PairingResult result = context(request)->submit_pairing_code(
      code->valuestring, browser->valuestring, now_ms());
  cJSON_Delete(payload);

  if (result == PairingResult::in_backoff ||
      result == PairingResult::backoff_started) {
    httpd_resp_set_hdr(request, "Retry-After", "600");
    return send_json(request, 429, "{\"error\":\"pairing_backoff\"}");
  }
  if (result != PairingResult::awaiting_physical_confirmation) {
    return send_json(request, 403, "{\"error\":\"pairing_required\"}");
  }
  return context(request)->defer_pairing_response(request);
}

esp_err_t session_handler(httpd_req_t* request) {
  WebDecision decision{};
  if (!authorize(request, decision)) {
    return send_reject(request, decision);
  }

  std::array<char, 512> cookie_storage{};
  std::string_view cookie;
  uint32_t header_bytes = 0;
  if (!read_header(request, "Cookie", cookie_storage, cookie, header_bytes)) {
    return send_reject(
        request,
        {.reason = WebReject::header_too_large, .http_status_code = 413});
  }
  const std::optional<std::string> csrf_token =
      context(request)->issue_csrf_token(cookie_token(cookie), now_ms());
  if (!csrf_token.has_value()) {
    return send_reject(
        request,
        {.reason = WebReject::unauthenticated, .http_status_code = 401});
  }

  std::array<char, 128> response{};
  const int length =
      std::snprintf(response.data(), response.size(),
                    "{\"authenticated\":true,\"csrf_token\":\"%s\"}",
                    csrf_token->c_str());
  if (length < 0 || static_cast<size_t>(length) >= response.size()) {
    return ESP_FAIL;
  }
  return send_json(
      request, 200,
      std::string_view(response.data(), static_cast<size_t>(length)));
}

esp_err_t echo_handler(httpd_req_t* request) {
  WebDecision decision{};
  if (!authorize(request, decision)) {
    return send_reject(request, decision);
  }
  RequestBudget budget = RequestBudget::for_request(
      request->uri, 0, static_cast<uint32_t>(request->content_len));
  if (!receive_body(request, budget, nullptr)) {
    return send_reject(
        request, {.reason = WebReject::body_too_large,
                  .http_status_code = 413});
  }
  return send_json(request, 200, "{\"ok\":true}");
}

esp_err_t import_handler(httpd_req_t* request) {
  WebDecision decision{};
  if (!authorize(request, decision)) {
    return send_reject(request, decision);
  }
  RequestBudget budget = RequestBudget::for_request(
      request->uri, 0, static_cast<uint32_t>(request->content_len));
  JsonDepthTracker depth;
  if (!receive_body(request, budget, &depth)) {
    const WebReject reason =
        budget.reason == WebReject::body_too_large
            ? WebReject::body_too_large
            : WebReject::json_too_deep;
    return send_reject(
        request, {.reason = reason, .http_status_code = 413});
  }
  return send_json(request, 200, "{\"imported\":true}");
}

esp_err_t websocket_handler(httpd_req_t* request) {
  if (request->method == HTTP_GET) {
    WebDecision decision{};
    if (!authorize(request, decision)) {
      return send_reject(request, decision);
    }
    return ESP_OK;
  }

  httpd_ws_frame_t frame{};
  esp_err_t result = httpd_ws_recv_frame(request, &frame, 0);
  if (result != ESP_OK) {
    return result;
  }
  if (frame.len > kWebSocketFrameLimit) {
    std::array<uint8_t, 2> close_code{0x03, 0xf1};
    httpd_ws_frame_t close_frame{
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_CLOSE,
        .payload = close_code.data(),
        .len = close_code.size(),
    };
    return httpd_ws_send_frame(request, &close_frame);
  }

  static std::array<uint8_t, kWebSocketFrameLimit> frame_buffer{};
  frame.payload = frame_buffer.data();
  return httpd_ws_recv_frame(request, &frame, frame_buffer.size());
}

}  // namespace

WebHandlerContext::WebHandlerContext(std::string expected_host,
                                     RandomSource& random)
    : guard_(std::move(expected_host), random),
      mutex_(xSemaphoreCreateMutexStatic(&mutex_storage_)),
      resolution_signal_(
          xSemaphoreCreateBinaryStatic(&resolution_signal_storage_)),
      worker_stopped_signal_(
          xSemaphoreCreateBinaryStatic(&worker_stopped_signal_storage_)),
      request_queue_(xQueueCreateStatic(
          1, sizeof(httpd_req_t*), request_queue_buffer_.data(),
          &request_queue_storage_)) {
  assert(mutex_ != nullptr);
  assert(resolution_signal_ != nullptr);
  assert(worker_stopped_signal_ != nullptr);
  assert(request_queue_ != nullptr);
  pairing_worker_ = xTaskCreateStatic(
      pairing_worker_entry, "web-pairing", kPairingWorkerStackBytes, this,
      tskIDLE_PRIORITY + 1, pairing_worker_stack_.data(),
      &pairing_worker_storage_);
  assert(pairing_worker_ != nullptr);
}

WebHandlerContext::~WebHandlerContext() {
  if (pairing_worker_ == nullptr) {
    return;
  }
  cancel_pairing_response();
  xSemaphoreGive(resolution_signal_);
  httpd_req_t* stop_request = nullptr;
  xQueueSend(request_queue_, &stop_request, portMAX_DELAY);
  xSemaphoreTake(worker_stopped_signal_, portMAX_DELAY);
  pairing_worker_ = nullptr;
}

void WebHandlerContext::open_pairing_window(
    std::string_view eight_digit_code, uint64_t now_ms) {
  lock();
  if (response_window_.pending()) {
    unlock();
    return;
  }
  resolution_ = {};
  response_window_.finish();
  while (xSemaphoreTake(resolution_signal_, 0) == pdTRUE) {
  }
  guard_.open_pairing_window(eight_digit_code, now_ms);
  unlock();
}

void WebHandlerContext::confirm_pairing(bool accepted, uint64_t now_ms) {
  lock();
  if (!response_window_.pending() || response_window_.expired(now_ms)) {
    guard_.cancel_pairing_confirmation();
    response_window_.finish();
    resolution_ = {};
    unlock();
    return;
  }

  const bool at_capacity = guard_.admin_session_count() >= 5;
  std::optional<AdminCredential> credential =
      guard_.confirm_pairing(accepted, now_ms);
  if (credential.has_value()) {
    resolution_ = {
        .resolution = PairingResolution::accepted,
        .credential = std::move(credential),
    };
  } else {
    resolution_ = {
        .resolution =
            accepted
                ? (at_capacity ? PairingResolution::capacity
                               : PairingResolution::expired)
                : PairingResolution::rejected,
        .credential = std::nullopt,
    };
  }
  unlock();
  xSemaphoreGive(resolution_signal_);
}

PairingResult WebHandlerContext::submit_pairing_code(
    std::string_view code, std::string_view browser_name, uint64_t now_ms) {
  lock();
  if (response_window_.pending()) {
    unlock();
    return PairingResult::window_closed;
  }
  resolution_ = {};
  PairingResult result =
      guard_.submit_pairing_code(code, browser_name, now_ms);
  if (result == PairingResult::awaiting_physical_confirmation) {
    if (!response_window_.begin(now_ms)) {
      guard_.cancel_pairing_confirmation();
      result = PairingResult::window_closed;
    } else {
      while (xSemaphoreTake(resolution_signal_, 0) == pdTRUE) {
      }
    }
  }
  unlock();
  return result;
}

WebDecision WebHandlerContext::authorize(const RequestMeta& request) {
  lock();
  const WebDecision result = guard_.authorize(request);
  unlock();
  return result;
}

std::optional<std::string> WebHandlerContext::issue_csrf_token(
    std::string_view cookie_token, uint64_t now_ms) {
  lock();
  std::optional<std::string> result =
      guard_.issue_csrf_token(cookie_token, now_ms);
  unlock();
  return result;
}

esp_err_t WebHandlerContext::defer_pairing_response(httpd_req_t* request) {
  if (request == nullptr || pairing_worker_ == nullptr) {
    cancel_pairing_response();
    return request == nullptr
               ? ESP_ERR_INVALID_ARG
               : send_json(request, 503,
                           "{\"error\":\"pairing_worker\"}");
  }

  HttpdPairingAsyncBackend backend(request_queue_);
  const PairingDeferResult result = defer_pairing_request(request, backend);
  if (result == PairingDeferResult::deferred) {
    return ESP_OK;
  }

  cancel_pairing_response();
  if (result == PairingDeferResult::begin_failed) {
    return send_json(request, 503, "{\"error\":\"pairing_worker\"}");
  }
  if (result == PairingDeferResult::unavailable_sent) {
    return ESP_OK;
  }
  ESP_LOGW(kTag, "failed to return pairing worker error");
  return ESP_FAIL;
}

bool WebHandlerContext::has_admin_session() {
  lock();
  const bool result = guard_.has_admin_session();
  unlock();
  return result;
}

PairingResolutionResult WebHandlerContext::take_pairing_resolution() {
  lock();
  PairingResolutionResult result = std::move(resolution_);
  resolution_ = {};
  if (result.resolution != PairingResolution::none) {
    response_window_.finish();
  }
  unlock();
  return result;
}

void WebHandlerContext::pairing_worker_entry(void* argument) {
  static_cast<WebHandlerContext*>(argument)->pairing_worker();
}

void WebHandlerContext::pairing_worker() {
  while (true) {
    httpd_req_t* request = nullptr;
    if (xQueueReceive(request_queue_, &request, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    if (request == nullptr) {
      xSemaphoreGive(worker_stopped_signal_);
      vTaskDelete(nullptr);
      return;
    }
    send_pairing_resolution(request, wait_for_pairing_resolution());
    httpd_req_async_handler_complete(request);
  }
}

PairingResolutionResult WebHandlerContext::wait_for_pairing_resolution() {
  while (true) {
    PairingResolutionResult result = take_pairing_resolution();
    if (result.resolution != PairingResolution::none) {
      return result;
    }

    lock();
    const uint64_t current_ms = now_ms();
    const bool pending = response_window_.pending();
    const uint64_t remaining_ms =
        response_window_.remaining_ms(current_ms);
    unlock();
    if (!pending || remaining_ms == 0) {
      cancel_pairing_response();
      return {.resolution = PairingResolution::expired,
              .credential = std::nullopt};
    }

    TickType_t wait_ticks = pdMS_TO_TICKS(remaining_ms);
    if (wait_ticks == 0) {
      wait_ticks = 1;
    }
    xSemaphoreTake(resolution_signal_, wait_ticks);
  }
}

void WebHandlerContext::cancel_pairing_response() {
  lock();
  resolution_ = {};
  response_window_.finish();
  guard_.cancel_pairing_confirmation();
  unlock();
  while (xSemaphoreTake(resolution_signal_, 0) == pdTRUE) {
  }
}

void WebHandlerContext::lock() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
}

void WebHandlerContext::unlock() {
  xSemaphoreGive(mutex_);
}

std::span<const httpd_uri_t> probe_web_handler_routes() {
  static const std::array<esp_err_t (*)(httpd_req_t*), 6> handlers{{
      health_handler,
      pairing_submit_handler,
      session_handler,
      echo_handler,
      import_handler,
      websocket_handler,
  }};
  static const std::array<httpd_uri_t, 6> routes = [] {
    std::array<httpd_uri_t, 6> result{};
    for (size_t index = 0; index < result.size(); ++index) {
      result[index].uri = kProbeWebRoutes[index].path.data();
      result[index].method =
          kProbeWebRoutes[index].method == HttpMethod::get ? HTTP_GET
                                                          : HTTP_POST;
      result[index].handler = handlers[index];
      result[index].is_websocket = kProbeWebRoutes[index].websocket;
      result[index].handle_ws_control_frames =
          kProbeWebRoutes[index].websocket;
    }
    return result;
  }();
  return routes;
}

esp_err_t register_probe_web_handlers(httpd_handle_t server,
                                      WebHandlerContext* context) {
  if (server == nullptr || context == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  for (const httpd_uri_t& route_template : probe_web_handler_routes()) {
    httpd_uri_t route = route_template;
    route.user_ctx = context;
    const esp_err_t result = httpd_register_uri_handler(server, &route);
    if (result != ESP_OK) {
      return result;
    }
  }
  return ESP_OK;
}
