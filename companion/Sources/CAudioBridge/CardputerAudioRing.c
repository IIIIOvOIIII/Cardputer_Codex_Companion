#include "CardputerAudioRing.h"

#include <string.h>

static uint32_t bounded_available(uint64_t read_counter, uint64_t write_counter) {
  const uint64_t distance = write_counter - read_counter;
  return distance > CARDPUTER_AUDIO_RING_CAPACITY
             ? CARDPUTER_AUDIO_RING_CAPACITY
             : (uint32_t)distance;
}

static void copy_into_ring(
    CardputerAudioRing *ring,
    uint64_t write_counter,
    const float *frames,
    uint32_t frame_count) {
  const uint32_t offset =
      (uint32_t)(write_counter % CARDPUTER_AUDIO_RING_CAPACITY);
  uint32_t first = CARDPUTER_AUDIO_RING_CAPACITY - offset;
  if (first > frame_count) {
    first = frame_count;
  }
  memcpy(&ring->frames[offset], frames, (size_t)first * sizeof(float));
  if (first < frame_count) {
    memcpy(
        ring->frames,
        frames + first,
        (size_t)(frame_count - first) * sizeof(float));
  }
}

static void copy_from_ring(
    const CardputerAudioRing *ring,
    uint64_t read_counter,
    float *frames,
    uint32_t frame_count) {
  const uint32_t offset =
      (uint32_t)(read_counter % CARDPUTER_AUDIO_RING_CAPACITY);
  uint32_t first = CARDPUTER_AUDIO_RING_CAPACITY - offset;
  if (first > frame_count) {
    first = frame_count;
  }
  memcpy(frames, &ring->frames[offset], (size_t)first * sizeof(float));
  if (first < frame_count) {
    memcpy(
        frames + first,
        ring->frames,
        (size_t)(frame_count - first) * sizeof(float));
  }
}

size_t cardputer_audio_ring_size(void) {
  return sizeof(CardputerAudioRing);
}

uint32_t cardputer_audio_ring_capacity(void) {
  return CARDPUTER_AUDIO_RING_CAPACITY;
}

bool cardputer_audio_ring_is_valid(const CardputerAudioRing *ring) {
  return ring != NULL && ring->magic == CARDPUTER_AUDIO_RING_MAGIC &&
         ring->version == CARDPUTER_AUDIO_RING_VERSION;
}

void cardputer_audio_ring_initialize(CardputerAudioRing *ring) {
  if (ring == NULL) {
    return;
  }
  memset(ring, 0, sizeof(*ring));
  ring->magic = CARDPUTER_AUDIO_RING_MAGIC;
  ring->version = CARDPUTER_AUDIO_RING_VERSION;
  atomic_init(&ring->read_counter, 0);
  atomic_init(&ring->write_counter, 0);
  atomic_init(&ring->producer_heartbeat_nanoseconds, 0);
}

uint32_t cardputer_audio_ring_available(const CardputerAudioRing *ring) {
  if (!cardputer_audio_ring_is_valid(ring)) {
    return 0;
  }
  const uint64_t write_counter =
      atomic_load_explicit(&ring->write_counter, memory_order_acquire);
  const uint64_t read_counter =
      atomic_load_explicit(&ring->read_counter, memory_order_acquire);
  return bounded_available(read_counter, write_counter);
}

uint32_t cardputer_audio_ring_write(
    CardputerAudioRing *ring,
    const float *frames,
    uint32_t frame_count) {
  if (!cardputer_audio_ring_is_valid(ring) || frames == NULL ||
      frame_count == 0) {
    return 0;
  }
  const uint64_t write_counter =
      atomic_load_explicit(&ring->write_counter, memory_order_relaxed);
  const uint64_t read_counter =
      atomic_load_explicit(&ring->read_counter, memory_order_acquire);
  const uint32_t available = bounded_available(read_counter, write_counter);
  const uint32_t writable = CARDPUTER_AUDIO_RING_CAPACITY - available;
  const uint32_t accepted = frame_count < writable ? frame_count : writable;
  if (accepted == 0) {
    return 0;
  }
  copy_into_ring(ring, write_counter, frames, accepted);
  atomic_store_explicit(
      &ring->write_counter, write_counter + accepted, memory_order_release);
  return accepted;
}

uint32_t cardputer_audio_ring_read(
    CardputerAudioRing *ring,
    float *frames,
    uint32_t frame_count) {
  if (!cardputer_audio_ring_is_valid(ring) || frames == NULL ||
      frame_count == 0) {
    return 0;
  }
  const uint64_t read_counter =
      atomic_load_explicit(&ring->read_counter, memory_order_relaxed);
  const uint64_t write_counter =
      atomic_load_explicit(&ring->write_counter, memory_order_acquire);
  const uint32_t available = bounded_available(read_counter, write_counter);
  const uint32_t consumed = frame_count < available ? frame_count : available;
  if (consumed == 0) {
    return 0;
  }
  copy_from_ring(ring, read_counter, frames, consumed);
  atomic_store_explicit(
      &ring->read_counter, read_counter + consumed, memory_order_release);
  return consumed;
}

uint32_t cardputer_audio_ring_read_or_silence(
    CardputerAudioRing *ring,
    float *frames,
    uint32_t frame_count) {
  if (frames == NULL || frame_count == 0) {
    return 0;
  }
  const uint32_t consumed =
      cardputer_audio_ring_read(ring, frames, frame_count);
  if (consumed < frame_count) {
    memset(
        frames + consumed,
        0,
        (size_t)(frame_count - consumed) * sizeof(float));
  }
  return consumed;
}

void cardputer_audio_ring_reset(CardputerAudioRing *ring) {
  if (!cardputer_audio_ring_is_valid(ring)) {
    return;
  }
  const uint64_t write_counter =
      atomic_load_explicit(&ring->write_counter, memory_order_acquire);
  atomic_store_explicit(
      &ring->read_counter, write_counter, memory_order_release);
}

void cardputer_audio_ring_heartbeat(
    CardputerAudioRing *ring,
    uint64_t monotonic_nanoseconds) {
  if (!cardputer_audio_ring_is_valid(ring)) {
    return;
  }
  atomic_store_explicit(
      &ring->producer_heartbeat_nanoseconds,
      monotonic_nanoseconds,
      memory_order_release);
}

uint64_t cardputer_audio_ring_last_heartbeat(
    const CardputerAudioRing *ring) {
  if (!cardputer_audio_ring_is_valid(ring)) {
    return 0;
  }
  return atomic_load_explicit(
      &ring->producer_heartbeat_nanoseconds, memory_order_acquire);
}
