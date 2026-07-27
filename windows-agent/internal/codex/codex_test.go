package codex

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

func TestJSONRPCProcessCorrelatesResponsesAndIgnoresMalformedEvents(t *testing.T) {
	process := NewProcessWithCommand(helperCommand(t, false))
	process.RetryDelay = func(int) time.Duration { return 0 }
	t.Cleanup(func() { _ = process.Close() })
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := process.Start(ctx); err != nil {
		t.Fatal(err)
	}
	result, err := process.Request(ctx, "test/echo", map[string]any{"value": "ready"})
	if err != nil {
		t.Fatal(err)
	}
	if result["value"] != "ready" {
		t.Fatalf("response correlation failed: %#v", result)
	}
	if err := process.Close(); err != nil {
		t.Fatal(err)
	}
	if process.Running() {
		t.Fatal("process remained running after close")
	}
}

func TestJSONRPCProcessRestartsWithBoundedBackoff(t *testing.T) {
	var starts atomic.Int32
	factory := func() *exec.Cmd {
		exitEarly := starts.Add(1) == 1
		return helperCommand(t, exitEarly)()
	}
	process := NewProcessWithCommand(factory)
	process.Attempts = 2
	process.RetryDelay = func(int) time.Duration { return 0 }
	t.Cleanup(func() { _ = process.Close() })
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := process.Start(ctx); err != nil {
		t.Fatal(err)
	}
	if starts.Load() != 2 {
		t.Fatalf("restart count mismatch: %d", starts.Load())
	}
}

func TestTelemetryPrefersTurnContextAndOmitsUnavailableLimits(t *testing.T) {
	rollout := filepath.Join(t.TempDir(), "rollout.jsonl")
	content := strings.Join([]string{
		`{"type":"turn_context","payload":{"model":"old","effort":"low"}}`,
		`not-json`,
		`{"type":"turn_context","payload":{"model":"gpt-5.6","effort":"high"}}`,
	}, "\n") + "\n"
	if err := os.WriteFile(rollout, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
	rpc := &fakeRPC{
		results: map[string]map[string]any{
			"config/read": {
				"config": map[string]any{
					"model":                  "fallback",
					"model_reasoning_effort": "medium",
					"service_tier":           "priority",
				},
			},
			"account/rateLimits/read": {
				"rateLimitsByLimitId": map[string]any{
					"codex": map[string]any{
						"limitId": "codex",
						"primary": map[string]any{
							"windowDurationMins": 300,
							"usedPercent":        38,
						},
					},
				},
			},
		},
	}
	reader := NewTelemetryReader(rpc)
	telemetry, err := reader.Read(
		context.Background(),
		map[string]any{"path": rollout},
		time.Unix(1_000, 0),
	)
	if err != nil {
		t.Fatal(err)
	}
	if telemetry.Model != "gpt-5.6" || telemetry.ThinkingLevel != "high" ||
		!telemetry.Fast {
		t.Fatalf("telemetry mismatch: %#v", telemetry)
	}
	if len(telemetry.Limits) != 1 || telemetry.Limits[0].Window != "5h" {
		t.Fatalf("classified limits mismatch: %#v", telemetry.Limits)
	}

	rpc.errors = map[string]error{
		"account/rateLimits/read": fmt.Errorf("temporarily unavailable"),
	}
	fresh := NewTelemetryReader(rpc)
	withoutLimits, err := fresh.Read(
		context.Background(),
		map[string]any{"path": rollout},
		time.Unix(2_000, 0),
	)
	if err != nil {
		t.Fatal(err)
	}
	encoded, err := json.Marshal(withoutLimits)
	if err != nil {
		t.Fatal(err)
	}
	if strings.Contains(string(encoded), `"limits"`) {
		t.Fatalf("unavailable limits were serialized: %s", encoded)
	}
}

func TestTelemetrySkipsOversizedRolloutEvent(t *testing.T) {
	rollout := filepath.Join(t.TempDir(), "oversized.jsonl")
	content := strings.Repeat("x", maxRPCLineBytes+1) + "\n" +
		`{"type":"turn_context","payload":{"model":"gpt-5.6","effort":"xhigh"}}` +
		"\n"
	if err := os.WriteFile(rollout, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
	model, effort := latestTurnContext(rollout)
	if model != "gpt-5.6" || effort != "xhigh" {
		t.Fatalf("valid event after oversized line was lost: %q %q", model, effort)
	}
}

func TestAdapterRoutesSupportedActionsAndNormalizesActiveSession(t *testing.T) {
	rpc := &fakeRPC{
		results: map[string]map[string]any{
			"thread/list": {
				"data": []any{
					map[string]any{
						"id":   "thread-1",
						"name": "Cardputer",
						"cwd":  `C:\work`,
						"status": map[string]any{
							"type":        "active",
							"activeFlags": []any{"waitingOnApproval"},
						},
					},
				},
			},
			"config/read": {
				"config": map[string]any{
					"model":                  "gpt-5.6",
					"model_reasoning_effort": "high",
					"service_tier":           "fast",
				},
			},
			"account/rateLimits/read": {},
			"thread/read": {
				"thread": map[string]any{
					"turns": []any{
						map[string]any{
							"id":     "turn-1",
							"status": "inProgress",
						},
					},
				},
			},
			"turn/interrupt": {},
		},
	}
	adapter := NewAdapter(rpc)
	snapshot, err := adapter.Snapshot(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if snapshot.SessionID != "thread-1" || snapshot.Approvals != 1 ||
		snapshot.PetState != "waiting" || snapshot.Model != "gpt-5.6" ||
		snapshot.Fast == nil || !*snapshot.Fast {
		t.Fatalf("snapshot mismatch: %#v", snapshot)
	}
	for _, action := range []string{
		"select_next", "select_previous", "interrupt", "approve", "reject",
		"none", "new", "provide_input",
	} {
		if err := adapter.Perform(context.Background(), action); err != nil {
			t.Fatalf("perform %s: %v", action, err)
		}
	}
	if rpc.approvals != 1 || rpc.rejections != 1 ||
		rpc.calls["turn/interrupt"] != 1 {
		t.Fatalf("action routing mismatch: %#v", rpc)
	}
}

type fakeRPC struct {
	results    map[string]map[string]any
	errors     map[string]error
	calls      map[string]int
	approvals  int
	rejections int
}

func (rpc *fakeRPC) Start(context.Context) error { return nil }
func (rpc *fakeRPC) Close() error                { return nil }

func (rpc *fakeRPC) Request(
	_ context.Context,
	method string,
	_ map[string]any,
) (map[string]any, error) {
	if rpc.calls == nil {
		rpc.calls = map[string]int{}
	}
	rpc.calls[method]++
	if err := rpc.errors[method]; err != nil {
		return nil, err
	}
	return rpc.results[method], nil
}

func (rpc *fakeRPC) RespondToPendingApproval(
	_ context.Context,
	approved bool,
) error {
	if approved {
		rpc.approvals++
	} else {
		rpc.rejections++
	}
	return nil
}

func helperCommand(t *testing.T, exitEarly bool) func() *exec.Cmd {
	t.Helper()
	return func() *exec.Cmd {
		command := exec.Command(os.Args[0], "-test.run=TestJSONRPCHelperProcess")
		command.Env = append(
			os.Environ(),
			"CARDPUTER_JSONRPC_HELPER=1",
			fmt.Sprintf("CARDPUTER_JSONRPC_EXIT_EARLY=%t", exitEarly),
		)
		return command
	}
}

func TestJSONRPCHelperProcess(t *testing.T) {
	if os.Getenv("CARDPUTER_JSONRPC_HELPER") != "1" {
		return
	}
	scanner := bufio.NewScanner(os.Stdin)
	encoder := json.NewEncoder(os.Stdout)
	for scanner.Scan() {
		var request map[string]any
		if json.Unmarshal(scanner.Bytes(), &request) != nil {
			continue
		}
		method, _ := request["method"].(string)
		if method == "initialize" &&
			os.Getenv("CARDPUTER_JSONRPC_EXIT_EARLY") == "true" {
			os.Exit(7)
		}
		if request["id"] == nil {
			continue
		}
		if method == "test/echo" {
			fmt.Fprintln(os.Stdout, "not-json")
			_ = encoder.Encode(map[string]any{
				"jsonrpc": "2.0",
				"id":      999,
				"result":  map[string]any{"value": "wrong"},
			})
		}
		params, _ := request["params"].(map[string]any)
		result := map[string]any{}
		if method == "test/echo" {
			result["value"] = params["value"]
		}
		_ = encoder.Encode(map[string]any{
			"jsonrpc": "2.0",
			"id":      request["id"],
			"result":  result,
		})
	}
	os.Exit(0)
}
