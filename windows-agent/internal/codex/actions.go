package codex

import (
	"context"
	"errors"
	"time"
)

type Snapshot struct {
	Type          string       `json:"type"`
	Sequence      uint64       `json:"sequence"`
	SessionID     string       `json:"session_id"`
	Title         string       `json:"title"`
	CWD           string       `json:"cwd"`
	State         string       `json:"state"`
	Approvals     uint8        `json:"approvals"`
	Inputs        uint8        `json:"inputs"`
	PetID         string       `json:"pet_id"`
	PetDigest     string       `json:"pet_digest"`
	PetState      string       `json:"pet_state"`
	Model         string       `json:"model,omitempty"`
	ThinkingLevel string       `json:"thinking_level,omitempty"`
	Fast          *bool        `json:"fast,omitempty"`
	Limits        []LimitUsage `json:"limits,omitempty"`
}

func (snapshot Snapshot) SameContent(other Snapshot) bool {
	snapshot.Sequence = 0
	other.Sequence = 0
	left, _ := snapshotContent(snapshot)
	right, _ := snapshotContent(other)
	return left == right
}

type Adapter struct {
	rpc       RPCClient
	telemetry *TelemetryReader
	threads   []map[string]any
	selected  int
	sequence  uint64
}

func NewAdapter(rpc RPCClient) *Adapter {
	return &Adapter{
		rpc:       rpc,
		telemetry: NewTelemetryReader(rpc),
	}
}

func (adapter *Adapter) Start(ctx context.Context) error {
	return adapter.rpc.Start(ctx)
}

func (adapter *Adapter) Close() error {
	return adapter.rpc.Close()
}

func (adapter *Adapter) Snapshot(ctx context.Context) (Snapshot, error) {
	result, err := adapter.rpc.Request(ctx, "thread/list", map[string]any{
		"limit":         50,
		"sortKey":       "updated_at",
		"sortDirection": "desc",
	})
	if err != nil {
		return Snapshot{}, err
	}
	adapter.threads = objectSlice(result["data"])
	if active := activeThreadIndex(adapter.threads); active >= 0 {
		adapter.selected = active
	} else if len(adapter.threads) == 0 {
		adapter.selected = 0
	} else if adapter.selected >= len(adapter.threads) {
		adapter.selected = len(adapter.threads) - 1
	}
	adapter.sequence++
	if len(adapter.threads) == 0 {
		return Snapshot{
			Type:     "snapshot",
			Sequence: adapter.sequence,
			Title:    "NO ACTIVE CODEX",
			CWD:      "-",
			State:    "offline",
			PetState: "waiting",
		}, nil
	}
	thread := adapter.threads[adapter.selected]
	status := objectValue(thread["status"])
	flags := stringSlice(status["activeFlags"])
	title := stringValue(thread["name"])
	if title == "" {
		title = stringValue(thread["preview"])
	}
	if title == "" {
		title = "Codex session"
	}
	state := stringValue(status["type"])
	if state == "" {
		state = "unknown"
	}
	snapshot := Snapshot{
		Type:      "snapshot",
		Sequence:  adapter.sequence,
		SessionID: stringValue(thread["id"]),
		Title:     title,
		CWD:       stringValue(thread["cwd"]),
		State:     state,
		Approvals: boolByte(contains(flags, "waitingOnApproval")),
		Inputs:    boolByte(contains(flags, "waitingOnUserInput")),
		PetState:  resolvePetState(state, flags),
	}
	if snapshot.CWD == "" {
		snapshot.CWD = "-"
	}
	if telemetry, telemetryErr := adapter.telemetry.Read(
		ctx,
		thread,
		time.Now(),
	); telemetryErr == nil {
		snapshot.Model = telemetry.Model
		snapshot.ThinkingLevel = telemetry.ThinkingLevel
		snapshot.Fast = &telemetry.Fast
		snapshot.Limits = telemetry.Limits
	}
	return snapshot, nil
}

func (adapter *Adapter) Perform(ctx context.Context, action string) error {
	switch action {
	case "", "none", "new", "provide_input":
		return nil
	case "select_next":
		if len(adapter.threads) != 0 {
			adapter.selected = (adapter.selected + 1) % len(adapter.threads)
		}
		return nil
	case "select_previous":
		if len(adapter.threads) != 0 {
			adapter.selected =
				(adapter.selected + len(adapter.threads) - 1) % len(adapter.threads)
		}
		return nil
	case "approve":
		return adapter.rpc.RespondToPendingApproval(ctx, true)
	case "reject":
		return adapter.rpc.RespondToPendingApproval(ctx, false)
	case "interrupt":
		return adapter.interruptSelected(ctx)
	default:
		return errors.New("unsupported device action")
	}
}

func (adapter *Adapter) interruptSelected(ctx context.Context) error {
	if adapter.selected < 0 || adapter.selected >= len(adapter.threads) {
		return nil
	}
	threadID := stringValue(adapter.threads[adapter.selected]["id"])
	if threadID == "" {
		return nil
	}
	result, err := adapter.rpc.Request(ctx, "thread/read", map[string]any{
		"threadId":     threadID,
		"includeTurns": true,
	})
	if err != nil {
		return err
	}
	thread := objectValue(result["thread"])
	turns := objectSlice(thread["turns"])
	for index := len(turns) - 1; index >= 0; index-- {
		if stringValue(turns[index]["status"]) == "inProgress" {
			turnID := stringValue(turns[index]["id"])
			if turnID != "" {
				_, err = adapter.rpc.Request(ctx, "turn/interrupt", map[string]any{
					"threadId": threadID,
					"turnId":   turnID,
				})
				return err
			}
		}
	}
	return nil
}

func activeThreadIndex(threads []map[string]any) int {
	for index, thread := range threads {
		if stringValue(objectValue(thread["status"])["type"]) == "active" {
			return index
		}
	}
	return -1
}

func resolvePetState(state string, flags []string) string {
	normalized := ""
	for _, character := range state {
		if character != '_' {
			normalized += string(character)
		}
	}
	switch normalized {
	case "failed", "error", "cancelled":
		return "failed"
	case "notloaded":
		return "waiting"
	case "review":
		return "review"
	case "active", "running", "inprogress":
		if contains(flags, "waitingOnApproval") ||
			contains(flags, "waitingOnUserInput") {
			return "waiting"
		}
		return "working"
	default:
		if contains(flags, "waitingOnApproval") ||
			contains(flags, "waitingOnUserInput") {
			return "waiting"
		}
		if contains(flags, "review") {
			return "review"
		}
		return "idle"
	}
}

func objectSlice(value any) []map[string]any {
	switch values := value.(type) {
	case []any:
		result := make([]map[string]any, 0, len(values))
		for _, candidate := range values {
			if object := objectValue(candidate); object != nil {
				result = append(result, object)
			}
		}
		return result
	case []map[string]any:
		return values
	default:
		return nil
	}
}

func stringSlice(value any) []string {
	switch values := value.(type) {
	case []any:
		result := make([]string, 0, len(values))
		for _, candidate := range values {
			if text, ok := candidate.(string); ok {
				result = append(result, text)
			}
		}
		return result
	case []string:
		return values
	default:
		return nil
	}
}

func contains(values []string, wanted string) bool {
	for _, value := range values {
		if value == wanted {
			return true
		}
	}
	return false
}

func boolByte(value bool) uint8 {
	if value {
		return 1
	}
	return 0
}
