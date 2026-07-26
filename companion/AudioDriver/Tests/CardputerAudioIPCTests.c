#include "CardputerAudioIPC.h"

#include <assert.h>

int main(void) {
  const CardputerAudioIPCPolicy development = {
      .expected_bundle_id = "com.lynx.cardputer-companion",
      .expected_team_id = "",
      .console_uid = 501,
      .development = true,
  };
  CardputerAudioIPCPeer peer = {
      .bundle_id = "com.lynx.cardputer-companion",
      .team_id = "",
      .effective_uid = 501,
      .ad_hoc = true,
      .apple_platform = false,
  };
  assert(cardputer_audio_ipc_authorize(&development, &peer));
  peer.bundle_id = "com.example.attacker";
  assert(!cardputer_audio_ipc_authorize(&development, &peer));
  peer.bundle_id = "com.lynx.cardputer-companion";
  peer.effective_uid = 502;
  assert(!cardputer_audio_ipc_authorize(&development, &peer));

  const CardputerAudioIPCPolicy release = {
      .expected_bundle_id = "com.lynx.cardputer-companion",
      .expected_team_id = "TEAM123456",
      .console_uid = 501,
      .development = false,
  };
  peer.effective_uid = 501;
  peer.ad_hoc = false;
  peer.team_id = "WRONGTEAM";
  assert(!cardputer_audio_ipc_authorize(&release, &peer));
  peer.team_id = "TEAM123456";
  assert(cardputer_audio_ipc_authorize(&release, &peer));

  const CardputerAudioIPCPolicy consumer = {
      .expected_bundle_id = "com.apple.audio.coreaudiod",
      .expected_team_id = "",
      .console_uid = 0,
      .development = false,
      .require_apple_platform = true,
  };
  peer.bundle_id = "com.apple.audio.coreaudiod";
  peer.team_id = "";
  peer.effective_uid = 202;
  peer.ad_hoc = false;
  peer.apple_platform = false;
  assert(!cardputer_audio_ipc_authorize(&consumer, &peer));
  peer.apple_platform = true;
  assert(cardputer_audio_ipc_authorize(&consumer, &peer));

  CardputerAudioRing ring;
  cardputer_audio_ring_initialize(&ring);
  const float sample = 0.5f;
  assert(cardputer_audio_ring_write(&ring, &sample, 1) == 1);

  CardputerAudioIPCLease lease;
  cardputer_audio_ipc_lease_initialize(&lease, &ring);
  assert(
      cardputer_audio_ipc_claim(&lease, 2, 100, 0) ==
      kCardputerAudioIPCProtocolMismatch);
  assert(
      cardputer_audio_ipc_claim(&lease, 1, 100, 0) ==
      kCardputerAudioIPCAccepted);
  assert(
      cardputer_audio_ipc_claim(&lease, 1, 200, 1) ==
      kCardputerAudioIPCLeaseBusy);
  assert(cardputer_audio_ipc_heartbeat(&lease, 100, 1));
  assert(
      cardputer_audio_ipc_claim(
          &lease, 1, 200, CARDPUTER_AUDIO_IPC_LEASE_NS + 2) ==
      kCardputerAudioIPCAccepted);
  assert(!cardputer_audio_ipc_release(&lease, 100));
  assert(cardputer_audio_ipc_release(&lease, 200));
  assert(cardputer_audio_ring_available(&ring) == 0);
  return 0;
}
