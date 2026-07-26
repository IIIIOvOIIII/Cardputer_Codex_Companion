#include "product/audio_protocol.hpp"

#include <algorithm>

namespace {

size_t payload_size(AudioSampleRate rate) {
  switch (rate) {
    case AudioSampleRate::hz24000:
      return kAudioPayloadBytes24k;
    case AudioSampleRate::hz16000:
      return kAudioPayloadBytes16k;
  }
  return 0;
}

bool valid_rate(AudioSampleRate rate) {
  return payload_size(rate) != 0;
}

AudioProtocolError validate_header(const AudioFrameHeader& header,
                                   size_t actual_payload_length) {
  if (header.version != kAudioProtocolVersion) {
    return AudioProtocolError::unsupported_version;
  }
  if ((header.flags & ~kAudioFlagMask) != 0) {
    return AudioProtocolError::invalid_flags;
  }
  if (!valid_rate(header.rate)) {
    return AudioProtocolError::invalid_rate;
  }
  if (header.duration_ms != kAudioFrameDurationMs) {
    return AudioProtocolError::invalid_duration;
  }
  const size_t expected = payload_size(header.rate);
  if (actual_payload_length != expected ||
      (header.payload_length != 0 && header.payload_length != expected)) {
    return AudioProtocolError::payload_length;
  }
  return AudioProtocolError::none;
}

bool valid_control_opcode(uint8_t value) {
  return value >= static_cast<uint8_t>(AudioControlOpcode::hello) &&
         value <= static_cast<uint8_t>(AudioControlOpcode::reset_statistics);
}

}  // namespace

AudioProtocolError encode_audio_packet(
    AudioFrameHeader header, std::span<const uint8_t> payload,
    std::span<uint8_t> output, size_t* written) {
  if (written == nullptr) {
    return AudioProtocolError::null_output;
  }
  *written = 0;
  const AudioProtocolError validation =
      validate_header(header, payload.size());
  if (validation != AudioProtocolError::none) {
    return validation;
  }
  const size_t required = kAudioFrameHeaderBytes + payload.size();
  if (output.size() < required) {
    return AudioProtocolError::output_too_small;
  }

  header.payload_length = static_cast<uint16_t>(payload.size());
  output[0] = header.version;
  output[1] = header.flags;
  output[2] = static_cast<uint8_t>(header.sequence & 0xFFU);
  output[3] = static_cast<uint8_t>(header.sequence >> 8U);
  output[4] = static_cast<uint8_t>(header.rate);
  output[5] = header.duration_ms;
  output[6] = static_cast<uint8_t>(header.payload_length & 0xFFU);
  output[7] = static_cast<uint8_t>(header.payload_length >> 8U);
  std::copy(payload.begin(), payload.end(), output.begin() + 8);
  *written = required;
  return AudioProtocolError::none;
}

AudioProtocolError decode_audio_packet(std::span<const uint8_t> packet,
                                       AudioFrameView* frame) {
  if (frame == nullptr) {
    return AudioProtocolError::null_output;
  }
  if (packet.size() < kAudioFrameHeaderBytes) {
    return AudioProtocolError::packet_too_short;
  }
  AudioFrameHeader header{
      .version = packet[0],
      .flags = packet[1],
      .sequence = static_cast<uint16_t>(
          static_cast<uint16_t>(packet[2]) |
          static_cast<uint16_t>(static_cast<uint16_t>(packet[3]) << 8U)),
      .rate = static_cast<AudioSampleRate>(packet[4]),
      .duration_ms = packet[5],
      .payload_length = static_cast<uint16_t>(
          static_cast<uint16_t>(packet[6]) |
          static_cast<uint16_t>(static_cast<uint16_t>(packet[7]) << 8U)),
  };
  const size_t actual_payload_length = packet.size() - kAudioFrameHeaderBytes;
  const AudioProtocolError validation =
      validate_header(header, actual_payload_length);
  if (validation != AudioProtocolError::none) {
    return validation;
  }
  if (header.payload_length != actual_payload_length) {
    return AudioProtocolError::payload_length;
  }
  frame->header = header;
  frame->payload = packet.subspan(kAudioFrameHeaderBytes);
  return AudioProtocolError::none;
}

AudioProtocolError encode_audio_control(
    AudioControlMessage message, std::span<uint8_t> output, size_t* written) {
  if (written == nullptr) {
    return AudioProtocolError::null_output;
  }
  *written = 0;
  const uint8_t opcode = static_cast<uint8_t>(message.opcode);
  if (!valid_control_opcode(opcode)) {
    return AudioProtocolError::unknown_control_opcode;
  }
  const bool has_rate =
      message.opcode == AudioControlOpcode::set_preferred_rate;
  const size_t required = has_rate ? 3 : 2;
  if (output.size() < required) {
    return AudioProtocolError::output_too_small;
  }
  if (has_rate && !valid_rate(message.preferred_rate)) {
    return AudioProtocolError::invalid_rate;
  }
  output[0] = kAudioProtocolVersion;
  output[1] = opcode;
  if (has_rate) {
    output[2] = static_cast<uint8_t>(message.preferred_rate);
  }
  *written = required;
  return AudioProtocolError::none;
}

AudioProtocolError decode_audio_control(std::span<const uint8_t> input,
                                        AudioControlMessage* message) {
  if (message == nullptr) {
    return AudioProtocolError::null_output;
  }
  if (input.size() < 2) {
    return AudioProtocolError::invalid_control_length;
  }
  if (input[0] != kAudioProtocolVersion) {
    return AudioProtocolError::unsupported_version;
  }
  if (!valid_control_opcode(input[1])) {
    return AudioProtocolError::unknown_control_opcode;
  }
  const AudioControlOpcode opcode =
      static_cast<AudioControlOpcode>(input[1]);
  const bool has_rate = opcode == AudioControlOpcode::set_preferred_rate;
  if (input.size() != (has_rate ? 3U : 2U)) {
    return AudioProtocolError::invalid_control_length;
  }
  AudioSampleRate rate = AudioSampleRate::hz24000;
  if (has_rate) {
    rate = static_cast<AudioSampleRate>(input[2]);
    if (!valid_rate(rate)) {
      return AudioProtocolError::invalid_rate;
    }
  }
  message->opcode = opcode;
  message->preferred_rate = rate;
  return AudioProtocolError::none;
}
