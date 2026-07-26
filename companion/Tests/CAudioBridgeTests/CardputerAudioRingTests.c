#include "CardputerAudioRing.h"

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>

typedef struct {
  CardputerAudioRing *ring;
  uint32_t frame_count;
} StressContext;

static void *stress_producer(void *opaque) {
  StressContext *context = opaque;
  for (uint32_t value = 0; value < context->frame_count;) {
    const float sample = (float)value;
    if (cardputer_audio_ring_write(context->ring, &sample, 1) == 1) {
      ++value;
    } else {
      sched_yield();
    }
  }
  return NULL;
}

static void *stress_consumer(void *opaque) {
  StressContext *context = opaque;
  for (uint32_t expected = 0; expected < context->frame_count;) {
    float sample = -1.0f;
    if (cardputer_audio_ring_read(context->ring, &sample, 1) == 1) {
      assert(sample == (float)expected);
      ++expected;
    } else {
      sched_yield();
    }
  }
  return NULL;
}

int main(void) {
  CardputerAudioRing ring;
  cardputer_audio_ring_initialize(&ring);
  assert(ring.magic == CARDPUTER_AUDIO_RING_MAGIC);
  assert(ring.version == CARDPUTER_AUDIO_RING_VERSION);
  assert(cardputer_audio_ring_size() == sizeof(CardputerAudioRing));
  assert(cardputer_audio_ring_capacity() == 16384u);

  float input[480];
  float output[480];
  for (uint32_t index = 0; index < 480; ++index) {
    input[index] = (float)index / 480.0f;
  }
  assert(cardputer_audio_ring_write(&ring, input, 480) == 480);
  assert(cardputer_audio_ring_read(&ring, output, 480) == 480);
  assert(memcmp(input, output, sizeof(input)) == 0);

  for (uint32_t pass = 0; pass < 96; ++pass) {
    assert(cardputer_audio_ring_write(&ring, input, 480) == 480);
    assert(cardputer_audio_ring_read(&ring, output, 480) == 480);
    assert(memcmp(input, output, sizeof(input)) == 0);
  }

  float capacity[CARDPUTER_AUDIO_RING_CAPACITY];
  memset(capacity, 0x3f, sizeof(capacity));
  assert(cardputer_audio_ring_write(
             &ring, capacity, CARDPUTER_AUDIO_RING_CAPACITY) ==
         CARDPUTER_AUDIO_RING_CAPACITY);
  assert(cardputer_audio_ring_write(&ring, input, 1) == 0);
  assert(cardputer_audio_ring_read(
             &ring, capacity, CARDPUTER_AUDIO_RING_CAPACITY) ==
         CARDPUTER_AUDIO_RING_CAPACITY);

  output[0] = 1.0f;
  output[1] = 1.0f;
  assert(cardputer_audio_ring_read_or_silence(&ring, output, 2) == 0);
  assert(output[0] == 0.0f);
  assert(output[1] == 0.0f);

  assert(cardputer_audio_ring_write(&ring, input, 2) == 2);
  cardputer_audio_ring_reset(&ring);
  assert(cardputer_audio_ring_read(&ring, output, 2) == 0);
  cardputer_audio_ring_heartbeat(&ring, UINT64_C(0x1122334455667788));
  assert(cardputer_audio_ring_last_heartbeat(&ring) ==
         UINT64_C(0x1122334455667788));

  StressContext context = {
      .ring = &ring,
      .frame_count = 100000,
  };
  pthread_t producer;
  pthread_t consumer;
  assert(pthread_create(&producer, NULL, stress_producer, &context) == 0);
  assert(pthread_create(&consumer, NULL, stress_consumer, &context) == 0);
  assert(pthread_join(producer, NULL) == 0);
  assert(pthread_join(consumer, NULL) == 0);
  return 0;
}
