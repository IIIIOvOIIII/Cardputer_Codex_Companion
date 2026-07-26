#ifndef CARDPUTER_AUDIO_IPC_H
#define CARDPUTER_AUDIO_IPC_H

#include "CardputerAudioDevice.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#define CARDPUTER_AUDIO_IPC_PROTOCOL_VERSION UINT32_C(1)
#define CARDPUTER_AUDIO_IPC_LEASE_NS UINT64_C(2000000000)

typedef struct CardputerAudioIPCPolicy {
  const char *expected_bundle_id;
  const char *expected_team_id;
  uid_t console_uid;
  bool development;
} CardputerAudioIPCPolicy;

typedef struct CardputerAudioIPCPeer {
  const char *bundle_id;
  const char *team_id;
  uid_t effective_uid;
  bool ad_hoc;
} CardputerAudioIPCPeer;

typedef enum CardputerAudioIPCClaimResult {
  kCardputerAudioIPCAccepted = 0,
  kCardputerAudioIPCProtocolMismatch = 1,
  kCardputerAudioIPCLeaseBusy = 2,
} CardputerAudioIPCClaimResult;

typedef struct CardputerAudioIPCLease {
  uint64_t owner;
  uint64_t deadline_nanoseconds;
  CardputerAudioRing *ring;
} CardputerAudioIPCLease;

bool cardputer_audio_ipc_authorize(
    const CardputerAudioIPCPolicy *policy,
    const CardputerAudioIPCPeer *peer);
void cardputer_audio_ipc_lease_initialize(
    CardputerAudioIPCLease *lease,
    CardputerAudioRing *ring);
CardputerAudioIPCClaimResult cardputer_audio_ipc_claim(
    CardputerAudioIPCLease *lease,
    uint32_t protocol_version,
    uint64_t owner,
    uint64_t now_nanoseconds);
bool cardputer_audio_ipc_heartbeat(
    CardputerAudioIPCLease *lease,
    uint64_t owner,
    uint64_t now_nanoseconds);
bool cardputer_audio_ipc_release(
    CardputerAudioIPCLease *lease,
    uint64_t owner);
int cardputer_audio_ipc_server_start(CardputerAudioDevice *device);

#endif
