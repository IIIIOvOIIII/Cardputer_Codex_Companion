#include "CardputerAudioDevice.h"

#include <assert.h>
#include <string.h>

int main(void) {
  CardputerAudioDevice device;
  cardputer_audio_device_initialize(&device);
  assert(cardputer_audio_device_input_stream_count(&device) == 1);
  assert(cardputer_audio_device_output_stream_count(&device) == 0);
  assert(cardputer_audio_device_sample_rate(&device) == 48000.0);
  assert(cardputer_audio_device_channel_count(&device) == 1);
  assert(kCardputerAudioObjectDevice == 2);
  assert(kCardputerAudioObjectInputStream == 3);

  assert(cardputer_audio_device_client_count(&device) == 0);
  cardputer_audio_device_start(&device);
  cardputer_audio_device_start(&device);
  assert(cardputer_audio_device_client_count(&device) == 2);
  cardputer_audio_device_stop(&device);
  cardputer_audio_device_stop(&device);
  cardputer_audio_device_stop(&device);
  assert(cardputer_audio_device_client_count(&device) == 0);

  float output[8];
  memset(output, 0x7f, sizeof(output));
  assert(cardputer_audio_device_render(&device, output, 8) == 0);
  for (size_t index = 0; index < 8; ++index) {
    assert(output[index] == 0.0f);
  }

  CardputerAudioRing ring;
  cardputer_audio_ring_initialize(&ring);
  cardputer_audio_device_set_ring(&device, &ring);
  const float input[] = {0.25f, -0.5f, 0.75f};
  assert(cardputer_audio_ring_write(&ring, input, 3) == 3);
  memset(output, 0x7f, sizeof(output));
  assert(cardputer_audio_device_render(&device, output, 8) == 3);
  assert(memcmp(input, output, sizeof(input)) == 0);
  for (size_t index = 3; index < 8; ++index) {
    assert(output[index] == 0.0f);
  }

  cardputer_audio_device_set_ring(&device, NULL);
  memset(output, 0x7f, sizeof(output));
  assert(cardputer_audio_device_render(&device, output, 8) == 0);
  for (size_t index = 0; index < 8; ++index) {
    assert(output[index] == 0.0f);
  }
  return 0;
}
