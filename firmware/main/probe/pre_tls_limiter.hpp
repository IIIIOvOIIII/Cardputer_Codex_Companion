#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct SourceKey {
  std::array<uint8_t, 16> bytes{};
  bool operator==(const SourceKey&) const = default;
};

enum class RejectReason {
  none,
  handshake_busy,
  source_rate,
  global_rate,
  source_table_full,
  invalid_token,
};

enum class Completion {
  established,
  handshake_failed,
  rejected_established_full,
  invalid_token,
};

struct Admission {
  RejectReason reason = RejectReason::none;
  uint32_t token = 0;

  [[nodiscard]] bool allowed() const {
    return reason == RejectReason::none;
  }
};

struct AdmissionSnapshot {
  uint8_t established = 0;
  uint8_t pending_handshakes = 0;
  uint32_t accepted_before_tls = 0;
  uint32_t tls_alloc_started = 0;
  uint32_t rejected_before_tls = 0;
};

class PreTlsLimiter {
 public:
  Admission begin(const SourceKey& source, uint64_t now_ms);
  void note_tls_alloc_started(uint32_t token);
  Completion complete(uint32_t token, bool handshake_ok);
  void cancel_pending();
  void close_established();
  [[nodiscard]] AdmissionSnapshot snapshot() const;

 private:
  static constexpr uint64_t kRateWindowMs = 60000;
  static constexpr uint8_t kMaxEstablished = 4;
  static constexpr size_t kSourceCapacity = 16;
  static constexpr size_t kPerSourceAttempts = 3;
  static constexpr size_t kGlobalAttempts = 6;

  struct SourceRecord {
    SourceKey source{};
    std::array<uint64_t, kPerSourceAttempts> accepted_at_ms{};
    uint8_t accepted_count = 0;
    bool occupied = false;
  };

  void prune_source(SourceRecord& record, uint64_t now_ms);
  void prune_global(uint64_t now_ms);
  uint32_t next_token();
  Admission reject(RejectReason reason);

  std::array<SourceRecord, kSourceCapacity> sources_{};
  std::array<uint64_t, kGlobalAttempts> global_accepted_at_ms_{};
  uint8_t global_accepted_count_ = 0;
  uint8_t established_ = 0;
  bool pending_ = false;
  bool pending_tls_alloc_started_ = false;
  uint32_t pending_token_ = 0;
  uint32_t token_generation_ = 0;
  uint32_t accepted_before_tls_ = 0;
  uint32_t tls_alloc_started_ = 0;
  uint32_t rejected_before_tls_ = 0;
};
