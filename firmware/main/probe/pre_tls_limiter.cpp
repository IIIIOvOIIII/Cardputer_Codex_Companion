#include "probe/pre_tls_limiter.hpp"

#include <algorithm>
#include <cstddef>

namespace {

template <size_t Capacity>
uint8_t prune_timestamps(std::array<uint64_t, Capacity>& timestamps,
                         uint8_t count, uint64_t now_ms,
                         uint64_t window_ms) {
  uint8_t retained = 0;
  for (uint8_t index = 0; index < count; ++index) {
    const uint64_t timestamp = timestamps[index];
    const bool expired =
        now_ms >= timestamp && now_ms - timestamp > window_ms;
    if (!expired) {
      timestamps[retained++] = timestamp;
    }
  }
  std::fill(timestamps.begin() + retained, timestamps.end(), 0);
  return retained;
}

}  // namespace

Admission PreTlsLimiter::begin(const SourceKey& source, uint64_t now_ms) {
  SourceRecord* record = nullptr;
  SourceRecord* free_record = nullptr;
  for (auto& candidate : sources_) {
    if (candidate.occupied && candidate.source == source) {
      record = &candidate;
      break;
    }
    if (!candidate.occupied && free_record == nullptr) {
      free_record = &candidate;
    }
  }

  if (record == nullptr && free_record == nullptr) {
    return reject(RejectReason::source_table_full);
  }

  prune_global(now_ms);
  if (record != nullptr) {
    prune_source(*record, now_ms);
    if (record->accepted_count >= kPerSourceAttempts) {
      return reject(RejectReason::source_rate);
    }
  }
  if (global_accepted_count_ >= kGlobalAttempts) {
    return reject(RejectReason::global_rate);
  }
  if (pending_) {
    return reject(RejectReason::handshake_busy);
  }

  if (record == nullptr) {
    record = free_record;
    record->source = source;
    record->occupied = true;
  }
  record->accepted_at_ms[record->accepted_count++] = now_ms;
  global_accepted_at_ms_[global_accepted_count_++] = now_ms;
  ++accepted_before_tls_;

  pending_ = true;
  pending_tls_alloc_started_ = false;
  pending_token_ = next_token();
  return {.reason = RejectReason::none, .token = pending_token_};
}

void PreTlsLimiter::note_tls_alloc_started(uint32_t token) {
  if (!pending_ || token != pending_token_ || pending_tls_alloc_started_) {
    return;
  }
  pending_tls_alloc_started_ = true;
  ++tls_alloc_started_;
}

Completion PreTlsLimiter::complete(uint32_t token, bool handshake_ok) {
  if (!pending_ || token != pending_token_) {
    return Completion::invalid_token;
  }

  pending_ = false;
  pending_tls_alloc_started_ = false;
  pending_token_ = 0;

  if (!handshake_ok) {
    return Completion::handshake_failed;
  }
  if (established_ >= kMaxEstablished) {
    return Completion::rejected_established_full;
  }

  ++established_;
  return Completion::established;
}

void PreTlsLimiter::cancel_pending() {
  pending_ = false;
  pending_tls_alloc_started_ = false;
  pending_token_ = 0;
}

void PreTlsLimiter::close_established() {
  if (established_ > 0) {
    --established_;
  }
}

AdmissionSnapshot PreTlsLimiter::snapshot() const {
  return {
      .established = established_,
      .pending_handshakes = static_cast<uint8_t>(pending_ ? 1 : 0),
      .accepted_before_tls = accepted_before_tls_,
      .tls_alloc_started = tls_alloc_started_,
      .rejected_before_tls = rejected_before_tls_,
  };
}

void PreTlsLimiter::prune_source(SourceRecord& record, uint64_t now_ms) {
  record.accepted_count = prune_timestamps(
      record.accepted_at_ms, record.accepted_count, now_ms, kRateWindowMs);
}

void PreTlsLimiter::prune_global(uint64_t now_ms) {
  global_accepted_count_ =
      prune_timestamps(global_accepted_at_ms_, global_accepted_count_, now_ms,
                       kRateWindowMs);
}

uint32_t PreTlsLimiter::next_token() {
  ++token_generation_;
  if (token_generation_ == 0) {
    ++token_generation_;
  }
  return token_generation_;
}

Admission PreTlsLimiter::reject(RejectReason reason) {
  ++rejected_before_tls_;
  return {.reason = reason, .token = 0};
}
