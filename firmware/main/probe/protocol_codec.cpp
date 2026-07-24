#include "probe/protocol_codec.hpp"

#include "phase0_protocol_vectors.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#ifndef ESP_PLATFORM
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#else
#include "esp_random.h"
#include "mbedtls/bignum.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/ecp.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#endif
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr size_t kSha256DigestLength = 32;

ByteVector hmac_sha256(std::span<const uint8_t> key,
                      std::span<const uint8_t> message);

ByteVector from_hex(std::string_view hex) {
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

void append_lp16(ByteVector& output, std::string_view value) {
  output.push_back(static_cast<uint8_t>((value.size() >> 8) & 0xFF));
  output.push_back(static_cast<uint8_t>(value.size() & 0xFF));
  output.insert(output.end(), value.begin(), value.end());
}

void append_lp16(ByteVector& output, std::span<const uint8_t> value) {
  output.push_back(static_cast<uint8_t>((value.size() >> 8) & 0xFF));
  output.push_back(static_cast<uint8_t>(value.size() & 0xFF));
  output.insert(output.end(), value.begin(), value.end());
}

std::array<uint8_t, 32> sha256_bytes(std::span<const uint8_t> value) {
  std::array<uint8_t, 32> digest{};
#ifndef ESP_PLATFORM
  SHA256(value.data(), value.size(), digest.data());
#else
  if (mbedtls_sha256(value.data(), value.size(), digest.data(), 0) != 0) {
    throw std::runtime_error("sha256 failed");
  }
#endif
  return digest;
}

template <size_t N>
std::array<uint8_t, N> to_fixed(std::span<const uint8_t> bytes) {
  if (bytes.size() != N) {
    throw std::runtime_error("crypto output length mismatch");
  }

  std::array<uint8_t, N> out{};
  std::copy_n(bytes.begin(), N, out.begin());
  return out;
}

ByteVector hkdf_sha256(std::span<const uint8_t> ikm,
                      std::span<const uint8_t> salt,
                      std::span<const uint8_t> info,
                      size_t length) {
  if (length == 0) {
    return {};
  }

  ByteVector zero_key(kSha256DigestLength);
  ByteVector prk = hmac_sha256(salt.empty() ? std::span<const uint8_t>(zero_key)
                                            : salt,
                               ikm);
  ByteVector out;
  out.reserve(length);
  ByteVector prev;
  uint8_t counter = 1;

  while (out.size() < length) {
    ByteVector block_input;
    block_input.insert(block_input.end(), prev.begin(), prev.end());
    block_input.insert(block_input.end(), info.begin(), info.end());
    block_input.push_back(counter);

    const auto digest = hmac_sha256(prk, block_input);
    const size_t need = std::min(digest.size(), length - out.size());
    out.insert(out.end(), digest.begin(), digest.begin() + static_cast<long>(need));
    prev.assign(digest.begin(), digest.end());
    ++counter;

    if (counter == 0) {
      throw std::runtime_error("hkdf output too long");
    }
  }

  return out;
}

ByteVector hmac_sha256(std::span<const uint8_t> key,
                      std::span<const uint8_t> message) {
  ByteVector digest(kSha256DigestLength);
#ifndef ESP_PLATFORM
  unsigned int digest_len = 0;
  HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
       message.data(), message.size(), digest.data(), &digest_len);
  if (digest_len != kSha256DigestLength) {
    throw std::runtime_error("hmac digest length mismatch");
  }
  digest.resize(digest_len);
#else
  const mbedtls_md_info_t* md =
      mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (md == nullptr ||
      mbedtls_md_hmac(md,
                      key.data(),
                      key.size(),
                      message.data(),
                      message.size(),
                      digest.data()) != 0) {
    throw std::runtime_error("hmac failed");
  }
#endif
  return digest;
}

ByteVector decode_sec1_public(std::string_view sec1_hex) {
  const ByteVector bytes = from_hex(sec1_hex);
  if (bytes.size() != protocol_vectors::pairing::sec1_bytes ||
      bytes.front() != 0x04) {
    throw std::runtime_error("unexpected sec1 public key");
  }
  return bytes;
}

ByteVector decode_private_scalar(std::string_view hex, size_t expected_length) {
  const ByteVector bytes = from_hex(hex);
  if (bytes.size() != expected_length) {
    throw std::runtime_error("unexpected private scalar length");
  }
  return bytes;
}

ByteVector ecdh_secret(std::string_view private_scalar_hex,
                       std::string_view peer_public_hex) {
  const ByteVector private_bytes = decode_private_scalar(
      private_scalar_hex,
      protocol_vectors::pairing::private_scalar_bytes);
  const ByteVector peer_bytes = decode_sec1_public(peer_public_hex);

#ifndef ESP_PLATFORM
  EC_KEY* local_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  if (!local_key) {
    throw std::runtime_error("EC_KEY_new_by_curve_name failed");
  }

  BIGNUM* private_bn = BN_bin2bn(private_bytes.data(),
                                 static_cast<int>(private_bytes.size()),
                                 nullptr);
  if (!private_bn) {
    EC_KEY_free(local_key);
    throw std::runtime_error("BN_bin2bn failed");
  }

  if (!EC_KEY_set_private_key(local_key, private_bn)) {
    BN_clear_free(private_bn);
    EC_KEY_free(local_key);
    throw std::runtime_error("EC_KEY_set_private_key failed");
  }

  const EC_GROUP* group = EC_KEY_get0_group(local_key);
  EC_POINT* public_point = EC_POINT_new(group);
  if (!public_point) {
    BN_clear_free(private_bn);
    EC_KEY_free(local_key);
    throw std::runtime_error("EC_POINT_new failed");
  }

  if (!EC_POINT_mul(group, public_point, private_bn, nullptr, nullptr, nullptr)) {
    EC_POINT_free(public_point);
    BN_clear_free(private_bn);
    EC_KEY_free(local_key);
    throw std::runtime_error("EC_POINT_mul failed");
  }

  EC_KEY_set_public_key(local_key, public_point);

  const uint8_t* peer_ptr = peer_bytes.data();
  EC_POINT* peer_point = EC_POINT_new(group);
  if (!peer_point) {
    EC_POINT_free(public_point);
    BN_clear_free(private_bn);
    EC_KEY_free(local_key);
    throw std::runtime_error("EC_POINT_new failed");
  }

  if (EC_POINT_oct2point(group, peer_point, peer_ptr, peer_bytes.size(), nullptr) != 1) {
    EC_POINT_free(peer_point);
    EC_POINT_free(public_point);
    BN_clear_free(private_bn);
    EC_KEY_free(local_key);
    throw std::runtime_error("invalid peer SEC1");
  }

  const size_t secret_len = (EC_GROUP_get_degree(group) + 7) / 8;
  ByteVector shared(secret_len);
  const int computed_len = ECDH_compute_key(shared.data(),
                                           shared.size(),
                                           peer_point,
                                           local_key,
                                           nullptr);
  if (computed_len <= 0) {
    EC_POINT_free(peer_point);
    EC_POINT_free(public_point);
    BN_clear_free(private_bn);
    EC_KEY_free(local_key);
    throw std::runtime_error("ECDH_compute_key failed");
  }

  shared.resize(static_cast<size_t>(computed_len));

  EC_POINT_free(peer_point);
  EC_POINT_free(public_point);
  BN_clear_free(private_bn);
  EC_KEY_free(local_key);

  return shared;
#else
  mbedtls_ecp_group group;
  mbedtls_mpi private_scalar;
  mbedtls_mpi shared_secret;
  mbedtls_ecp_point peer_point;
  mbedtls_ecp_group_init(&group);
  mbedtls_mpi_init(&private_scalar);
  mbedtls_mpi_init(&shared_secret);
  mbedtls_ecp_point_init(&peer_point);

  auto cleanup = [&]() {
    mbedtls_ecp_point_free(&peer_point);
    mbedtls_mpi_free(&shared_secret);
    mbedtls_mpi_free(&private_scalar);
    mbedtls_ecp_group_free(&group);
  };

  int rc = mbedtls_ecp_group_load(
      &group, MBEDTLS_ECP_DP_SECP256R1);
  if (rc == 0) {
    rc = mbedtls_mpi_read_binary(
        &private_scalar, private_bytes.data(), private_bytes.size());
  }
  if (rc == 0) {
    rc = mbedtls_ecp_point_read_binary(
        &group, &peer_point, peer_bytes.data(), peer_bytes.size());
  }
  if (rc == 0) {
    rc = mbedtls_ecp_check_pubkey(&group, &peer_point);
  }

  const auto rng = [](void*, unsigned char* output, size_t length) -> int {
    esp_fill_random(output, length);
    return 0;
  };
  if (rc == 0) {
    rc = mbedtls_ecdh_compute_shared(
        &group,
        &shared_secret,
        &peer_point,
        &private_scalar,
        rng,
        nullptr);
  }

  ByteVector shared(kSha256DigestLength);
  if (rc == 0) {
    rc = mbedtls_mpi_write_binary(
        &shared_secret, shared.data(), shared.size());
  }
  cleanup();
  if (rc != 0) {
    throw std::runtime_error("mbedtls ECDH failed");
  }
  return shared;
#endif
}

std::string to_hex(std::span<const uint8_t> bytes) {
  std::ostringstream hex;
  hex << std::hex << std::setfill('0');
  for (uint8_t value : bytes) {
    hex << std::setw(2) << static_cast<int>(value);
  }
  return hex.str();
}

std::string to_attempt_sas(std::span<const uint8_t> sample) {
  if (sample.size() != 4) {
    throw std::runtime_error("SAS sample must be 4 bytes");
  }

  const uint32_t word =
      (static_cast<uint32_t>(sample[0]) << 24) |
      (static_cast<uint32_t>(sample[1]) << 16) |
      (static_cast<uint32_t>(sample[2]) << 8) |
      static_cast<uint32_t>(sample[3]);

  const uint32_t token = word % 1'000'000;
  std::ostringstream out;
  out << std::setfill('0') << std::setw(6) << token;
  return out.str();
}

ByteVector make_label_attempt_info(std::string_view label, uint32_t attempt) {
  ByteVector info(label.begin(), label.end());
  info.push_back(static_cast<uint8_t>((attempt >> 24) & 0xFF));
  info.push_back(static_cast<uint8_t>((attempt >> 16) & 0xFF));
  info.push_back(static_cast<uint8_t>((attempt >> 8) & 0xFF));
  info.push_back(static_cast<uint8_t>(attempt & 0xFF));
  return info;
}
}  // namespace

ByteVector encode_pairing_transcript(const PairingInput& input) {
  ByteVector transcript;
  append_lp16(transcript, input.device_id);
  append_lp16(transcript, input.companion_instance_id);
  append_lp16(transcript, input.protocol_version);

  auto append_prefixed_sec1 = [](ByteVector& out, std::string_view field) {
    auto bytes = from_hex(field);
    if (bytes.size() != protocol_vectors::pairing::sec1_bytes) {
      throw std::runtime_error("public SEC1 key size mismatch");
    }
    out.insert(out.end(), bytes.begin(), bytes.end());
  };

  append_prefixed_sec1(transcript, input.device_identity_sec1_hex);
  append_prefixed_sec1(transcript, input.companion_identity_sec1_hex);
  append_prefixed_sec1(transcript, input.device_ephemeral_sec1_hex);
  append_prefixed_sec1(transcript, input.companion_ephemeral_sec1_hex);

  const ByteVector device_nonce = from_hex(input.device_nonce_hex);
  const ByteVector companion_nonce = from_hex(input.companion_nonce_hex);
  if (device_nonce.size() != protocol_vectors::pairing::nonce_bytes ||
      companion_nonce.size() != protocol_vectors::pairing::nonce_bytes) {
    throw std::runtime_error("pairing nonce size mismatch");
  }
  transcript.insert(transcript.end(), device_nonce.begin(), device_nonce.end());
  transcript.insert(transcript.end(), companion_nonce.begin(), companion_nonce.end());

  ByteVector framed;
  framed.insert(
      framed.end(),
      protocol_vectors::pairing::magic.begin(),
      protocol_vectors::pairing::magic.end());
  framed.insert(
      framed.end(),
      protocol_vectors::pairing::version_bytes.begin(),
      protocol_vectors::pairing::version_bytes.end());
  framed.insert(framed.end(), transcript.begin(), transcript.end());
  return framed;
}

PairingExpected derive_pairing_values(const PairingInput& input) {
  const ByteVector transcript = encode_pairing_transcript(input);
  const auto transcript_sha256 = sha256_bytes(transcript);

  const ByteVector shared_secret = ecdh_secret(
      input.device_ephemeral_private_scalar_hex,
      input.companion_ephemeral_sec1_hex
  );

  PairingExpected expected;
  expected.pairing_root = to_fixed<32>(hkdf_sha256(
      shared_secret,
      transcript_sha256,
      std::span<const uint8_t>(
          reinterpret_cast<const uint8_t*>(
              protocol_vectors::pairing::hkdf_label_root.data()),
          protocol_vectors::pairing::hkdf_label_root.size()),
      32));
  expected.gatt_auth_key = to_fixed<32>(hkdf_sha256(
      shared_secret,
      transcript_sha256,
      std::span<const uint8_t>(
          reinterpret_cast<const uint8_t*>(
              protocol_vectors::pairing::hkdf_label_gatt_auth.data()),
          protocol_vectors::pairing::hkdf_label_gatt_auth.size()),
      32));

  uint32_t attempt = 0;
  for (; attempt <= protocol_vectors::pairing::sas_max_attempt; ++attempt) {
    const ByteVector info = make_label_attempt_info(
        protocol_vectors::pairing::hkdf_label_sas,
        attempt);

    const auto sample = hkdf_sha256(shared_secret,
                                   transcript_sha256,
                                   info,
                                   4);
    if (sample.size() != 4) {
      continue;
    }

    const uint32_t word =
        (static_cast<uint32_t>(sample[0]) << 24) |
        (static_cast<uint32_t>(sample[1]) << 16) |
        (static_cast<uint32_t>(sample[2]) << 8) |
        static_cast<uint32_t>(sample[3]);

    if (word < protocol_vectors::pairing::sas_rejection_limit) {
      expected.sas = to_attempt_sas(sample);
      expected.sas_attempt = attempt;
      return expected;
    }
  }

  throw std::runtime_error("SAS attempts exhausted");
}

ByteVector encode_gatt_authenticated_bytes(const ConnectionId& connection_id,
                                         const GattMessage& message) {
  ByteVector bytes;
  bytes.push_back(protocol_vectors::gatt::frame_version);
  bytes.push_back(message.flags);
  bytes.insert(bytes.end(), connection_id.begin(), connection_id.end());
  bytes.insert(bytes.end(), message.operation_id.begin(), message.operation_id.end());

  for (int i = 7; i >= 0; --i) {
    bytes.push_back(static_cast<uint8_t>((message.counter >> (i * 8)) & 0xFF));
  }

  bytes.push_back(static_cast<uint8_t>((message.fragment_index >> 8) & 0xFF));
  bytes.push_back(static_cast<uint8_t>(message.fragment_index & 0xFF));
  bytes.push_back(static_cast<uint8_t>((message.fragment_count >> 8) & 0xFF));
  bytes.push_back(static_cast<uint8_t>(message.fragment_count & 0xFF));

  const auto message_len = static_cast<uint32_t>(message.full_message_utf8.size());
  bytes.push_back(static_cast<uint8_t>((message_len >> 24) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((message_len >> 16) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((message_len >> 8) & 0xFF));
  bytes.push_back(static_cast<uint8_t>(message_len & 0xFF));

  const auto message_hash = sha256_bytes(message.full_message_utf8);
  bytes.insert(bytes.end(), message_hash.begin(), message_hash.end());

  const auto fragment_len = static_cast<uint16_t>(message.fragment.size());
  bytes.push_back(static_cast<uint8_t>((fragment_len >> 8) & 0xFF));
  bytes.push_back(static_cast<uint8_t>(fragment_len & 0xFF));
  bytes.insert(bytes.end(), message.fragment.begin(), message.fragment.end());

  return bytes;
}

GattSender::GattSender(std::span<const uint8_t, 32> auth_key) {
  std::copy_n(auth_key.begin(), 32, auth_key_.begin());
}

void GattSender::begin_connection(std::span<const uint8_t, 16> connection_id) {
  std::copy_n(connection_id.begin(), 16, current_connection_id_.begin());
  has_connection_ = true;
  expected_counter_ = 0;
}

GattFrame GattSender::encode(const GattMessage& message) {
  GattFrame frame;
  frame.status = GattFrame::Status::counter_mismatch;
  frame.counter = message.counter;

  if (!has_connection_) {
    frame.status = GattFrame::Status::no_connection;
    return frame;
  }

  if (message.counter != expected_counter_) {
    return frame;
  }

  frame.status = GattFrame::Status::ok;
  frame.authenticated_bytes = encode_gatt_authenticated_bytes(current_connection_id_, message);

  ByteVector hmac_input;
  hmac_input.insert(
      hmac_input.end(),
      protocol_vectors::gatt::hmac_label.begin(),
      protocol_vectors::gatt::hmac_label.end());
  hmac_input.insert(hmac_input.end(), frame.authenticated_bytes.begin(), frame.authenticated_bytes.end());

  auto full_tag = hmac_sha256(auth_key_, hmac_input);
  if (full_tag.size() < frame.tag.size()) {
    throw std::runtime_error("hmac tag too short");
  }

  std::copy_n(full_tag.begin(), frame.tag.size(), frame.tag.begin());

  ++expected_counter_;
  return frame;
}

Counter GattSender::next_counter() const {
  return expected_counter_;
}

void GattSender::end_connection() {
  has_connection_ = false;
  current_connection_id_.fill(0);
  expected_counter_ = 0;
}

bool GattSender::has_connection() const {
  return has_connection_;
}

ByteVector encode_wss_canonical_message(
    std::span<const uint8_t> companion_instance_id,
    std::span<const uint8_t> device_id,
    std::span<const uint8_t> protocol_version,
    std::span<const uint8_t> exporter,
    std::span<const uint8_t> challenge) {
  ByteVector message;
  append_lp16(message, exporter);
  append_lp16(message, companion_instance_id);
  append_lp16(message, device_id);
  append_lp16(message, protocol_version);
  append_lp16(message, challenge);
  return message;
}

WssAuthInput make_runtime_input(
    std::span<const uint8_t> exporter,
    std::span<const uint8_t> challenge,
    const RuntimeWssIdentity& identity) {
  WssAuthInput input;
  input.companion_instance_id = identity.companion_instance_id;
  input.device_id = identity.device_id;
  input.protocol_version = identity.protocol_version;
  input.exporter_hex = to_hex(exporter);
  input.challenge_hex = to_hex(challenge);
  return input;
}
