package codex

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"io"
	"os"
	"strings"
	"time"
	"unicode"
)

const maxRolloutTailBytes int64 = 8 * 1024 * 1024

type LimitUsage struct {
	Scope       string `json:"scope"`
	Window      string `json:"window"`
	UsedPercent uint8  `json:"used_percent"`
}

type Telemetry struct {
	Model         string       `json:"model,omitempty"`
	ThinkingLevel string       `json:"thinking_level,omitempty"`
	Fast          bool         `json:"fast"`
	Limits        []LimitUsage `json:"limits,omitempty"`
}

type TelemetryReader struct {
	rpc             RPCClient
	lastRateAttempt time.Time
	lastRateSuccess time.Time
	cachedLimits    []LimitUsage
}

func NewTelemetryReader(rpc RPCClient) *TelemetryReader {
	return &TelemetryReader{rpc: rpc}
}

func (reader *TelemetryReader) Read(
	ctx context.Context,
	thread map[string]any,
	now time.Time,
) (Telemetry, error) {
	configResult, err := reader.rpc.Request(ctx, "config/read", map[string]any{})
	if err != nil {
		return Telemetry{}, err
	}
	configuration := objectValue(configResult["config"])
	if configuration == nil {
		configuration = configResult
	}
	contextModel, contextEffort := latestTurnContext(stringValue(thread["path"]))
	model := contextModel
	if model == "" {
		model = stringValue(configuration["model"])
	}
	thinking := contextEffort
	if thinking == "" {
		thinking = stringValue(configuration["model_reasoning_effort"])
	}
	tier := strings.ToLower(stringValue(configuration["service_tier"]))

	if reader.lastRateAttempt.IsZero() ||
		now.Sub(reader.lastRateAttempt) >= 60*time.Second {
		reader.lastRateAttempt = now
		if result, rateErr := reader.rpc.Request(
			ctx,
			"account/rateLimits/read",
			map[string]any{},
		); rateErr == nil {
			reader.cachedLimits = classifyLimits(result)
			reader.lastRateSuccess = now
		}
	}
	var limits []LimitUsage
	if !reader.lastRateSuccess.IsZero() &&
		now.Sub(reader.lastRateSuccess) <= 120*time.Second {
		limits = append([]LimitUsage(nil), reader.cachedLimits...)
	}
	return Telemetry{
		Model:         model,
		ThinkingLevel: thinking,
		Fast:          tier == "priority" || tier == "fast",
		Limits:        limits,
	}, nil
}

func classifyLimits(result map[string]any) []LimitUsage {
	raw := objectValue(result["rateLimitsByLimitId"])
	if raw == nil {
		raw = objectValue(result["rateLimitsByLimitID"])
	}
	type slot struct {
		scope  string
		window string
	}
	candidates := map[slot][]uint8{}
	for _, rawBucket := range raw {
		bucket := objectValue(rawBucket)
		scope := classifyScope(bucket)
		if scope == "" {
			continue
		}
		for _, key := range []string{"primary", "secondary"} {
			window := objectValue(bucket[key])
			minutes, minutesOK := integerValue(window["windowDurationMins"])
			percent, percentOK := integerValue(window["usedPercent"])
			if !minutesOK || !percentOK || percent < 0 || percent > 100 {
				continue
			}
			windowName := ""
			switch minutes {
			case 300:
				windowName = "5h"
			case 10080:
				windowName = "weekly"
			}
			if windowName != "" {
				key := slot{scope: scope, window: windowName}
				candidates[key] = append(candidates[key], uint8(percent))
			}
		}
	}
	order := []slot{
		{scope: "codex", window: "5h"},
		{scope: "codex", window: "weekly"},
		{scope: "spark", window: "5h"},
		{scope: "spark", window: "weekly"},
	}
	limits := make([]LimitUsage, 0, len(order))
	for _, key := range order {
		values := candidates[key]
		if len(values) == 1 {
			limits = append(limits, LimitUsage{
				Scope:       key.scope,
				Window:      key.window,
				UsedPercent: values[0],
			})
		}
	}
	return limits
}

func classifyScope(bucket map[string]any) string {
	identity := normalizeIdentity(
		stringValue(bucket["limitId"]) + " " +
			stringValue(bucket["limitName"]),
	)
	if strings.Contains(identity, "gpt53codexspark") {
		return "spark"
	}
	if strings.Contains(identity, "codex") &&
		!strings.Contains(identity, "spark") {
		return "codex"
	}
	return ""
}

func normalizeIdentity(value string) string {
	return strings.Map(func(character rune) rune {
		if unicode.IsLetter(character) || unicode.IsDigit(character) {
			return unicode.ToLower(character)
		}
		return -1
	}, value)
}

func latestTurnContext(path string) (string, string) {
	if path == "" {
		return "", ""
	}
	file, err := os.Open(path)
	if err != nil {
		return "", ""
	}
	defer file.Close()
	info, err := file.Stat()
	if err != nil {
		return "", ""
	}
	start := info.Size() - maxRolloutTailBytes
	if start < 0 {
		start = 0
	}
	if _, err := file.Seek(start, 0); err != nil {
		return "", ""
	}
	reader := bufio.NewReaderSize(file, 64*1024)
	if start > 0 {
		discardLine(reader)
	}
	var model string
	var effort string
	for {
		line, readErr := readBoundedLine(reader, maxRPCLineBytes)
		if line != nil {
			var event struct {
				Type    string         `json:"type"`
				Payload map[string]any `json:"payload"`
			}
			if json.Unmarshal(line, &event) == nil &&
				event.Type == "turn_context" {
				model = stringValue(event.Payload["model"])
				effort = stringValue(event.Payload["effort"])
			}
		}
		if errors.Is(readErr, io.EOF) {
			break
		}
		if readErr != nil {
			break
		}
	}
	return model, effort
}

func readBoundedLine(reader *bufio.Reader, maximum int) ([]byte, error) {
	line := make([]byte, 0, 64*1024)
	overflow := false
	for {
		fragment, err := reader.ReadSlice('\n')
		if !overflow {
			if len(line)+len(fragment) <= maximum {
				line = append(line, fragment...)
			} else {
				line = nil
				overflow = true
			}
		}
		if errors.Is(err, bufio.ErrBufferFull) {
			continue
		}
		if len(line) != 0 && line[len(line)-1] == '\n' {
			line = line[:len(line)-1]
		}
		return line, err
	}
}

func discardLine(reader *bufio.Reader) {
	for {
		_, err := reader.ReadSlice('\n')
		if errors.Is(err, bufio.ErrBufferFull) {
			continue
		}
		return
	}
}

func objectValue(value any) map[string]any {
	object, _ := value.(map[string]any)
	return object
}

func stringValue(value any) string {
	text, _ := value.(string)
	return text
}

func integerValue(value any) (int, bool) {
	switch typed := value.(type) {
	case int:
		return typed, true
	case float64:
		return int(typed), typed == float64(int(typed))
	case json.Number:
		value, err := typed.Int64()
		return int(value), err == nil
	default:
		return 0, false
	}
}
