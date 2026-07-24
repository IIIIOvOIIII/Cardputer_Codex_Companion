#include "probe/pinned_wss_transport.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <string_view>
#include <vector>

#ifndef ESP_PLATFORM
#include <openssl/evp.h>
#include <openssl/pem.h>
#endif

namespace {

ByteVector decode_hex(std::string_view hex) {
  if (hex.size() % 2 != 0) {
    return {};
  }

  ByteVector out;
  out.reserve(hex.size() / 2);

  const auto value = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
      return 10 + (c - 'A');
    }
    return -1;
  };

  for (size_t i = 0; i < hex.size(); i += 2) {
    const int hi = value(hex[i]);
    const int lo = value(hex[i + 1]);
    if (hi < 0 || lo < 0) {
      return {};
    }
    out.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }

  return out;
}

bool raw_signature_to_der(std::span<const uint8_t> signature,
                         std::array<uint8_t, 80>& der,
                         size_t& der_len) {
  if (signature.size() != kWssSignatureLength) {
    return false;
  }

  auto encode_integer = [](std::span<const uint8_t> value,
                          std::array<uint8_t, 80>& out,
                          size_t& offset) -> bool {
    const uint8_t* start = value.data();
    size_t length = value.size();

    while (length > 1 && start[0] == 0x00) {
      ++start;
      --length;
    }

    const bool high_bit_set = (start[0] & 0x80u) != 0;
    const size_t integer_len = length + static_cast<size_t>(high_bit_set);

    if (offset + 2 + integer_len > out.size()) {
      return false;
    }

    out[offset++] = 0x02;
    out[offset++] = static_cast<uint8_t>(integer_len);
    if (high_bit_set) {
      out[offset++] = 0x00;
    }
    std::memcpy(out.data() + offset, start, length);
    offset += length;
    return true;
  };

  size_t offset = 0;
  std::array<uint8_t, 80> encoded{};

  if (!encode_integer(signature.subspan(0, kWssSignatureLength / 2), encoded, offset) ||
      !encode_integer(signature.subspan(kWssSignatureLength / 2,
                                       kWssSignatureLength / 2),
                     encoded,
                     offset)) {
    return false;
  }

  if (offset + 2 > der.size() || offset < 4 || offset > 0x7F) {
    return false;
  }

  der.fill(0);
  der[0] = 0x30;
  der[1] = static_cast<uint8_t>(offset);
  std::memcpy(der.data() + 2, encoded.data(), offset);
  der_len = offset + 2;
  return true;
}

}  // namespace

bool spki_pin_matches(std::span<const uint8_t> expected,
                      std::span<const uint8_t> observed) {
  if (expected.size() != kWssPeerSpkiLength || observed.size() != kWssPeerSpkiLength) {
    return false;
  }

  uint8_t mismatch = 0;
  for (size_t i = 0; i < kWssPeerSpkiLength; ++i) {
    mismatch |= static_cast<uint8_t>(expected[i] ^ observed[i]);
  }
  return mismatch == 0;
}

ByteVector encode_wss_auth(const WssAuthInput& input) {
  const auto exporter = decode_hex(input.exporter_hex);
  const auto challenge = decode_hex(input.challenge_hex);
  if (exporter.size() != kWssExporterLength || challenge.size() != kWssExporterLength) {
    return {};
  }

  const std::span<const uint8_t> companion_instance_id(
      reinterpret_cast<const uint8_t*>(input.companion_instance_id.data()),
      input.companion_instance_id.size());
  const std::span<const uint8_t> device_id(
      reinterpret_cast<const uint8_t*>(input.device_id.data()),
      input.device_id.size());
  const std::span<const uint8_t> protocol_version(
      reinterpret_cast<const uint8_t*>(input.protocol_version.data()),
      input.protocol_version.size());
  return encode_wss_canonical_message(companion_instance_id, device_id,
                                      protocol_version, exporter, challenge);
}

bool verify_wss_signature(const WssAuthInput& input,
                         std::span<const uint8_t> signature,
                         std::span<const uint8_t> device_public_key) {
  if (signature.size() != kWssSignatureLength || device_public_key.empty()) {
    return false;
  }

  const ByteVector message = encode_wss_auth(input);
  if (message.empty()) {
    return false;
  }

  std::array<uint8_t, 80> der_signature{};
  size_t der_signature_len = 0;
  if (!raw_signature_to_der(signature, der_signature, der_signature_len)) {
    return false;
  }

#ifndef ESP_PLATFORM
  const uint8_t* key_cursor = device_public_key.data();
  EVP_PKEY* public_key = d2i_PUBKEY(nullptr, &key_cursor, device_public_key.size());
  if (public_key == nullptr) {
    return false;
  }

  EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
  if (md_ctx == nullptr) {
    EVP_PKEY_free(public_key);
    return false;
  }

  const bool valid =
      EVP_DigestVerifyInit(md_ctx, nullptr, EVP_sha256(), nullptr, public_key) == 1 &&
      EVP_DigestVerifyUpdate(md_ctx, message.data(), message.size()) == 1 &&
      EVP_DigestVerifyFinal(md_ctx,
                            der_signature.data(),
                            der_signature_len) == 1;

  EVP_MD_CTX_free(md_ctx);
  EVP_PKEY_free(public_key);
  return valid;
#else
  std::array<uint8_t, 32> hash{};
  mbedtls_sha256(message.data(), message.size(), hash.data(), 0);

  mbedtls_pk_context public_key_ctx;
  mbedtls_pk_init(&public_key_ctx);
  const int parsed =
      mbedtls_pk_parse_public_key(&public_key_ctx,
                                  device_public_key.data(),
                                  device_public_key.size());
  if (parsed != 0) {
    mbedtls_pk_free(&public_key_ctx);
    return false;
  }

  const int verified = mbedtls_pk_verify(&public_key_ctx,
                                         MBEDTLS_MD_SHA256,
                                         hash.data(),
                                         hash.size(),
                                         der_signature.data(),
                                         der_signature_len);
  mbedtls_pk_free(&public_key_ctx);
  return verified == 0;
#endif
}

#ifdef ESP_PLATFORM

#include "esp_transport.h"
#include "esp_transport_ws.h"
#include "esp_tls.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "phase0_protocol_vectors.hpp"
#include "sys/select.h"

namespace {

struct PinnedWssInternalContext {
  PinnedWssTransportContext* public_context = nullptr;
  esp_tls_t* tls = nullptr;
};

void clear_internal_context(PinnedWssInternalContext& context) {
  if (context.tls != nullptr) {
    esp_tls_conn_destroy(context.tls);
    context.tls = nullptr;
  }
  if (context.public_context != nullptr) {
    context.public_context->tls = nullptr;
    context.public_context->has_exporter = false;
    context.public_context->exporter.fill(0);
  }
}

int fail_pinned_wss_connect(PinnedWssInternalContext& context,
                            PinnedWssTransportError error) {
  clear_internal_context(context);
  if (context.public_context != nullptr) {
    context.public_context->last_error = error;
  }
  return -1;
}

int pinned_wss_connect(esp_transport_handle_t transport, const char*, int, int timeout_ms) {
  auto* context = static_cast<PinnedWssInternalContext*>(
      esp_transport_get_context_data(transport));
  if (context == nullptr || context->public_context == nullptr) {
    return -1;
  }

  PinnedWssTransportContext& public_context = *context->public_context;
  if (public_context.companion_host == nullptr ||
      public_context.companion_port <= 0 ||
      public_context.peer_ca_certificate == nullptr ||
      public_context.peer_ca_certificate_length == 0) {
    public_context.last_error = PinnedWssTransportError::missing_context;
    return -1;
  }

  clear_internal_context(*context);
  public_context.last_error = PinnedWssTransportError::ok;

  context->tls = esp_tls_init();
  if (context->tls == nullptr) {
    public_context.last_error = PinnedWssTransportError::missing_context;
    return -1;
  }

  const int host_length =
      static_cast<int>(std::char_traits<char>::length(public_context.companion_host));
  esp_tls_cfg_t tls_config{};
  tls_config.cacert_buf =
      reinterpret_cast<const unsigned char*>(public_context.peer_ca_certificate);
  tls_config.cacert_bytes =
      static_cast<unsigned int>(public_context.peer_ca_certificate_length);
  tls_config.timeout_ms = timeout_ms;
  tls_config.common_name = public_context.companion_host;
  tls_config.skip_common_name = false;

  const int connected =
      esp_tls_conn_new_sync(public_context.companion_host,
                           host_length,
                           public_context.companion_port,
                           &tls_config,
                           context->tls);
  if (connected != 1) {
    return fail_pinned_wss_connect(
        *context, PinnedWssTransportError::missing_ssl_context);
  }

  const mbedtls_ssl_context* ssl = static_cast<mbedtls_ssl_context*>(
      esp_tls_get_ssl_context(context->tls));
  if (ssl == nullptr) {
    return fail_pinned_wss_connect(
        *context, PinnedWssTransportError::missing_ssl_context);
  }

  const mbedtls_x509_crt* peer_cert = mbedtls_ssl_get_peer_cert(ssl);
  if (peer_cert == nullptr) {
    return fail_pinned_wss_connect(
        *context, PinnedWssTransportError::missing_peer_certificate);
  }

  std::array<uint8_t, 512> spki_der{};
  const int der_length = mbedtls_pk_write_pubkey_der(
      &peer_cert->pk, spki_der.data(), spki_der.size());
  if (der_length <= 0) {
    return fail_pinned_wss_connect(
        *context, PinnedWssTransportError::spki_encoding_failed);
  }

  const uint8_t* spki =
      spki_der.data() + spki_der.size() - static_cast<size_t>(der_length);
  std::array<uint8_t, kWssPeerSpkiLength> observed_pin{};
  mbedtls_sha256(spki, static_cast<size_t>(der_length), observed_pin.data(), 0);

  if (!spki_pin_matches(public_context.expected_peer_spki, observed_pin)) {
    return fail_pinned_wss_connect(
        *context, PinnedWssTransportError::spki_mismatch);
  }

  const int exported = mbedtls_ssl_export_keying_material(
      const_cast<mbedtls_ssl_context*>(ssl),
      public_context.exporter.data(),
      public_context.exporter.size(),
      protocol_vectors::wss::exporter_label.data(),
      protocol_vectors::wss::exporter_label.size(),
      nullptr,
      0,
      0);
  if (exported != 0) {
    return fail_pinned_wss_connect(
        *context, PinnedWssTransportError::exporter_failed);
  }

  public_context.tls = context->tls;
  public_context.has_exporter = true;
  return 0;
}

int pinned_wss_read(esp_transport_handle_t transport, char* buffer, int len,
                    int) {
  auto* context = static_cast<PinnedWssInternalContext*>(
      esp_transport_get_context_data(transport));
  if (context == nullptr || context->tls == nullptr) {
    return -1;
  }

  return static_cast<int>(esp_tls_conn_read(context->tls, buffer,
                                           static_cast<size_t>(len)));
}

int pinned_wss_write(esp_transport_handle_t transport, const char* buffer, int len,
                     int) {
  auto* context = static_cast<PinnedWssInternalContext*>(
      esp_transport_get_context_data(transport));
  if (context == nullptr || context->tls == nullptr) {
    return -1;
  }

  return static_cast<int>(
      esp_tls_conn_write(context->tls, buffer, static_cast<size_t>(len)));
}

int pinned_wss_poll_read(esp_transport_handle_t transport, int timeout_ms) {
  auto* context = static_cast<PinnedWssInternalContext*>(
      esp_transport_get_context_data(transport));
  if (context == nullptr || context->tls == nullptr) {
    return -1;
  }

  const int available = static_cast<int>(esp_tls_get_bytes_avail(context->tls));
  if (available != 0) {
    return available > 0 ? 1 : available;
  }

  int socket_fd = -1;
  if (esp_tls_get_conn_sockfd(context->tls, &socket_fd) != ESP_OK) {
    return -1;
  }
  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(socket_fd, &read_fds);
  timeval timeout{};
  timeval* timeout_pointer = nullptr;
  if (timeout_ms >= 0) {
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    timeout_pointer = &timeout;
  }
  return select(socket_fd + 1, &read_fds, nullptr, nullptr, timeout_pointer);
}

int pinned_wss_poll_write(esp_transport_handle_t transport, int timeout_ms) {
  auto* context = static_cast<PinnedWssInternalContext*>(
      esp_transport_get_context_data(transport));
  if (context == nullptr || context->tls == nullptr) {
    return -1;
  }

  int socket_fd = -1;
  if (esp_tls_get_conn_sockfd(context->tls, &socket_fd) != ESP_OK) {
    return -1;
  }
  fd_set write_fds;
  FD_ZERO(&write_fds);
  FD_SET(socket_fd, &write_fds);
  timeval timeout{};
  timeval* timeout_pointer = nullptr;
  if (timeout_ms >= 0) {
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    timeout_pointer = &timeout;
  }
  return select(socket_fd + 1, nullptr, &write_fds, nullptr, timeout_pointer);
}

int pinned_wss_close(esp_transport_handle_t transport) {
  auto* context = static_cast<PinnedWssInternalContext*>(
      esp_transport_get_context_data(transport));
  if (context == nullptr) {
    return 0;
  }

  if (context->tls == nullptr) {
    if (context->public_context != nullptr) {
      context->public_context->has_exporter = false;
      context->public_context->exporter.fill(0);
      context->public_context->tls = nullptr;
    }
    return 0;
  }

  const int rc = esp_tls_conn_destroy(context->tls);
  context->tls = nullptr;
  if (context->public_context != nullptr) {
    context->public_context->tls = nullptr;
    context->public_context->has_exporter = false;
    context->public_context->exporter.fill(0);
  }

  return rc;
}

int pinned_wss_destroy(esp_transport_handle_t transport) {
  auto* context = static_cast<PinnedWssInternalContext*>(
      esp_transport_get_context_data(transport));
  if (context == nullptr) {
    return 0;
  }

  clear_internal_context(*context);
  if (context->public_context != nullptr) {
    context->public_context->base_transport = nullptr;
  }
  delete context;
  return 0;
}

}  // namespace

void clear_pinned_wss_context(PinnedWssTransportContext& context) {
  if (context.base_transport != nullptr) {
    esp_transport_close(context.base_transport);
  }
  context.tls = nullptr;
  context.has_exporter = false;
  context.exporter.fill(0);
}

uint64_t begin_pinned_wss_connection_generation(
    PinnedWssTransportContext& context) {
  ++context.connection_generation;
  clear_pinned_wss_context(context);
  return context.connection_generation;
}

esp_transport_handle_t create_pinned_wss_transport(
    PinnedWssTransportContext& context) {
  if (context.companion_host == nullptr || context.companion_host[0] == '\0' ||
      context.peer_ca_certificate == nullptr ||
      context.peer_ca_certificate_length == 0 ||
      context.companion_port <= 0) {
    context.last_error = PinnedWssTransportError::missing_context;
    return nullptr;
  }

  destroy_pinned_wss_transport(context);
  clear_pinned_wss_context(context);
  context.last_error = PinnedWssTransportError::ok;

  esp_transport_handle_t base = esp_transport_init();
  if (base == nullptr) {
    context.last_error = PinnedWssTransportError::missing_context;
    return nullptr;
  }

  auto* transport_context = new (std::nothrow) PinnedWssInternalContext{};
  if (transport_context == nullptr) {
    esp_transport_destroy(base);
    context.last_error = PinnedWssTransportError::missing_context;
    return nullptr;
  }

  transport_context->public_context = &context;

  if (esp_transport_set_context_data(base, transport_context) != ESP_OK) {
    delete transport_context;
    esp_transport_destroy(base);
    context.last_error = PinnedWssTransportError::missing_context;
    return nullptr;
  }

  const esp_err_t set_funcs =
      esp_transport_set_func(base, pinned_wss_connect, pinned_wss_read,
                            pinned_wss_write, pinned_wss_close,
                            pinned_wss_poll_read, pinned_wss_poll_write,
                            pinned_wss_destroy);
  if (set_funcs != ESP_OK) {
    delete transport_context;
    esp_transport_destroy(base);
    context.last_error = PinnedWssTransportError::missing_context;
    return nullptr;
  }
  context.base_transport = base;

  esp_transport_handle_t websocket = esp_transport_ws_init(base);
  if (websocket == nullptr) {
    esp_transport_destroy(base);
    context.last_error = PinnedWssTransportError::missing_context;
    return nullptr;
  }
  context.websocket_transport = websocket;

  const esp_transport_ws_config_t websocket_config{
      .ws_path = context.websocket_path,
      .sub_protocol = context.websocket_subprotocol,
      .user_agent = "cardputer-phase0",
      .headers = nullptr,
      .auth = nullptr,
      .propagate_control_frames = true,
  };

  if (esp_transport_ws_set_config(websocket, &websocket_config) != ESP_OK) {
    destroy_pinned_wss_transport(context);
    context.last_error = PinnedWssTransportError::missing_context;
    return nullptr;
  }

  if (esp_transport_set_default_port(websocket, context.companion_port) != ESP_OK) {
    destroy_pinned_wss_transport(context);
    context.last_error = PinnedWssTransportError::missing_context;
    return nullptr;
  }

  return websocket;
}

void destroy_pinned_wss_transport(PinnedWssTransportContext& context) {
  esp_transport_handle_t websocket = context.websocket_transport;
  esp_transport_handle_t base = context.base_transport;
  context.websocket_transport = nullptr;
  context.base_transport = nullptr;

  if (websocket != nullptr) {
    esp_transport_destroy(websocket);
  }
  if (base != nullptr) {
    esp_transport_destroy(base);
  }

  context.tls = nullptr;
  context.has_exporter = false;
  context.exporter.fill(0);
}

#endif
