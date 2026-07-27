package app

import (
	"context"

	"github.com/cardputer/codex-companion/windows-agent/internal/codex"
	"github.com/cardputer/codex-companion/windows-agent/internal/device"
)

type DeviceClient interface {
	Heartbeat(context.Context) (device.ActionEnvelope, error)
	PostStatus(context.Context, any) error
}

type CodexClient interface {
	Snapshot(context.Context) (codex.Snapshot, error)
	Perform(context.Context, string) error
}

type Agent struct {
	device          DeviceClient
	codex           CodexClient
	migratePairing  func(string, uint32) error
	lastPINRevision uint32
	lastSnapshot    *codex.Snapshot
	wireSequence    uint64
}

func NewAgent(deviceClient DeviceClient, codexClient CodexClient) *Agent {
	return &Agent{
		device: deviceClient,
		codex:  codexClient,
	}
}

func (agent *Agent) SetPairingMigrationHandler(
	revision uint32,
	handler func(string, uint32) error,
) {
	agent.lastPINRevision = revision
	agent.migratePairing = handler
}

func (agent *Agent) Step(ctx context.Context) error {
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
	}
	if err := agent.codex.Perform(ctx, action.Action); err != nil {
		return err
	}
	snapshot, err := agent.codex.Snapshot(ctx)
	if err != nil {
		return err
	}
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
