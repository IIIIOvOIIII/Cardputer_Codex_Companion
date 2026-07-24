#pragma once

class PairingAsyncBackend {
 public:
  virtual ~PairingAsyncBackend() = default;
  virtual bool begin(void* request, void** async_request) = 0;
  virtual bool enqueue(void* async_request) = 0;
  virtual bool send_unavailable(void* async_request) = 0;
  virtual void complete(void* async_request) = 0;
};

enum class PairingDeferResult {
  deferred,
  begin_failed,
  unavailable_sent,
  unavailable_send_failed,
};

PairingDeferResult defer_pairing_request(void* request,
                                         PairingAsyncBackend& backend);
