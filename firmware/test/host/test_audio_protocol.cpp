#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <string_view>

#include "product/audio_protocol.hpp"

int main() {
  static_assert(std::string_view{kAudioDataCharacteristicUuid}.starts_with(
      "7A100005-"));
  static_assert(std::string_view{kAudioControlCharacteristicUuid}.starts_with(
      "7A100006-"));
  static_assert(std::string_view{kAudioStatusCharacteristicUuid}.starts_with(
      "7A100007-"));

  std::array<uint8_t, kAudioPacketBytes24k> packet{};
  std::array<uint8_t, kAudioPayloadBytes24k> payload{};
  for (size_t index = 0; index < payload.size(); ++index) {
    payload[index] = static_cast<uint8_t>(index);
  }

  size_t written = 0;
  assert(encode_audio_packet(
             {.version = kAudioProtocolVersion,
              .flags = kAudioFlagStart,
              .sequence = 0x1234,
              .rate = AudioSampleRate::hz24000,
              .duration_ms = kAudioFrameDurationMs},
             payload, packet, &written) == AudioProtocolError::none);
  assert(written == kAudioPacketBytes24k);
  assert(packet[0] == 1);
  assert(packet[1] == kAudioFlagStart);
  assert(packet[2] == 0x34);
  assert(packet[3] == 0x12);
  assert(packet[4] == static_cast<uint8_t>(AudioSampleRate::hz24000));
  assert(packet[5] == 10);
  assert(packet[6] == 124);
  assert(packet[7] == 0);
  assert(packet[8] == 0);
  assert(packet[131] == 123);

  AudioFrameView decoded{};
  assert(decode_audio_packet(packet, &decoded) == AudioProtocolError::none);
  assert(decoded.header.sequence == 0x1234);
  assert(decoded.header.payload_length == kAudioPayloadBytes24k);
  assert(decoded.payload.size() == kAudioPayloadBytes24k);
  assert(decoded.payload[123] == 123);

  std::array<uint8_t, kAudioPayloadBytes16k> payload16{};
  std::array<uint8_t, kAudioPacketBytes16k> packet16{};
  assert(encode_audio_packet(
             {.sequence = 0xFFFF,
              .rate = AudioSampleRate::hz16000,
              .duration_ms = kAudioFrameDurationMs},
             payload16, packet16, &written) == AudioProtocolError::none);
  assert(written == kAudioPacketBytes16k);
  assert(packet16[2] == 0xFF && packet16[3] == 0xFF);

  auto malformed = packet16;
  malformed[6] = 85;
  assert(decode_audio_packet(malformed, &decoded) ==
         AudioProtocolError::payload_length);

  auto bad_version = packet16;
  bad_version[0] = 2;
  assert(decode_audio_packet(bad_version, &decoded) ==
         AudioProtocolError::unsupported_version);

  auto bad_flags = packet16;
  bad_flags[1] = 0x80;
  assert(decode_audio_packet(bad_flags, &decoded) ==
         AudioProtocolError::invalid_flags);

  auto bad_rate = packet16;
  bad_rate[4] = 3;
  assert(decode_audio_packet(bad_rate, &decoded) ==
         AudioProtocolError::invalid_rate);

  auto bad_duration = packet16;
  bad_duration[5] = 20;
  assert(decode_audio_packet(bad_duration, &decoded) ==
         AudioProtocolError::invalid_duration);

  std::array<uint8_t, kAudioPacketBytes16k - 1> short_output{};
  assert(encode_audio_packet(
             {.rate = AudioSampleRate::hz16000,
              .duration_ms = kAudioFrameDurationMs},
             payload16, short_output, &written) ==
         AudioProtocolError::output_too_small);

  std::array<uint8_t, 3> control{};
  assert(encode_audio_control(
             {.opcode = AudioControlOpcode::set_preferred_rate,
              .preferred_rate = AudioSampleRate::hz16000},
             control, &written) == AudioProtocolError::none);
  assert(written == 3);
  assert((control == std::array<uint8_t, 3>{1, 4, 2}));

  AudioControlMessage control_message{};
  assert(decode_audio_control(
             std::span<const uint8_t>{control.data(), written},
             &control_message) == AudioProtocolError::none);
  assert(control_message.opcode ==
         AudioControlOpcode::set_preferred_rate);
  assert(control_message.preferred_rate == AudioSampleRate::hz16000);

  const std::array<uint8_t, 2> remote_start{1, 6};
  assert(decode_audio_control(remote_start, &control_message) ==
         AudioProtocolError::unknown_control_opcode);

  const std::array<uint8_t, 2> truncated_set_rate{1, 4};
  assert(decode_audio_control(truncated_set_rate, &control_message) ==
         AudioProtocolError::invalid_control_length);
  return 0;
}
