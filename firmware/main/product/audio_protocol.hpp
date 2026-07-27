#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

inline constexpr char kAudioDataCharacteristicUuid[] =
    "7A100005-2C4D-4F20-9F20-434F44455831";
inline constexpr char kAudioControlCharacteristicUuid[] =
    "7A100006-2C4D-4F20-9F20-434F44455831";
inline constexpr char kAudioStatusCharacteristicUuid[] =
    "7A100007-2C4D-4F20-9F20-434F44455831";

inline constexpr uint8_t kAudioProtocolVersion = 1;

enum class AudioSampleRate : uint8_t {
  hz24000 = 1,
  hz16000 = 2,
};

constexpr uint8_t audio_frame_duration_ms(AudioSampleRate rate) {
  switch (rate) {
    case AudioSampleRate::hz24000:
      return 19;
    case AudioSampleRate::hz16000:
      return 28;
  }
  return 0;
}

constexpr size_t audio_frame_samples(AudioSampleRate rate) {
  switch (rate) {
    case AudioSampleRate::hz24000:
      return 456;
    case AudioSampleRate::hz16000:
      return 448;
  }
  return 0;
}

inline constexpr size_t kAudioFrameHeaderBytes = 8;
inline constexpr size_t kAudioPayloadBytes24k = 232;
inline constexpr size_t kAudioPayloadBytes16k = 228;
inline constexpr size_t kAudioPacketBytes24k =
    kAudioFrameHeaderBytes + kAudioPayloadBytes24k;
inline constexpr size_t kAudioPacketBytes16k =
    kAudioFrameHeaderBytes + kAudioPayloadBytes16k;

inline constexpr uint8_t kAudioFlagStart = 1U << 0U;
inline constexpr uint8_t kAudioFlagDiscontinuity = 1U << 1U;
inline constexpr uint8_t kAudioFlagDegradedRate = 1U << 2U;
inline constexpr uint8_t kAudioFlagMask =
    kAudioFlagStart | kAudioFlagDiscontinuity | kAudioFlagDegradedRate;

struct AudioFrameHeader {
  uint8_t version = kAudioProtocolVersion;
  uint8_t flags = 0;
  uint16_t sequence = 0;
  AudioSampleRate rate = AudioSampleRate::hz24000;
  uint8_t duration_ms = audio_frame_duration_ms(rate);
  uint16_t payload_length = 0;
};

struct AudioFrameView {
  AudioFrameHeader header{};
  std::span<const uint8_t> payload{};
};

enum class AudioControlOpcode : uint8_t {
  hello = 1,
  sink_ready = 2,
  sink_not_ready = 3,
  set_preferred_rate = 4,
  reset_statistics = 5,
};

struct AudioControlMessage {
  AudioControlOpcode opcode = AudioControlOpcode::hello;
  AudioSampleRate preferred_rate = AudioSampleRate::hz24000;
};

enum class AudioProtocolError : uint8_t {
  none,
  null_output,
  output_too_small,
  packet_too_short,
  unsupported_version,
  invalid_flags,
  invalid_rate,
  invalid_duration,
  payload_length,
  unknown_control_opcode,
  invalid_control_length,
};

AudioProtocolError encode_audio_packet(
    AudioFrameHeader header, std::span<const uint8_t> payload,
    std::span<uint8_t> output, size_t* written);

AudioProtocolError decode_audio_packet(std::span<const uint8_t> packet,
                                       AudioFrameView* frame);

AudioProtocolError encode_audio_control(
    AudioControlMessage message, std::span<uint8_t> output, size_t* written);

AudioProtocolError decode_audio_control(std::span<const uint8_t> input,
                                        AudioControlMessage* message);
