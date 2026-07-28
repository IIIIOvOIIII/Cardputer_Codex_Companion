package app

import (
	"context"
	"time"

	"github.com/cardputer/codex-companion/windows-agent/internal/codex"
	"github.com/cardputer/codex-companion/windows-agent/internal/device"
	"github.com/cardputer/codex-companion/windows-agent/internal/pet"
)

type DeviceClient interface {
	Heartbeat(context.Context) (device.ActionEnvelope, error)
	PostStatus(context.Context, any) error
}

type CodexClient interface {
	Snapshot(context.Context) (codex.Snapshot, error)
	Perform(context.Context, string) error
}

type PetSynchronizer interface {
	Synchronize(context.Context) pet.Result
}

type Agent struct {
	device          DeviceClient
	codex           CodexClient
	migratePairing  func(string, uint32) error
	lastPINRevision uint32
	lastSnapshot    *codex.Snapshot
	wireSequence    uint64
	petSynchronizer PetSynchronizer
	petResult       pet.Result
	nextPetSync     time.Time
	now             func() time.Time
}

func NewAgent(deviceClient DeviceClient, codexClient CodexClient) *Agent {
	return &Agent{
		device: deviceClient,
		codex:  codexClient,
		now:    time.Now,
	}
}

func (agent *Agent) SetPetSynchronizer(synchronizer PetSynchronizer) {
	agent.petSynchronizer = synchronizer
	agent.nextPetSync = time.Time{}
}

func (agent *Agent) SetPairingMigrationHandler(
	revision uint32,
	handler func(string, uint32) error,
) {
	agent.lastPINRevision = revision
	agent.migratePairing = handler
}

func (agent *Agent) Step(ctx context.Context) error {
	petAttemptedThisStep := agent.synchronizePetIfDue(ctx)
	action, err := agent.device.Heartbeat(ctx)
	if err != nil {
		return err
	}
	if action.NextPairing != "" && action.PINRevision > agent.lastPINRevision &&
		agent.migratePairing != nil {
		if err := agent.migratePairing(
			action.NextPairing,
			action.PINRevision,
		); err != nil {
			return err
		}
		agent.lastPINRevision = action.PINRevision
	}
	if action.NeedsSnapshot {
		agent.lastSnapshot = nil
		if !petAttemptedThisStep {
			agent.synchronizePet(ctx)
		}
	}
	if err := agent.codex.Perform(ctx, action.Action); err != nil {
		return err
	}
	snapshot, err := agent.codex.Snapshot(ctx)
	if err != nil {
		return err
	}
	snapshot.PetID = agent.petResult.PetID
	snapshot.PetDigest = agent.petResult.Digest
	if agent.lastSnapshot != nil &&
		snapshot.SameContent(*agent.lastSnapshot) {
		return nil
	}
	agent.wireSequence++
	snapshot.Sequence = agent.wireSequence
	if err := agent.device.PostStatus(ctx, snapshot); err != nil {
		return err
	}
	posted := snapshot
	agent.lastSnapshot = &posted
	return nil
}

func (agent *Agent) synchronizePetIfDue(ctx context.Context) bool {
	if agent.petSynchronizer == nil {
		return false
	}
	now := agent.now()
	if !agent.nextPetSync.IsZero() && now.Before(agent.nextPetSync) {
		return false
	}
	agent.synchronizePet(ctx)
	return true
}

func (agent *Agent) synchronizePet(ctx context.Context) {
	if agent.petSynchronizer == nil {
		return
	}
	agent.petResult = agent.petSynchronizer.Synchronize(ctx)
	delay := 30 * time.Second
	if agent.petResult.ErrorCode != "" {
		delay = 5 * time.Second
	}
	agent.nextPetSync = agent.now().Add(delay)
}
