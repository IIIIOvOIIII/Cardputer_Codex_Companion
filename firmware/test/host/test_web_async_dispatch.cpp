#include <array>
#include <cassert>
#include <cstdint>
#include <span>

#include "probe/web_async_dispatch.hpp"

namespace {

enum class Call : uint8_t {
  begin,
  enqueue,
  send_unavailable,
  complete,
};

class FakeBackend final : public PairingAsyncBackend {
 public:
  bool begin(void* request, void** async_request) override {
    calls[call_count++] = Call::begin;
    assert(request != nullptr);
    if (!begin_succeeds) {
      return false;
    }
    *async_request = &async_token;
    return true;
  }

  bool enqueue(void* async_request) override {
    calls[call_count++] = Call::enqueue;
    assert(async_request == &async_token);
    return enqueue_succeeds;
  }

  bool send_unavailable(void* async_request) override {
    calls[call_count++] = Call::send_unavailable;
    assert(async_request == &async_token);
    return send_succeeds;
  }

  void complete(void* async_request) override {
    calls[call_count++] = Call::complete;
    assert(async_request == &async_token);
  }

  std::span<const Call> recorded_calls() const {
    return std::span<const Call>(calls.data(), call_count);
  }

  bool begin_succeeds = true;
  bool enqueue_succeeds = true;
  bool send_succeeds = true;

 private:
  uint8_t async_token = 0;
  std::array<Call, 4> calls{};
  size_t call_count = 0;
};

void expect_calls(const FakeBackend& backend,
                  std::span<const Call> expected) {
  assert(backend.recorded_calls().size() == expected.size());
  for (size_t index = 0; index < expected.size(); ++index) {
    assert(backend.recorded_calls()[index] == expected[index]);
  }
}

}  // namespace

int main() {
  uint8_t request = 0;

  FakeBackend begin_failure;
  begin_failure.begin_succeeds = false;
  assert(defer_pairing_request(&request, begin_failure) ==
         PairingDeferResult::begin_failed);
  constexpr std::array begin_failure_calls{Call::begin};
  expect_calls(begin_failure, begin_failure_calls);

  FakeBackend deferred;
  assert(defer_pairing_request(&request, deferred) ==
         PairingDeferResult::deferred);
  constexpr std::array deferred_calls{Call::begin, Call::enqueue};
  expect_calls(deferred, deferred_calls);

  FakeBackend queue_failure;
  queue_failure.enqueue_succeeds = false;
  assert(defer_pairing_request(&request, queue_failure) ==
         PairingDeferResult::unavailable_sent);
  constexpr std::array queue_failure_calls{
      Call::begin,
      Call::enqueue,
      Call::send_unavailable,
      Call::complete,
  };
  expect_calls(queue_failure, queue_failure_calls);

  FakeBackend send_failure;
  send_failure.enqueue_succeeds = false;
  send_failure.send_succeeds = false;
  assert(defer_pairing_request(&request, send_failure) ==
         PairingDeferResult::unavailable_send_failed);
  expect_calls(send_failure, queue_failure_calls);

  return 0;
}
