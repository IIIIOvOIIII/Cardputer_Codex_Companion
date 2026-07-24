#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "probe/web_guard.hpp"

enum class PairingResolution {
  none,
  accepted,
  rejected,
  capacity,
  expired,
};

struct PairingResolutionResult {
  PairingResolution resolution = PairingResolution::none;
  std::optional<AdminCredential> credential;
};

class WebHandlerContext final : public WebPairingPhysicalSink {
 public:
  // Owns a permanent worker and must live for the HTTPS server's lifetime.
  WebHandlerContext(std::string expected_host, RandomSource& random);
  ~WebHandlerContext();
  WebHandlerContext(const WebHandlerContext&) = delete;
  WebHandlerContext& operator=(const WebHandlerContext&) = delete;

  void open_pairing_window(std::string_view eight_digit_code,
                           uint64_t now_ms) override;
  void confirm_pairing(bool accepted, uint64_t now_ms) override;

  PairingResult submit_pairing_code(std::string_view code,
                                    std::string_view browser_name,
                                    uint64_t now_ms);
  WebDecision authorize(const RequestMeta& request);
  std::optional<std::string> issue_csrf_token(
      std::string_view cookie_token, uint64_t now_ms);
  esp_err_t defer_pairing_response(httpd_req_t* request);
  [[nodiscard]] bool has_admin_session();
  [[nodiscard]] PairingResolutionResult take_pairing_resolution();

 private:
  static void pairing_worker_entry(void* argument);
  void pairing_worker();
  PairingResolutionResult wait_for_pairing_resolution();
  void cancel_pairing_response();
  void lock();
  void unlock();

  static constexpr uint32_t kPairingWorkerStackBytes = 8192;

  WebGuard guard_;
  PairingResponseWindow response_window_;
  StaticSemaphore_t mutex_storage_{};
  SemaphoreHandle_t mutex_ = nullptr;
  StaticSemaphore_t resolution_signal_storage_{};
  SemaphoreHandle_t resolution_signal_ = nullptr;
  StaticSemaphore_t worker_stopped_signal_storage_{};
  SemaphoreHandle_t worker_stopped_signal_ = nullptr;
  StaticQueue_t request_queue_storage_{};
  std::array<uint8_t, sizeof(httpd_req_t*)> request_queue_buffer_{};
  QueueHandle_t request_queue_ = nullptr;
  StaticTask_t pairing_worker_storage_{};
  std::array<StackType_t, kPairingWorkerStackBytes> pairing_worker_stack_{};
  TaskHandle_t pairing_worker_ = nullptr;
  PairingResolutionResult resolution_{};
};

std::span<const httpd_uri_t> probe_web_handler_routes();
esp_err_t register_probe_web_handlers(httpd_handle_t server,
                                      WebHandlerContext* context);
