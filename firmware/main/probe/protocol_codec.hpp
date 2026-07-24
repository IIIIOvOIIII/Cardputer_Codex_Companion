#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using ByteVector = std::vector<uint8_t>;
using Counter = uint64_t;
using ConnectionId = std::array<uint8_t, 16>;
using GattAuthKey = std::array<uint8_t, 32>;
using AuthTag = std::array<uint8_t, 16>;
using PublicKeyBytes = std::vector<uint8_t>;
using SignatureBytes = std::array<uint8_t, 64>;

struct PairingInput {
  std::string device_id;
  std::string companion_instance_id;
  std::string protocol_version;
  std::string device_identity_sec1_hex;
  std::string companion_identity_sec1_hex;
  std::string device_ephemeral_sec1_hex;
  std::string companion_ephemeral_sec1_hex;
  std::string device_nonce_hex;
  std::string companion_nonce_hex;
  std::string device_ephemeral_private_scalar_hex;

  bool operator==(const PairingInput&) const = default;
};

struct PairingExpected {
  std::array<uint8_t, 32> pairing_root{};
  std::array<uint8_t, 32> gatt_auth_key{};
  std::string sas;
  uint32_t sas_attempt = 0;

  bool operator==(const PairingExpected&) const = default;
};

struct GattMessage {
  ConnectionId operation_id{};
  ByteVector full_message_utf8;
  ByteVector fragment;
  Counter counter = 0;
  uint16_t fragment_index = 0;
  uint16_t fragment_count = 1;
  uint8_t flags = 0;

  bool operator==(const GattMessage&) const = default;
};

struct GattFrame {
  ByteVector authenticated_bytes;
  AuthTag tag{};
  Counter counter = 0;
  enum class Status : uint8_t {
    ok,
    no_connection,
    counter_mismatch,
  } status = Status::ok;

  bool operator==(const GattFrame&) const = default;
};

struct WssAuthInput {
  std::string companion_instance_id;
  std::string device_id;
  std::string protocol_version;
  std::string exporter_hex;
  std::string challenge_hex;

  bool operator==(const WssAuthInput&) const = default;
};

struct RuntimeWssIdentity {
  std::string companion_instance_id;
  std::string device_id;
  std::string protocol_version;
};

ByteVector encode_pairing_transcript(const PairingInput& input);
PairingExpected derive_pairing_values(const PairingInput& input);

class GattSender {
 public:
  explicit GattSender(std::span<const uint8_t, 32> auth_key);

  void begin_connection(std::span<const uint8_t, 16> connection_id);
  GattFrame encode(const GattMessage& message);
  Counter next_counter() const;
  void end_connection();
  bool has_connection() const;

 private:
  GattAuthKey auth_key_{};
  bool has_connection_ = false;
  ConnectionId current_connection_id_{};
  Counter expected_counter_ = 0;
};

WssAuthInput make_runtime_input(
    std::span<const uint8_t> exporter,
    std::span<const uint8_t> challenge,
    const RuntimeWssIdentity& identity);

ByteVector encode_gatt_authenticated_bytes(const ConnectionId& connection_id,
                                         const GattMessage& message);
ByteVector encode_wss_canonical_message(
    std::span<const uint8_t> companion_instance_id,
    std::span<const uint8_t> device_id,
    std::span<const uint8_t> protocol_version,
    std::span<const uint8_t> exporter,
    std::span<const uint8_t> challenge);
