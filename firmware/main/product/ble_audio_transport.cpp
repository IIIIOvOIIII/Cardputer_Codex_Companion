#include "product/ble_audio_transport.hpp"

#include <algorithm>
#include <limits>

namespace {

uint32_t saturating_increment(uint32_t value) {
  return value == std::numeric_limits<uint32_t>::max() ? value : value + 1;
}

uint32_t saturating_add(uint32_t value, uint8_t increment) {
  while (increment-- > 0) {
    value = saturating_increment(value);
  }
  return value;
}

uint8_t batch_frame_count(size_t frame_size) {
  if (frame_size == kAudioPacketBytes24k ||
      frame_size == kAudioPacketBytes16k) {
    return 1;
  }
  return 0;
}

}  // namespace

BleAudioSendResult BleAudioTransport::map_result(
    BleAudioNotifyResult result, uint8_t frame_count) {
  switch (result) {
    case BleAudioNotifyResult::sent:
      sent_frames_ = saturating_add(sent_frames_, frame_count);
      return BleAudioSendResult::sent;
    case BleAudioNotifyResult::transient_failure:
      transport_drops_ =
          saturating_add(transport_drops_, frame_count);
      return BleAudioSendResult::dropped;
    case BleAudioNotifyResult::fatal_failure:
      transport_drops_ =
          saturating_add(transport_drops_, frame_count);
      return BleAudioSendResult::fatal_failure;
  }
  transport_drops_ = saturating_add(transport_drops_, frame_count);
  return BleAudioSendResult::fatal_failure;
}

BleAudioSendResult BleAudioTransport::try_send(
    std::span<const uint8_t> frame) {
  if (!notifier_.sink_ready()) {
    transport_drops_ = saturating_add(
        transport_drops_,
        static_cast<uint8_t>(pending_frame_count_ + 1));
    pending_size_ = 0;
    pending_frame_size_ = 0;
    pending_frame_count_ = 0;
    return BleAudioSendResult::not_ready;
  }
  const uint8_t target_frame_count = batch_frame_count(frame.size());
  if (target_frame_count == 0 ||
      pending_size_ + frame.size() > pending_.size()) {
    transport_drops_ = saturating_add(
        transport_drops_,
        static_cast<uint8_t>(pending_frame_count_ + 1));
    pending_size_ = 0;
    pending_frame_size_ = 0;
    pending_frame_count_ = 0;
    return BleAudioSendResult::fatal_failure;
  }
  if (pending_frame_count_ != 0 &&
      pending_frame_size_ != frame.size()) {
    transport_drops_ = saturating_add(
        transport_drops_, pending_frame_count_);
    pending_size_ = 0;
    pending_frame_size_ = 0;
    pending_frame_count_ = 0;
  }
  std::copy(frame.begin(), frame.end(), pending_.begin() + pending_size_);
  pending_size_ += frame.size();
  pending_frame_size_ = frame.size();
  ++pending_frame_count_;
  if (pending_frame_count_ < target_frame_count) {
    return BleAudioSendResult::sent;
  }
  const size_t batch_size = pending_size_;
  const uint8_t batch_count = pending_frame_count_;
  pending_size_ = 0;
  pending_frame_size_ = 0;
  pending_frame_count_ = 0;
  return map_result(
      notifier_.notify_frame(
          std::span<const uint8_t>(pending_.data(), batch_size)),
      batch_count);
}

BleAudioSendResult BleAudioTransport::send_status(
    std::span<const uint8_t> status) {
  if (!notifier_.status_ready()) {
    return BleAudioSendResult::not_ready;
  }
  return map_result(notifier_.notify_status(status), 0);
}

void BleAudioTransport::clear() {
  pending_size_ = 0;
  pending_frame_size_ = 0;
  pending_frame_count_ = 0;
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
