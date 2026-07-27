#include "CardputerAudioIPC.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <dispatch/dispatch.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <os/log.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xpc/xpc.h>

#ifndef CARDPUTER_AUDIO_DEVELOPMENT
#define CARDPUTER_AUDIO_DEVELOPMENT 0
#endif

#ifndef CARDPUTER_AUDIO_CONSOLE_UID
#define CARDPUTER_AUDIO_CONSOLE_UID 0
#endif

#ifndef CARDPUTER_AUDIO_TEAM_ID
#define CARDPUTER_AUDIO_TEAM_ID ""
#endif

#define CARDPUTER_AUDIO_COMPANION_ID "com.lynx.cardputer-companion"
#define CARDPUTER_AUDIO_COREAUDIOD_ID "com.apple.audio.coreaudiod"
#define CARDPUTER_AUDIO_MACH_SERVICE \
  "com.lynx.cardputer-codex-microphone.ipc"

typedef struct CardputerAudioIPCServer {
  pthread_mutex_t lock;
  CardputerAudioIPCLease lease;
  int shared_fd;
  void *mapping;
  size_t mapping_size;
  xpc_connection_t listener;
} CardputerAudioIPCServer;

static CardputerAudioIPCServer g_server = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .shared_fd = -1,
};

bool cardputer_audio_ipc_authorize(
    const CardputerAudioIPCPolicy *policy,
    const CardputerAudioIPCPeer *peer) {
  if (policy == NULL || peer == NULL ||
      policy->expected_bundle_id == NULL ||
      peer->bundle_id == NULL ||
      strcmp(policy->expected_bundle_id, peer->bundle_id) != 0) {
    return false;
  }
  if (policy->require_apple_platform) {
    return peer->apple_platform && !peer->ad_hoc;
  }
  if (policy->development) {
    return peer->ad_hoc && peer->effective_uid == policy->console_uid;
  }
  return !peer->ad_hoc && policy->expected_team_id != NULL &&
         peer->team_id != NULL &&
         strcmp(policy->expected_team_id, peer->team_id) == 0;
}

void cardputer_audio_ipc_lease_initialize(
    CardputerAudioIPCLease *lease,
    CardputerAudioRing *ring) {
  if (lease == NULL) {
    return;
  }
  lease->owner = 0;
  lease->deadline_nanoseconds = 0;
  lease->ring = ring;
}

CardputerAudioIPCClaimResult cardputer_audio_ipc_claim(
    CardputerAudioIPCLease *lease,
    uint32_t protocol_version,
    uint64_t owner,
    uint64_t now_nanoseconds) {
  if (lease == NULL ||
      protocol_version != CARDPUTER_AUDIO_IPC_PROTOCOL_VERSION) {
    return kCardputerAudioIPCProtocolMismatch;
  }
  if (owner == 0) {
    return kCardputerAudioIPCLeaseBusy;
  }
  if (lease->owner != 0 && lease->owner != owner &&
      now_nanoseconds <= lease->deadline_nanoseconds) {
    return kCardputerAudioIPCLeaseBusy;
  }
  if (lease->owner != owner && lease->ring != NULL) {
    cardputer_audio_ring_reset(lease->ring);
  }
  lease->owner = owner;
  lease->deadline_nanoseconds =
      now_nanoseconds + CARDPUTER_AUDIO_IPC_LEASE_NS;
  return kCardputerAudioIPCAccepted;
}

bool cardputer_audio_ipc_heartbeat(
    CardputerAudioIPCLease *lease,
    uint64_t owner,
    uint64_t now_nanoseconds) {
  if (lease == NULL || owner == 0 || lease->owner != owner ||
      now_nanoseconds > lease->deadline_nanoseconds) {
    return false;
  }
  lease->deadline_nanoseconds =
      now_nanoseconds + CARDPUTER_AUDIO_IPC_LEASE_NS;
  if (lease->ring != NULL) {
    cardputer_audio_ring_heartbeat(lease->ring, now_nanoseconds);
  }
  return true;
}

bool cardputer_audio_ipc_release(
    CardputerAudioIPCLease *lease,
    uint64_t owner) {
  if (lease == NULL || owner == 0 || lease->owner != owner) {
    return false;
  }
  lease->owner = 0;
  lease->deadline_nanoseconds = 0;
  if (lease->ring != NULL) {
    cardputer_audio_ring_reset(lease->ring);
    cardputer_audio_ring_heartbeat(lease->ring, 0);
  }
  return true;
}

static uint64_t monotonic_nanoseconds(void) {
  static mach_timebase_info_data_t timebase = {0};
  if (timebase.numer == 0) {
    (void)mach_timebase_info(&timebase);
  }
  const uint64_t ticks = mach_absolute_time();
  return ticks * timebase.numer / timebase.denom;
}

static bool copy_cf_string(
    CFDictionaryRef dictionary,
    CFStringRef key,
    char *output,
    size_t output_size) {
  if (dictionary == NULL || output == NULL || output_size == 0) {
    return false;
  }
  CFTypeRef value = CFDictionaryGetValue(dictionary, key);
  return value != NULL &&
         CFGetTypeID(value) == CFStringGetTypeID() &&
         CFStringGetCString(
             (CFStringRef)value,
             output,
             (CFIndex)output_size,
             kCFStringEncodingUTF8);
}

static bool copy_cf_number(
    CFDictionaryRef dictionary,
    CFStringRef key,
    int *output) {
  if (dictionary == NULL || output == NULL) {
    return false;
  }
  CFTypeRef value = CFDictionaryGetValue(dictionary, key);
  return value != NULL &&
         CFGetTypeID(value) == CFNumberGetTypeID() &&
         CFNumberGetValue(
             (CFNumberRef)value,
             kCFNumberIntType,
             output);
}

static bool peer_identity(
    xpc_connection_t connection,
    char bundle_id[256],
    char team_id[128],
    CardputerAudioIPCPeer *peer) {
  if (connection == NULL || peer == NULL) {
    return false;
  }
  const pid_t pid = xpc_connection_get_pid(connection);
  CFNumberRef pid_number =
      CFNumberCreate(NULL, kCFNumberIntType, &pid);
  if (pid_number == NULL) {
    return false;
  }
  const void *keys[] = {kSecGuestAttributePid};
  const void *values[] = {pid_number};
  CFDictionaryRef attributes = CFDictionaryCreate(
      NULL,
      keys,
      values,
      1,
      &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  CFRelease(pid_number);
  if (attributes == NULL) {
    return false;
  }
  SecCodeRef code = NULL;
  OSStatus status = SecCodeCopyGuestWithAttributes(
      NULL, attributes, kSecCSDefaultFlags, &code);
  CFRelease(attributes);
  if (status != errSecSuccess || code == NULL) {
    return false;
  }
  CFDictionaryRef signing = NULL;
  status = SecCodeCopySigningInformation(
      code, kSecCSSigningInformation, &signing);
  CFRelease(code);
  if (status != errSecSuccess || signing == NULL) {
    return false;
  }
  const bool has_bundle = copy_cf_string(
      signing,
      kSecCodeInfoIdentifier,
      bundle_id,
      256);
  const bool has_team =
      copy_cf_string(
          signing,
          kSecCodeInfoTeamIdentifier,
          team_id,
          128);
  int platform_identifier = 0;
  (void)copy_cf_number(
      signing,
      kSecCodeInfoPlatformIdentifier,
      &platform_identifier);
  CFRelease(signing);
  if (!has_bundle) {
    return false;
  }
  if (!has_team) {
    team_id[0] = '\0';
  }
  peer->bundle_id = bundle_id;
  peer->team_id = team_id;
  peer->effective_uid = xpc_connection_get_euid(connection);
  peer->ad_hoc = !has_team;
  peer->apple_platform = platform_identifier != 0;
  return true;
}

static bool authorize_producer(xpc_connection_t connection) {
  char bundle_id[256] = {0};
  char team_id[128] = {0};
  CardputerAudioIPCPeer peer = {0};
  if (!peer_identity(connection, bundle_id, team_id, &peer)) {
    os_log_error(
        OS_LOG_DEFAULT,
        "Cardputer audio producer identity lookup failed: pid=%d uid=%u",
        xpc_connection_get_pid(connection),
        (unsigned)xpc_connection_get_euid(connection));
    return false;
  }
  const CardputerAudioIPCPolicy policy = {
      .expected_bundle_id = CARDPUTER_AUDIO_COMPANION_ID,
      .expected_team_id = CARDPUTER_AUDIO_TEAM_ID,
      .console_uid = CARDPUTER_AUDIO_CONSOLE_UID,
      .development = CARDPUTER_AUDIO_DEVELOPMENT != 0,
      .require_apple_platform = false,
  };
  const bool authorized = cardputer_audio_ipc_authorize(&policy, &peer);
  if (!authorized) {
    os_log_error(
        OS_LOG_DEFAULT,
        "Cardputer audio producer rejected: bundle=%{public}s "
        "team=%{public}s uid=%u adhoc=%d",
        bundle_id,
        team_id,
        (unsigned)peer.effective_uid,
        peer.ad_hoc);
  }
  return authorized;
}

static bool authorize_consumer(xpc_connection_t connection) {
  char bundle_id[256] = {0};
  char team_id[128] = {0};
  CardputerAudioIPCPeer peer = {0};
  if (!peer_identity(connection, bundle_id, team_id, &peer)) {
    return false;
  }
  const CardputerAudioIPCPolicy policy = {
      .expected_bundle_id = CARDPUTER_AUDIO_COREAUDIOD_ID,
      .expected_team_id = "",
      .console_uid = 0,
      .development = false,
      .require_apple_platform = true,
  };
  return cardputer_audio_ipc_authorize(&policy, &peer);
}

static void send_result(
    xpc_object_t request,
    bool ok,
    const char *error,
    int file_descriptor) {
  xpc_object_t reply = xpc_dictionary_create_reply(request);
  if (reply == NULL) {
    return;
  }
  xpc_dictionary_set_bool(reply, "ok", ok);
  if (error != NULL) {
    xpc_dictionary_set_string(reply, "error", error);
  }
  if (file_descriptor >= 0) {
    xpc_dictionary_set_fd(reply, "fd", file_descriptor);
  }
  xpc_connection_t remote =
      xpc_dictionary_get_remote_connection(request);
  if (remote != NULL) {
    xpc_connection_send_message(remote, reply);
  }
  xpc_release(reply);
}

static void handle_message(
    uint64_t owner,
    xpc_object_t event) {
  if (xpc_get_type(event) != XPC_TYPE_DICTIONARY) {
    pthread_mutex_lock(&g_server.lock);
    cardputer_audio_ipc_release(&g_server.lease, owner);
    pthread_mutex_unlock(&g_server.lock);
    return;
  }
  xpc_connection_t remote =
      xpc_dictionary_get_remote_connection(event);
  const char *operation = xpc_dictionary_get_string(event, "op");
  if (operation == NULL) {
    send_result(event, false, "invalid_request", -1);
    return;
  }
  const uint64_t now = monotonic_nanoseconds();
  if (strcmp(operation, "consumer_claim") == 0) {
    const uint64_t version =
        xpc_dictionary_get_uint64(event, "version");
    const bool accepted =
        version == CARDPUTER_AUDIO_IPC_PROTOCOL_VERSION &&
        authorize_consumer(remote);
    send_result(
        event,
        accepted,
        accepted ? NULL : "unauthorized_or_protocol_mismatch",
        accepted ? g_server.shared_fd : -1);
    return;
  }
  if (!authorize_producer(remote)) {
    send_result(event, false, "unauthorized", -1);
    return;
  }
  if (strcmp(operation, "hello") == 0) {
    const uint64_t version =
        xpc_dictionary_get_uint64(event, "version");
    send_result(
        event,
        version == CARDPUTER_AUDIO_IPC_PROTOCOL_VERSION,
        version == CARDPUTER_AUDIO_IPC_PROTOCOL_VERSION
            ? NULL
            : "protocol_mismatch",
        -1);
    return;
  }
  if (strcmp(operation, "claim") == 0) {
    const uint32_t version = (uint32_t)xpc_dictionary_get_uint64(
        event, "version");
    pthread_mutex_lock(&g_server.lock);
    const CardputerAudioIPCClaimResult result =
        cardputer_audio_ipc_claim(
            &g_server.lease, version, owner, now);
    pthread_mutex_unlock(&g_server.lock);
    if (result == kCardputerAudioIPCAccepted) {
      send_result(event, true, NULL, g_server.shared_fd);
    } else {
      send_result(
          event,
          false,
          result == kCardputerAudioIPCProtocolMismatch
              ? "protocol_mismatch"
              : "lease_busy",
          -1);
    }
    return;
  }
  if (strcmp(operation, "heartbeat") == 0) {
    pthread_mutex_lock(&g_server.lock);
    const bool accepted =
        cardputer_audio_ipc_heartbeat(&g_server.lease, owner, now);
    pthread_mutex_unlock(&g_server.lock);
    send_result(
        event,
        accepted,
        accepted ? NULL : "lease_expired",
        -1);
    return;
  }
  if (strcmp(operation, "release") == 0) {
    pthread_mutex_lock(&g_server.lock);
    const bool released =
        cardputer_audio_ipc_release(&g_server.lease, owner);
    pthread_mutex_unlock(&g_server.lock);
    send_result(
        event,
        released,
        released ? NULL : "not_owner",
        -1);
    return;
  }
  send_result(event, false, "unknown_operation", -1);
}

static int create_shared_ring(void) {
  char name[96];
  for (int attempt = 0; attempt < 8; ++attempt) {
    snprintf(
        name,
        sizeof(name),
        "/cardputer-audio-%d-%08x",
        getpid(),
        arc4random());
    g_server.shared_fd =
        shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (g_server.shared_fd >= 0) {
      shm_unlink(name);
      break;
    }
  }
  if (g_server.shared_fd < 0) {
    return -1;
  }
  g_server.mapping_size = cardputer_audio_ring_size();
  if (ftruncate(
          g_server.shared_fd,
          (off_t)g_server.mapping_size) != 0) {
    close(g_server.shared_fd);
    g_server.shared_fd = -1;
    return -1;
  }
  g_server.mapping = mmap(
      NULL,
      g_server.mapping_size,
      PROT_READ | PROT_WRITE,
      MAP_SHARED,
      g_server.shared_fd,
      0);
  if (g_server.mapping == MAP_FAILED) {
    close(g_server.shared_fd);
    g_server.shared_fd = -1;
    g_server.mapping = NULL;
    return -1;
  }
  CardputerAudioRing *ring = g_server.mapping;
  cardputer_audio_ring_initialize(ring);
  cardputer_audio_ipc_lease_initialize(&g_server.lease, ring);
  return 0;
}

int cardputer_audio_ipc_server_run(void) {
  if (g_server.listener != NULL) {
    return 0;
  }
  if (create_shared_ring() != 0) {
    return -1;
  }
  dispatch_queue_t queue = dispatch_queue_create(
      "com.lynx.cardputer.audio.ipc", DISPATCH_QUEUE_SERIAL);
  g_server.listener = xpc_connection_create_mach_service(
      CARDPUTER_AUDIO_MACH_SERVICE,
      queue,
      XPC_CONNECTION_MACH_SERVICE_LISTENER);
  if (g_server.listener == NULL) {
    return -1;
  }
  xpc_connection_set_event_handler(
      g_server.listener,
      ^(xpc_object_t peer_object) {
        if (xpc_get_type(peer_object) != XPC_TYPE_CONNECTION) {
          return;
        }
        xpc_connection_t peer = (xpc_connection_t)peer_object;
        const uint64_t owner = (uint64_t)(uintptr_t)peer;
        xpc_connection_set_event_handler(
            peer,
            ^(xpc_object_t event) {
              handle_message(owner, event);
            });
        xpc_connection_resume(peer);
      });
  xpc_connection_resume(g_server.listener);
  dispatch_main();
  return 0;
}
