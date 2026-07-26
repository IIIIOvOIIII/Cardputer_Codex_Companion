#ifndef CARDPUTER_AUDIO_DEVICE_H
#define CARDPUTER_AUDIO_DEVICE_H

#include "CardputerAudioRing.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum {
  kCardputerAudioObjectDevice = 2,
  kCardputerAudioObjectInputStream = 3,
};

typedef struct CardputerAudioDevice {
  _Atomic(CardputerAudioRing *) ring;
  _Atomic uint32_t client_count;
} CardputerAudioDevice;

void cardputer_audio_device_initialize(CardputerAudioDevice *device);
void cardputer_audio_device_set_ring(
    CardputerAudioDevice *device,
    CardputerAudioRing *ring);
uint32_t cardputer_audio_device_input_stream_count(
    const CardputerAudioDevice *device);
uint32_t cardputer_audio_device_output_stream_count(
    const CardputerAudioDevice *device);
double cardputer_audio_device_sample_rate(
    const CardputerAudioDevice *device);
uint32_t cardputer_audio_device_channel_count(
    const CardputerAudioDevice *device);
void cardputer_audio_device_start(CardputerAudioDevice *device);
void cardputer_audio_device_stop(CardputerAudioDevice *device);
uint32_t cardputer_audio_device_client_count(
    const CardputerAudioDevice *device);
uint32_t cardputer_audio_device_render(
    CardputerAudioDevice *device,
    float *output,
    uint32_t frame_count);

#endif
