#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "probe/web_guard.hpp"

enum class PairingResolution {
  none,
  accepted,
  rejected,
  capacity_or_expired,
};

struct PairingResolutionResult {
  PairingResolution resolution = PairingResolution::none;
  std::optional<AdminCredential> credential;
};

class WebHandlerContext final : public WebPairingPhysicalSink {
 public:
  WebHandlerContext(std::string expected_host, RandomSource& random);

  void open_pairing_window(std::string_view eight_digit_code,
                           uint64_t now_ms) override;
  void confirm_pairing(bool accepted, uint64_t now_ms) override;

  PairingResult submit_pairing_code(std::string_view code,
                                    std::string_view browser_name,
                                    uint64_t now_ms);
  WebDecision authorize(const RequestMeta& request);
  std::optional<std::string> issue_csrf_token(
      std::string_view cookie_token, uint64_t now_ms);
  [[nodiscard]] bool has_admin_session();
  [[nodiscard]] PairingResolutionResult take_pairing_resolution();

 private:
  void lock();
  void unlock();

  WebGuard guard_;
  StaticSemaphore_t mutex_storage_{};
  SemaphoreHandle_t mutex_ = nullptr;
  PairingResolutionResult resolution_{};
};

std::span<const httpd_uri_t> probe_web_handler_routes();
esp_err_t register_probe_web_handlers(httpd_handle_t server,
                                      WebHandlerContext* context);
