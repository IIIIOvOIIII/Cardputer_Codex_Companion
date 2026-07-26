#ifndef CARDPUTER_AUDIO_RING_H
#define CARDPUTER_AUDIO_RING_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CARDPUTER_AUDIO_RING_MAGIC UINT32_C(0x43414D49)
#define CARDPUTER_AUDIO_RING_VERSION UINT32_C(1)
#define CARDPUTER_AUDIO_RING_CAPACITY UINT32_C(16384)

typedef struct CardputerAudioRing {
  uint32_t magic;
  uint32_t version;
  _Atomic uint64_t read_counter;
  _Atomic uint64_t write_counter;
  _Atomic uint64_t producer_heartbeat_nanoseconds;
  float frames[CARDPUTER_AUDIO_RING_CAPACITY];
} CardputerAudioRing;

size_t cardputer_audio_ring_size(void);
uint32_t cardputer_audio_ring_capacity(void);
bool cardputer_audio_ring_is_valid(const CardputerAudioRing *ring);
void cardputer_audio_ring_initialize(CardputerAudioRing *ring);
uint32_t cardputer_audio_ring_available(const CardputerAudioRing *ring);
uint32_t cardputer_audio_ring_write(
    CardputerAudioRing *ring,
    const float *frames,
    uint32_t frame_count);
uint32_t cardputer_audio_ring_read(
    CardputerAudioRing *ring,
    float *frames,
    uint32_t frame_count);
uint32_t cardputer_audio_ring_read_or_silence(
    CardputerAudioRing *ring,
    float *frames,
    uint32_t frame_count);
void cardputer_audio_ring_reset(CardputerAudioRing *ring);
void cardputer_audio_ring_heartbeat(
    CardputerAudioRing *ring,
    uint64_t monotonic_nanoseconds);
uint64_t cardputer_audio_ring_last_heartbeat(
    const CardputerAudioRing *ring);

#ifdef __cplusplus
}
#endif

#endif
