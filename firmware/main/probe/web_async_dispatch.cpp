#include "probe/web_async_dispatch.hpp"

PairingDeferResult defer_pairing_request(void* request,
                                         PairingAsyncBackend& backend) {
  void* async_request = nullptr;
  if (!backend.begin(request, &async_request)) {
    return PairingDeferResult::begin_failed;
  }
  if (backend.enqueue(async_request)) {
    return PairingDeferResult::deferred;
  }

  const bool response_sent = backend.send_unavailable(async_request);
  backend.complete(async_request);
  return response_sent ? PairingDeferResult::unavailable_sent
                       : PairingDeferResult::unavailable_send_failed;
}
