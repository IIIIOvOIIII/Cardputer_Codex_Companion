#include <array>
#include <cassert>
#include <cstdint>
#include <span>

#include "product/ble_audio_transport.hpp"

namespace {

class FakeAudioNotifier final : public BleAudioNotifier {
 public:
  bool sink_ready() const override { return ready; }
  bool status_ready() const override { return status_subscribed; }

  BleAudioNotifyResult notify_frame(
      std::span<const uint8_t> frame) override {
    ++frame_calls;
    last_size = frame.size();
    return next_result;
  }

  BleAudioNotifyResult notify_status(
      std::span<const uint8_t> status) override {
    ++status_calls;
    last_size = status.size();
    return next_result;
  }

  bool ready = true;
  bool status_subscribed = true;
  int frame_calls = 0;
  int status_calls = 0;
  size_t last_size = 0;
  BleAudioNotifyResult next_result = BleAudioNotifyResult::sent;
};

}  // namespace

int main() {
  FakeAudioNotifier notifier;
  BleAudioTransport transport(notifier);
  std::array<uint8_t, 240> frame{};

  assert(transport.try_send(frame) == BleAudioSendResult::sent);
  assert(notifier.frame_calls == 1);
  assert(notifier.last_size == 240);
  assert(transport.sent_frames() == 1);
  assert(transport.transport_drops() == 0);

  notifier.next_result = BleAudioNotifyResult::transient_failure;
  assert(transport.try_send(frame) == BleAudioSendResult::dropped);
  assert(notifier.frame_calls == 2);
  assert(transport.sent_frames() == 1);
  assert(transport.transport_drops() == 1);

  notifier.next_result = BleAudioNotifyResult::sent;
  assert(transport.try_send(frame) == BleAudioSendResult::sent);
  transport.clear();
  assert(transport.try_send(frame) == BleAudioSendResult::sent);
  assert(notifier.frame_calls == 4);
  transport.clear();

  notifier.ready = false;
  assert(transport.try_send(frame) == BleAudioSendResult::not_ready);
  assert(notifier.frame_calls == 4);
  assert(transport.transport_drops() == 2);

  notifier.ready = true;
  notifier.next_result = BleAudioNotifyResult::fatal_failure;
  assert(transport.try_send(frame) == BleAudioSendResult::fatal_failure);
  assert(notifier.frame_calls == 5);
  assert(transport.transport_drops() == 3);

  FakeAudioNotifier fallback_notifier;
  BleAudioTransport fallback_transport(fallback_notifier);
  std::array<uint8_t, 236> fallback_frame{};
  assert(fallback_transport.try_send(fallback_frame) ==
         BleAudioSendResult::sent);
  assert(fallback_notifier.frame_calls == 1);
  assert(fallback_notifier.last_size == 236);
  assert(fallback_transport.sent_frames() == 1);

  std::array<uint8_t, 12> status{};
  notifier.ready = false;
  notifier.status_subscribed = true;
  notifier.next_result = BleAudioNotifyResult::sent;
  assert(transport.send_status(status) == BleAudioSendResult::sent);
  assert(notifier.status_calls == 1);
  assert(notifier.last_size == 12);
  assert(transport.sent_frames() == 3);

  const BleKeyboardLinkState hid_ready{
      .gap_connected = true,
      .encrypted = true,
      .authenticated = true,
      .bonded = true,
      .hidd_connected = true,
      .input_report_subscribed = true,
  };
  assert(ble_keyboard_ready_from_state(hid_ready));
  assert(!ble_audio_sink_ready_from_state({
      .data_notify = false,
      .status_notify = true,
      .companion_bound = true,
      .encrypted = true,
  }));
  return 0;
}
