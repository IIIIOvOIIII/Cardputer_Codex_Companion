#include "probe/bounded_https_server.hpp"

#include <climits>
#include <cstdint>
#include <cstring>

#include "esp_timer.h"
#include "esp_tls.h"
#include "lwip/sockets.h"
#include "probe/web_handlers.hpp"
#include "unistd.h"

namespace {

struct BoundedServerState {
  PreTlsLimiter limiter;
  esp_tls_cfg_server_t tls_config{};
  httpd_handle_t server = nullptr;
  bool started = false;
};

BoundedServerState state;

BoundedServerState* server_state(httpd_handle_t server) {
  return static_cast<BoundedServerState*>(
      httpd_get_global_transport_ctx(server));
}

bool normalize_source(int sockfd, SourceKey& source) {
  sockaddr_storage peer{};
  socklen_t peer_length = sizeof(peer);
  if (getpeername(sockfd, reinterpret_cast<sockaddr*>(&peer), &peer_length) !=
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

int tls_recv(httpd_handle_t server, int sockfd, char* buffer,
             size_t buffer_length, int flags) {
  static_cast<void>(flags);
  auto* tls =
      static_cast<esp_tls_t*>(httpd_sess_get_transport_ctx(server, sockfd));
  if (tls == nullptr) {
    return HTTPD_SOCK_ERR_INVALID;
  }
  return static_cast<int>(esp_tls_conn_read(tls, buffer, buffer_length));
}

int tls_send(httpd_handle_t server, int sockfd, const char* buffer,
             size_t buffer_length, int flags) {
  static_cast<void>(flags);
  auto* tls =
      static_cast<esp_tls_t*>(httpd_sess_get_transport_ctx(server, sockfd));
  if (tls == nullptr) {
    return HTTPD_SOCK_ERR_INVALID;
  }
  return static_cast<int>(esp_tls_conn_write(tls, buffer, buffer_length));
}

int tls_pending(httpd_handle_t server, int sockfd) {
  auto* tls =
      static_cast<esp_tls_t*>(httpd_sess_get_transport_ctx(server, sockfd));
  if (tls == nullptr) {
    return HTTPD_SOCK_ERR_INVALID;
  }
  return static_cast<int>(esp_tls_get_bytes_avail(tls));
}

void free_tls_transport(void* context) {
  if (context != nullptr) {
    esp_tls_server_session_delete(static_cast<esp_tls_t*>(context));
  }
}

esp_err_t bounded_open(httpd_handle_t server, int sockfd) {
  BoundedServerState* current = server_state(server);
  if (current == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  SourceKey source{};
  if (!normalize_source(sockfd, source)) {
    return ESP_FAIL;
  }

  const uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1000;
  const Admission lease = current->limiter.begin(source, now_ms);
  if (!lease.allowed()) {
    return ESP_FAIL;
  }

  esp_tls_t* tls = esp_tls_init();
  if (tls == nullptr) {
    current->limiter.complete(lease.token, false);
    return ESP_ERR_NO_MEM;
  }
  current->limiter.note_tls_alloc_started(lease.token);

  const int handshake =
      esp_tls_server_session_create(&current->tls_config, sockfd, tls);
  const Completion completion =
      current->limiter.complete(lease.token, handshake == 0);
  if (completion != Completion::established) {
    esp_tls_server_session_delete(tls);
    return ESP_FAIL;
  }

  httpd_sess_set_transport_ctx(server, sockfd, tls, free_tls_transport);
  esp_err_t result = httpd_sess_set_recv_override(server, sockfd, tls_recv);
  if (result == ESP_OK) {
    result = httpd_sess_set_send_override(server, sockfd, tls_send);
  }
  if (result == ESP_OK) {
    result = httpd_sess_set_pending_override(server, sockfd, tls_pending);
  }
  return result;
}

void bounded_close(httpd_handle_t server, int sockfd) {
  BoundedServerState* current = server_state(server);
  void* transport = httpd_sess_get_transport_ctx(server, sockfd);
  if (transport != nullptr) {
    if (current != nullptr) {
      current->limiter.close_established();
    }
    httpd_sess_set_transport_ctx(server, sockfd, nullptr, nullptr);
  }
  close(sockfd);
}

void retain_static_context(void*) {}

}  // namespace

esp_err_t start_bounded_https_server(const BoundedHttpsServerConfig& config,
                                     httpd_handle_t* server) {
  if (server == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  *server = nullptr;
  if (state.started) {
    return ESP_ERR_INVALID_STATE;
  }
  if (config.server_certificate.empty() || config.server_private_key.empty() ||
      config.web_handlers == nullptr ||
      config.server_certificate.size() > UINT_MAX ||
      config.server_private_key.size() > UINT_MAX) {
    return ESP_ERR_INVALID_ARG;
  }

  state.limiter = PreTlsLimiter{};
  state.tls_config = {};
  state.tls_config.servercert_buf = config.server_certificate.data();
  state.tls_config.servercert_bytes =
      static_cast<unsigned int>(config.server_certificate.size());
  state.tls_config.serverkey_buf = config.server_private_key.data();
  state.tls_config.serverkey_bytes =
      static_cast<unsigned int>(config.server_private_key.size());
  state.tls_config.tls_handshake_timeout_ms = config.handshake_timeout_ms;

  httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
  http_config.server_port = config.port;
  http_config.max_open_sockets = 5;
  http_config.lru_purge_enable = false;
  http_config.open_fn = bounded_open;
  http_config.close_fn = bounded_close;
  http_config.global_transport_ctx = &state;
  http_config.global_transport_ctx_free_fn = retain_static_context;

  const esp_err_t result = httpd_start(&state.server, &http_config);
  if (result != ESP_OK) {
    state.server = nullptr;
    return result;
  }
  const esp_err_t registration =
      register_probe_web_handlers(state.server, config.web_handlers);
  if (registration != ESP_OK) {
    httpd_stop(state.server);
    state.server = nullptr;
    return registration;
  }

  state.started = true;
  *server = state.server;
  return ESP_OK;
}

AdmissionSnapshot bounded_https_server_snapshot() {
  return state.limiter.snapshot();
}
