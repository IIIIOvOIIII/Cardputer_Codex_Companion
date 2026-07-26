#ifndef CARDPUTER_AUDIO_XPC_CLIENT_H
#define CARDPUTER_AUDIO_XPC_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CardputerAudioXPCClient CardputerAudioXPCClient;

#if __has_attribute(swift_name)
#define CARDPUTER_SWIFT_NAME(name) __attribute__((swift_name(name)))
#else
#define CARDPUTER_SWIFT_NAME(name)
#endif

CardputerAudioXPCClient *cardputer_audio_xpc_client_create(void)
    CARDPUTER_SWIFT_NAME("cardputerAudioXPCClientCreate()");
void cardputer_audio_xpc_client_destroy(
    CardputerAudioXPCClient *client)
    CARDPUTER_SWIFT_NAME("cardputerAudioXPCClientDestroy(_:)");
bool cardputer_audio_xpc_client_hello(
    CardputerAudioXPCClient *client,
    uint32_t version)
    CARDPUTER_SWIFT_NAME("cardputerAudioXPCClientHello(_:version:)");
int cardputer_audio_xpc_client_claim(
    CardputerAudioXPCClient *client,
    uint32_t version)
    CARDPUTER_SWIFT_NAME("cardputerAudioXPCClientClaim(_:version:)");
bool cardputer_audio_xpc_client_heartbeat(
    CardputerAudioXPCClient *client)
    CARDPUTER_SWIFT_NAME("cardputerAudioXPCClientHeartbeat(_:)");
void cardputer_audio_xpc_client_release(
    CardputerAudioXPCClient *client)
    CARDPUTER_SWIFT_NAME("cardputerAudioXPCClientRelease(_:)");

#undef CARDPUTER_SWIFT_NAME

#ifdef __cplusplus
}
#endif

#endif
