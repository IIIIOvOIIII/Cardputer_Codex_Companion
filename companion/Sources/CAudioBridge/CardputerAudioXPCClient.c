#include "CardputerAudioXPCClient.h"

#include <stdlib.h>
#include <string.h>
#include <xpc/xpc.h>

#define CARDPUTER_AUDIO_MACH_SERVICE \
  "com.lynx.cardputer-codex-microphone.ipc"

struct CardputerAudioXPCClient {
  xpc_connection_t connection;
};

static xpc_object_t send_request(
    CardputerAudioXPCClient *client,
    const char *operation,
    uint32_t version) {
  if (client == NULL || client->connection == NULL ||
      operation == NULL) {
    return NULL;
  }
  xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
  xpc_dictionary_set_string(request, "op", operation);
  if (version != 0) {
    xpc_dictionary_set_uint64(request, "version", version);
  }
  xpc_object_t reply =
      xpc_connection_send_message_with_reply_sync(
          client->connection, request);
  xpc_release(request);
  if (reply == NULL ||
      xpc_get_type(reply) != XPC_TYPE_DICTIONARY ||
      !xpc_dictionary_get_bool(reply, "ok")) {
    if (reply != NULL) {
      xpc_release(reply);
    }
    return NULL;
  }
  return reply;
}

CardputerAudioXPCClient *cardputer_audio_xpc_client_create(void) {
  CardputerAudioXPCClient *client = calloc(1, sizeof(*client));
  if (client == NULL) {
    return NULL;
  }
  client->connection = xpc_connection_create_mach_service(
      CARDPUTER_AUDIO_MACH_SERVICE, NULL, 0);
  if (client->connection == NULL) {
    free(client);
    return NULL;
  }
  xpc_connection_set_event_handler(
      client->connection,
      ^(xpc_object_t event) {
        (void)event;
      });
  xpc_connection_resume(client->connection);
  return client;
}

void cardputer_audio_xpc_client_destroy(
    CardputerAudioXPCClient *client) {
  if (client == NULL) {
    return;
  }
  if (client->connection != NULL) {
    xpc_connection_cancel(client->connection);
    xpc_release(client->connection);
  }
  free(client);
}

bool cardputer_audio_xpc_client_hello(
    CardputerAudioXPCClient *client,
    uint32_t version) {
  xpc_object_t reply = send_request(client, "hello", version);
  if (reply == NULL) {
    return false;
  }
  xpc_release(reply);
  return true;
}

int cardputer_audio_xpc_client_claim(
    CardputerAudioXPCClient *client,
    uint32_t version) {
  xpc_object_t reply = send_request(client, "claim", version);
  if (reply == NULL) {
    return -1;
  }
  const int descriptor = xpc_dictionary_dup_fd(reply, "fd");
  xpc_release(reply);
  return descriptor;
}

bool cardputer_audio_xpc_client_heartbeat(
    CardputerAudioXPCClient *client) {
  xpc_object_t reply = send_request(client, "heartbeat", 0);
  if (reply == NULL) {
    return false;
  }
  xpc_release(reply);
  return true;
}

void cardputer_audio_xpc_client_release(
    CardputerAudioXPCClient *client) {
  xpc_object_t reply = send_request(client, "release", 0);
  if (reply != NULL) {
    xpc_release(reply);
  }
}
