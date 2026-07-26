#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "probe/ble_services.hpp"

enum class BleAudioNotifyResult : uint8_t {
  sent,
  transient_failure,
  fatal_failure,
};

enum class BleAudioSendResult : uint8_t {
  sent,
  not_ready,
  dropped,
  fatal_failure,
};

class BleAudioNotifier {
 public:
  virtual ~BleAudioNotifier() = default;
  [[nodiscard]] virtual bool sink_ready() const = 0;
  [[nodiscard]] virtual bool status_ready() const = 0;
  virtual BleAudioNotifyResult notify_frame(
      std::span<const uint8_t> frame) = 0;
  virtual BleAudioNotifyResult notify_status(
      std::span<const uint8_t> status) = 0;
};

class IBleAudioTransport {
 public:
  virtual ~IBleAudioTransport() = default;
  [[nodiscard]] virtual bool sink_ready() const = 0;
  virtual BleAudioSendResult try_send(
      std::span<const uint8_t> frame) = 0;
  virtual BleAudioSendResult send_status(
      std::span<const uint8_t> status) = 0;
  virtual void clear() = 0;
  [[nodiscard]] virtual uint32_t transport_drops() const = 0;
};

class BleAudioTransport final : public IBleAudioTransport {
 public:
  explicit BleAudioTransport(BleAudioNotifier& notifier)
      : notifier_(notifier) {}

  [[nodiscard]] bool sink_ready() const override {
    return notifier_.sink_ready();
  }
  BleAudioSendResult try_send(
      std::span<const uint8_t> frame) override;
  BleAudioSendResult send_status(
      std::span<const uint8_t> status) override;
  void clear() override {}
  [[nodiscard]] uint32_t transport_drops() const override {
    return transport_drops_;
  }
  [[nodiscard]] uint32_t sent_frames() const { return sent_frames_; }

 private:
  BleAudioSendResult map_result(BleAudioNotifyResult result,
                                bool count_frame);

  BleAudioNotifier& notifier_;
  uint32_t sent_frames_ = 0;
  uint32_t transport_drops_ = 0;
};

std::unique_ptr<IBleAudioTransport> make_product_ble_audio_transport();
