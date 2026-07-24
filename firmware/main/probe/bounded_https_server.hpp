#pragma once

#include <cstdint>
#include <span>

#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/task.h"
#include "probe/pre_tls_limiter.hpp"

class WebHandlerContext;

inline constexpr uint32_t kHttpsServerTaskStackBytes = 8192;

struct BoundedHttpsServerConfig {
  std::span<const uint8_t> server_certificate;
  std::span<const uint8_t> server_private_key;
  WebHandlerContext* web_handlers = nullptr;
  uint16_t port = 443;
  uint32_t handshake_timeout_ms = 10000;
};

esp_err_t start_bounded_https_server(const BoundedHttpsServerConfig& config,
                                     httpd_handle_t* server);

[[nodiscard]] AdmissionSnapshot bounded_https_server_snapshot();
[[nodiscard]] TaskHandle_t bounded_https_server_task();
