#include "product/ble_audio_transport.hpp"

#include <limits>

namespace {

uint32_t saturating_increment(uint32_t value) {
  return value == std::numeric_limits<uint32_t>::max() ? value : value + 1;
}

}  // namespace

BleAudioSendResult BleAudioTransport::map_result(
    BleAudioNotifyResult result, bool count_frame) {
  switch (result) {
    case BleAudioNotifyResult::sent:
      if (count_frame) {
        sent_frames_ = saturating_increment(sent_frames_);
      }
      return BleAudioSendResult::sent;
    case BleAudioNotifyResult::transient_failure:
      transport_drops_ = saturating_increment(transport_drops_);
      return BleAudioSendResult::dropped;
    case BleAudioNotifyResult::fatal_failure:
      transport_drops_ = saturating_increment(transport_drops_);
      return BleAudioSendResult::fatal_failure;
  }
  transport_drops_ = saturating_increment(transport_drops_);
  return BleAudioSendResult::fatal_failure;
}

BleAudioSendResult BleAudioTransport::try_send(
    std::span<const uint8_t> frame) {
  if (!notifier_.sink_ready()) {
    transport_drops_ = saturating_increment(transport_drops_);
    return BleAudioSendResult::not_ready;
  }
  return map_result(notifier_.notify_frame(frame), true);
}

BleAudioSendResult BleAudioTransport::send_status(
    std::span<const uint8_t> status) {
  if (!notifier_.status_ready()) {
    return BleAudioSendResult::not_ready;
  }
  return map_result(notifier_.notify_status(status), false);
}

#ifdef ESP_PLATFORM

#include "esp_err.h"

namespace {

BleAudioNotifyResult notify_result(esp_err_t result) {
  if (result == ESP_OK) {
    return BleAudioNotifyResult::sent;
  }
  if (result == ESP_ERR_NO_MEM || result == ESP_ERR_TIMEOUT) {
    return BleAudioNotifyResult::transient_failure;
  }
  return BleAudioNotifyResult::fatal_failure;
}

class ProductBleAudioNotifier final : public BleAudioNotifier {
 public:
  bool sink_ready() const override {
    return ble_audio_sink_ready();
  }

  bool status_ready() const override {
    return ble_audio_status_ready();
  }

  BleAudioNotifyResult notify_frame(
      std::span<const uint8_t> frame) override {
    return notify_result(notify_audio_frame(frame));
  }

  BleAudioNotifyResult notify_status(
      std::span<const uint8_t> status) override {
    return notify_result(notify_audio_status(status));
  }
};

}  // namespace

std::unique_ptr<IBleAudioTransport> make_product_ble_audio_transport() {
  static ProductBleAudioNotifier notifier;
  return std::make_unique<BleAudioTransport>(notifier);
}

#else

std::unique_ptr<IBleAudioTransport> make_product_ble_audio_transport() {
  return nullptr;
}

#endif
