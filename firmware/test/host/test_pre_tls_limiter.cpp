#include <cassert>
#include <cstdint>

#include "probe/pre_tls_limiter.hpp"

namespace {

SourceKey source(uint8_t last) {
  SourceKey value{};
  value.bytes[15] = last;
  return value;
}

void test_four_established_plus_one_pending() {
  PreTlsLimiter limiter;
  constexpr uint64_t now = 1000;

  for (uint8_t index = 1; index <= 4; ++index) {
    const auto lease = limiter.begin(source(index), now);
    assert(lease.allowed());
    limiter.note_tls_alloc_started(lease.token);
    assert(limiter.complete(lease.token, true) == Completion::established);
  }
  assert(limiter.snapshot().established == 4);
  assert(limiter.snapshot().accepted_before_tls == 4);
  assert(limiter.snapshot().tls_alloc_started == 4);

  const auto pending = limiter.begin(source(5), now);
  assert(pending.allowed());
  assert(limiter.snapshot().established == 4);
  assert(limiter.snapshot().pending_handshakes == 1);
  assert(limiter.snapshot().accepted_before_tls == 5);

  const auto second = limiter.begin(source(6), now);
  assert(second.reason == RejectReason::handshake_busy);
  assert(limiter.snapshot().tls_alloc_started == 4);
  assert(limiter.snapshot().rejected_before_tls == 1);

  limiter.note_tls_alloc_started(pending.token + 1);
  assert(limiter.snapshot().tls_alloc_started == 4);
  assert(limiter.complete(pending.token + 1, true) ==
         Completion::invalid_token);
  assert(limiter.snapshot().pending_handshakes == 1);

  limiter.note_tls_alloc_started(pending.token);
  limiter.note_tls_alloc_started(pending.token);
  assert(limiter.snapshot().tls_alloc_started == 5);
  assert(limiter.complete(pending.token, true) ==
         Completion::rejected_established_full);
  assert(limiter.snapshot().established == 4);
  assert(limiter.snapshot().pending_handshakes == 0);
  assert(limiter.snapshot().tls_alloc_started == 5);

  for (uint8_t index = 0; index < 5; ++index) {
    limiter.close_established();
  }
  assert(limiter.snapshot().established == 0);
}

void test_per_source_rate() {
  PreTlsLimiter limiter;
  for (uint64_t now : {0ULL, 1000ULL, 2000ULL}) {
    assert(limiter.begin(source(1), now).allowed());
    limiter.cancel_pending();
  }
  assert(limiter.begin(source(1), 3000).reason == RejectReason::source_rate);
  assert(limiter.snapshot().accepted_before_tls == 3);
  assert(limiter.snapshot().rejected_before_tls == 1);

  assert(limiter.begin(source(1), 60001).allowed());
  limiter.cancel_pending();
}

void test_global_rate() {
  PreTlsLimiter limiter;
  for (uint8_t index = 1; index <= 6; ++index) {
    assert(limiter.begin(source(index), index * 1000).allowed());
    limiter.cancel_pending();
  }
  assert(limiter.begin(source(7), 7000).reason == RejectReason::global_rate);
  assert(limiter.snapshot().rejected_before_tls == 1);
}

void test_source_table_capacity() {
  PreTlsLimiter limiter;
  for (uint8_t index = 1; index <= 16; ++index) {
    assert(limiter.begin(source(index), index * 61000ULL).allowed());
    limiter.cancel_pending();
  }
  assert(limiter.begin(source(17), 16 * 61000ULL).reason ==
         RejectReason::source_table_full);
}

void test_failed_and_cancelled_handshakes_release_pending() {
  PreTlsLimiter limiter;
  const auto failed = limiter.begin(source(1), 0);
  assert(failed.allowed());
  limiter.note_tls_alloc_started(failed.token);
  assert(limiter.complete(failed.token, false) ==
         Completion::handshake_failed);
  assert(limiter.snapshot().pending_handshakes == 0);
  assert(limiter.snapshot().established == 0);
  assert(limiter.snapshot().tls_alloc_started == 1);

  const auto cancelled = limiter.begin(source(2), 1000);
  assert(cancelled.allowed());
  limiter.cancel_pending();
  assert(limiter.snapshot().pending_handshakes == 0);
  assert(limiter.complete(cancelled.token, true) == Completion::invalid_token);
}

}  // namespace

int main() {
  test_four_established_plus_one_pending();
  test_per_source_rate();
  test_global_rate();
  test_source_table_capacity();
  test_failed_and_cancelled_handshakes_release_pending();
  return 0;
}
