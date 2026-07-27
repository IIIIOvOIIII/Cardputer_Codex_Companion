package app

import (
	"context"
	"testing"
	"time"

	"github.com/cardputer/codex-companion/windows-agent/internal/codex"
	"github.com/cardputer/codex-companion/windows-agent/internal/device"
	"github.com/cardputer/codex-companion/windows-agent/internal/pet"
)

func TestAgentExecutesActionAndPostsChangedOrRequestedSnapshot(t *testing.T) {
	fast := true
	machine := &fakeCodex{
		snapshot: codex.Snapshot{
			Type:          "snapshot",
			SessionID:     "thread-1",
			Title:         "Cardputer",
			CWD:           `C:\work`,
			State:         "active",
			PetState:      "working",
			Model:         "gpt-5.6",
			ThinkingLevel: "high",
			Fast:          &fast,
		},
	}
	deviceClient := &fakeDevice{
		actions: []device.ActionEnvelope{
			{Sequence: 1, Action: "select_next", NeedsSnapshot: true},
			{Sequence: 2, Action: "none", NeedsSnapshot: false},
		},
	}
	agent := NewAgent(deviceClient, machine)
	if err := agent.Step(context.Background()); err != nil {
		t.Fatal(err)
	}
	if err := agent.Step(context.Background()); err != nil {
		t.Fatal(err)
	}
	if len(machine.actions) != 2 || machine.actions[0] != "select_next" {
		t.Fatalf("actions were not routed: %#v", machine.actions)
	}
	if len(deviceClient.snapshots) != 1 ||
		deviceClient.snapshots[0].Sequence != 1 {
		t.Fatalf("snapshot deduplication mismatch: %#v", deviceClient.snapshots)
	}
}

func TestAgentPersistsNewerPairingMigrationBeforeNextHeartbeat(t *testing.T) {
	machine := &fakeCodex{
		snapshot: codex.Snapshot{
			Type: "snapshot", Title: "NO ACTIVE CODEX", CWD: "-",
			State: "offline", PetState: "waiting",
		},
	}
	deviceClient := &fakeDevice{
		actions: []device.ActionEnvelope{{
			Action: "none", NextPairing: "87654321", PINRevision: 8,
		}},
	}
	agent := NewAgent(deviceClient, machine)
	var migrated string
	var revision uint32
	agent.SetPairingMigrationHandler(7, func(next string, nextRevision uint32) error {
		migrated = next
		revision = nextRevision
		return nil
	})
	if err := agent.Step(context.Background()); err != nil {
		t.Fatal(err)
	}
	if migrated != "87654321" || revision != 8 {
		t.Fatalf("pairing migration mismatch: %q %d", migrated, revision)
	}
}

func TestAgentSynchronizesPetOnThirtySecondCadence(t *testing.T) {
	machine := &fakeCodex{
		snapshot: codex.Snapshot{
			Type: "snapshot", Title: "NO ACTIVE CODEX", CWD: "-",
			State: "offline", PetState: "waiting",
		},
	}
	deviceClient := &fakeDevice{
		actions: []device.ActionEnvelope{
			{Action: "none"},
			{Action: "none"},
			{Action: "none"},
		},
	}
	synchronizer := &fakePetSynchronizer{
		result: pet.Result{PetID: "rocky", Digest: "digest"},
	}
	agent := NewAgent(deviceClient, machine)
	agent.SetPetSynchronizer(synchronizer)
	now := time.Unix(1_000, 0)
	agent.now = func() time.Time { return now }
	if err := agent.Step(context.Background()); err != nil {
		t.Fatal(err)
	}
	now = now.Add(29 * time.Second)
	if err := agent.Step(context.Background()); err != nil {
		t.Fatal(err)
	}
	now = now.Add(time.Second)
	if err := agent.Step(context.Background()); err != nil {
		t.Fatal(err)
	}
	if synchronizer.calls != 2 {
		t.Fatalf("pet cadence mismatch: %d", synchronizer.calls)
	}
	if deviceClient.snapshots[0].PetID != "rocky" ||
		deviceClient.snapshots[0].PetDigest != "digest" {
		t.Fatalf("pet status missing from snapshot: %#v", deviceClient.snapshots[0])
	}
}

type fakeDevice struct {
	actions   []device.ActionEnvelope
	snapshots []codex.Snapshot
}

func (client *fakeDevice) Heartbeat(context.Context) (device.ActionEnvelope, error) {
	action := client.actions[0]
	client.actions = client.actions[1:]
	return action, nil
}

func (client *fakeDevice) PostStatus(_ context.Context, snapshot any) error {
	client.snapshots = append(client.snapshots, snapshot.(codex.Snapshot))
	return nil
}

type fakeCodex struct {
	snapshot codex.Snapshot
	actions  []string
}

type fakePetSynchronizer struct {
	result pet.Result
	calls  int
}

func (sync *fakePetSynchronizer) Synchronize(context.Context) pet.Result {
	sync.calls++
	return sync.result
}

func (machine *fakeCodex) Snapshot(context.Context) (codex.Snapshot, error) {
	return machine.snapshot, nil
}

func (machine *fakeCodex) Perform(_ context.Context, action string) error {
	machine.actions = append(machine.actions, action)
	return nil
}
