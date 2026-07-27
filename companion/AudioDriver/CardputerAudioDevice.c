#include "CardputerAudioDevice.h"

#include <string.h>

void cardputer_audio_device_initialize(CardputerAudioDevice *device) {
  if (device == NULL) {
    return;
  }
  atomic_init(&device->ring, NULL);
  atomic_init(&device->client_count, 0);
}

void cardputer_audio_device_set_ring(
    CardputerAudioDevice *device,
    CardputerAudioRing *ring) {
  if (device == NULL) {
    return;
  }
  atomic_store_explicit(&device->ring, ring, memory_order_release);
}

bool cardputer_audio_device_replace_ring_if_idle(
    CardputerAudioDevice *device,
    CardputerAudioRing *ring) {
  if (device == NULL ||
      atomic_load_explicit(
          &device->client_count, memory_order_acquire) != 0) {
    return false;
  }
  cardputer_audio_device_set_ring(device, ring);
  return true;
}

uint32_t cardputer_audio_device_input_stream_count(
    const CardputerAudioDevice *device) {
  return device == NULL ? 0 : 1;
}

uint32_t cardputer_audio_device_output_stream_count(
    const CardputerAudioDevice *device) {
  (void)device;
  return 0;
}

double cardputer_audio_device_sample_rate(
    const CardputerAudioDevice *device) {
  return device == NULL ? 0.0 : 48000.0;
}

uint32_t cardputer_audio_device_channel_count(
    const CardputerAudioDevice *device) {
  return device == NULL ? 0 : 1;
}

void cardputer_audio_device_start(CardputerAudioDevice *device) {
  if (device == NULL) {
    return;
  }
  uint32_t count =
      atomic_load_explicit(&device->client_count, memory_order_relaxed);
  while (count != UINT32_MAX &&
         !atomic_compare_exchange_weak_explicit(
             &device->client_count,
             &count,
             count + 1,
             memory_order_release,
             memory_order_relaxed)) {
  }
}

void cardputer_audio_device_stop(CardputerAudioDevice *device) {
  if (device == NULL) {
    return;
  }
  uint32_t count =
      atomic_load_explicit(&device->client_count, memory_order_relaxed);
  while (count > 0 &&
         !atomic_compare_exchange_weak_explicit(
             &device->client_count,
             &count,
             count - 1,
             memory_order_release,
             memory_order_relaxed)) {
  }
}

uint32_t cardputer_audio_device_client_count(
    const CardputerAudioDevice *device) {
  return device == NULL
             ? 0
             : atomic_load_explicit(
                   &device->client_count, memory_order_acquire);
}

uint32_t cardputer_audio_device_render(
    CardputerAudioDevice *device,
    float *output,
    uint32_t frame_count) {
  if (output == NULL || frame_count == 0) {
    return 0;
  }
  CardputerAudioRing *ring =
      device == NULL
          ? NULL
          : atomic_load_explicit(&device->ring, memory_order_acquire);
  if (ring == NULL || !cardputer_audio_ring_is_valid(ring)) {
    memset(output, 0, (size_t)frame_count * sizeof(float));
    return 0;
  }
  return cardputer_audio_ring_read_or_silence(ring, output, frame_count);
}
