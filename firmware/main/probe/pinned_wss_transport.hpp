#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "probe/protocol_codec.hpp"

constexpr size_t kWssPeerSpkiLength = 32;
constexpr size_t kWssExporterLength = 32;
constexpr size_t kWssSignatureLength = 64;

bool spki_pin_matches(std::span<const uint8_t> expected,
                      std::span<const uint8_t> observed);

ByteVector encode_wss_auth(const WssAuthInput& input);

bool verify_wss_signature(
    const WssAuthInput& input,
    std::span<const uint8_t> signature,
    std::span<const uint8_t> device_public_key);

#ifdef ESP_PLATFORM

#include "esp_tls.h"
#include "esp_err.h"
#include "esp_transport.h"
#include "esp_websocket_client.h"

class ProbeController;

enum class PinnedWssTransportError {
  ok = 0,
  missing_context,
  missing_ssl_context,
  missing_peer_certificate,
  spki_encoding_failed,
  spki_mismatch,
  exporter_failed,
};

struct PinnedWssTransportContext {
  const char* companion_host = nullptr;
  int companion_port = 0;
  const char* websocket_path = "/";
  const char* websocket_subprotocol = nullptr;
  const char* companion_uri = nullptr;
  const char* peer_ca_certificate = nullptr;
  size_t peer_ca_certificate_length = 0;
  std::array<uint8_t, kWssPeerSpkiLength> expected_peer_spki{};
  std::array<uint8_t, kWssExporterLength> exporter{};
  esp_tls_t* tls = nullptr;
  esp_transport_handle_t base_transport = nullptr;
  esp_transport_handle_t websocket_transport = nullptr;
  esp_websocket_client_handle_t websocket_client = nullptr;
  ProbeController* probe_controller = nullptr;
  bool has_exporter = false;
  uint64_t connection_generation = 0;
  PinnedWssTransportError last_error = PinnedWssTransportError::ok;
};

void clear_pinned_wss_context(PinnedWssTransportContext& context);
uint64_t begin_pinned_wss_connection_generation(
    PinnedWssTransportContext& context);
esp_transport_handle_t create_pinned_wss_transport(
    PinnedWssTransportContext& context);
void destroy_pinned_wss_transport(PinnedWssTransportContext& context);
esp_websocket_client_handle_t create_pinned_wss_client(
    PinnedWssTransportContext& context);
void destroy_pinned_wss_client(PinnedWssTransportContext& context);
bool accept_pinned_wss_auth_ok(PinnedWssTransportContext& context,
                               uint64_t connection_generation);

#endif
