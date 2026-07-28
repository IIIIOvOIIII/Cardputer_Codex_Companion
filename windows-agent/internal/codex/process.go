package codex

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os/exec"
	"sync"
	"time"
)

const (
	defaultProcessAttempts = 3
	maxRPCLineBytes        = 4 * 1024 * 1024
)

type RemoteError struct {
	Message string
}

func (err RemoteError) Error() string {
	return "Codex app-server error: " + err.Message
}

type Process struct {
	mutex           sync.Mutex
	commandFactory  func() *exec.Cmd
	command         *exec.Cmd
	input           io.WriteCloser
	scanner         *bufio.Scanner
	nextID          uint64
	pendingApproval any
	initialized     bool

	Attempts   int
	RetryDelay func(attempt int) time.Duration
}

func NewProcess(executable string) *Process {
	return NewProcessWithCommand(func() *exec.Cmd {
		return exec.Command(executable, "app-server", "--listen", "stdio://")
	})
}

func NewProcessWithCommand(factory func() *exec.Cmd) *Process {
	return &Process{
		commandFactory: factory,
		Attempts:       defaultProcessAttempts,
		RetryDelay:     processRetryDelay,
	}
}

func (process *Process) Start(ctx context.Context) error {
	process.mutex.Lock()
	defer process.mutex.Unlock()
	return process.ensureStartedLocked(ctx)
}

func (process *Process) Request(
	ctx context.Context,
	method string,
	params map[string]any,
) (map[string]any, error) {
	process.mutex.Lock()
	defer process.mutex.Unlock()
	attempts := process.attemptCount()
	var lastErr error
	for attempt := 1; attempt <= attempts; attempt++ {
		if err := process.ensureStartedLocked(ctx); err != nil {
			lastErr = err
		} else {
			result, err := process.requestOnceLocked(ctx, method, params)
			if err == nil {
				return result, nil
			}
			var remote RemoteError
			if errors.As(err, &remote) {
				return nil, err
			}
			lastErr = err
			process.stopLocked(true)
		}
		if attempt < attempts {
			if err := waitForRetry(ctx, process.retryDelay(attempt)); err != nil {
				return nil, err
			}
		}
	}
	return nil, lastErr
}

func (process *Process) RespondToPendingApproval(
	ctx context.Context,
	approved bool,
) error {
	process.mutex.Lock()
	defer process.mutex.Unlock()
	if err := process.ensureStartedLocked(ctx); err != nil {
		return err
	}
	if process.pendingApproval == nil {
		return nil
	}
	id := process.pendingApproval
	process.pendingApproval = nil
	return process.sendLocked(map[string]any{
		"jsonrpc": "2.0",
		"id":      id,
		"result": map[string]any{
			"decision": map[bool]string{true: "accept", false: "decline"}[approved],
		},
	})
}

func (process *Process) Running() bool {
	process.mutex.Lock()
	defer process.mutex.Unlock()
	return process.command != nil && process.command.Process != nil &&
		process.command.ProcessState == nil
}

func (process *Process) Close() error {
	process.mutex.Lock()
	defer process.mutex.Unlock()
	return process.stopLocked(false)
}

func (process *Process) ensureStartedLocked(ctx context.Context) error {
	if process.initialized && process.command != nil &&
		process.command.ProcessState == nil {
		return nil
	}
	attempts := process.attemptCount()
	var lastErr error
	for attempt := 1; attempt <= attempts; attempt++ {
		process.stopLocked(true)
		if err := process.startOnceLocked(); err != nil {
			lastErr = err
		} else {
			_, err := process.requestOnceLocked(ctx, "initialize", map[string]any{
				"clientInfo": map[string]any{
					"name":    "cardputer-companion",
					"title":   "Cardputer Codex Companion",
					"version": "1.3.1",
				},
				"capabilities": map[string]any{"experimentalApi": false},
			})
			if err == nil {
				err = process.sendLocked(map[string]any{
					"jsonrpc": "2.0",
					"method":  "initialized",
					"params":  map[string]any{},
				})
			}
			if err == nil {
				process.initialized = true
				return nil
			}
			lastErr = err
		}
		process.stopLocked(true)
		if attempt < attempts {
			if err := waitForRetry(ctx, process.retryDelay(attempt)); err != nil {
				return err
			}
		}
	}
	return fmt.Errorf("start Codex app-server: %w", lastErr)
}

func (process *Process) startOnceLocked() error {
	if process.commandFactory == nil {
		return errors.New("Codex command is unavailable")
	}
	command := process.commandFactory()
	input, err := command.StdinPipe()
	if err != nil {
		return err
	}
	output, err := command.StdoutPipe()
	if err != nil {
		_ = input.Close()
		return err
	}
	command.Stderr = io.Discard
	if err := command.Start(); err != nil {
		_ = input.Close()
		return err
	}
	scanner := bufio.NewScanner(output)
	scanner.Buffer(make([]byte, 64*1024), maxRPCLineBytes)
	process.command = command
	process.input = input
	process.scanner = scanner
	process.initialized = false
	process.pendingApproval = nil
	return nil
}

func (process *Process) requestOnceLocked(
	ctx context.Context,
	method string,
	params map[string]any,
) (map[string]any, error) {
	if process.command == nil || process.scanner == nil {
		return nil, errors.New("Codex app-server is not running")
	}
	process.nextID++
	requestID := process.nextID
	if err := process.sendLocked(map[string]any{
		"jsonrpc": "2.0",
		"id":      requestID,
		"method":  method,
		"params":  params,
	}); err != nil {
		return nil, err
	}
	for {
		scanned, err := process.scanLocked(ctx)
		if err != nil {
			return nil, err
		}
		if !scanned {
			break
		}
		var object map[string]any
		if err := json.Unmarshal(process.scanner.Bytes(), &object); err != nil {
			continue
		}
		if isApprovalRequest(object) {
			process.pendingApproval = object["id"]
			continue
		}
		responseID, ok := numericID(object["id"])
		if !ok || responseID != requestID {
			continue
		}
		if remote, ok := object["error"].(map[string]any); ok {
			message, _ := remote["message"].(string)
			if message == "" {
				message = "request rejected"
			}
			return nil, RemoteError{Message: message}
		}
		result, ok := object["result"].(map[string]any)
		if !ok {
			return nil, errors.New("Codex app-server returned a malformed response")
		}
		return result, nil
	}
	if err := process.scanner.Err(); err != nil {
		return nil, fmt.Errorf("read Codex app-server response: %w", err)
	}
	return nil, errors.New("Codex app-server stopped")
}

func (process *Process) scanLocked(ctx context.Context) (bool, error) {
	result := make(chan bool, 1)
	go func() {
		result <- process.scanner.Scan()
	}()
	select {
	case scanned := <-result:
		return scanned, nil
	case <-ctx.Done():
		if process.command != nil && process.command.Process != nil {
			_ = process.command.Process.Kill()
		}
		<-result
		return false, ctx.Err()
	}
}

func (process *Process) sendLocked(object map[string]any) error {
	if process.input == nil {
		return errors.New("Codex app-server is not running")
	}
	data, err := json.Marshal(object)
	if err != nil {
		return errors.New("encode Codex app-server request")
	}
	data = append(data, '\n')
	if _, err := process.input.Write(data); err != nil {
		return errors.New("write Codex app-server request")
	}
	return nil
}

func (process *Process) stopLocked(force bool) error {
	command := process.command
	input := process.input
	process.command = nil
	process.input = nil
	process.scanner = nil
	process.initialized = false
	process.pendingApproval = nil
	if command == nil || command.Process == nil {
		return nil
	}
	if input != nil {
		_ = input.Close()
	}
	if force {
		_ = command.Process.Kill()
		return command.Wait()
	}
	waited := make(chan error, 1)
	go func() { waited <- command.Wait() }()
	select {
	case err := <-waited:
		return normalizeWaitError(err)
	case <-time.After(2 * time.Second):
		_ = command.Process.Kill()
		return normalizeWaitError(<-waited)
	}
}

func (process *Process) attemptCount() int {
	if process.Attempts < 1 {
		return 1
	}
	return process.Attempts
}

func (process *Process) retryDelay(attempt int) time.Duration {
	if process.RetryDelay == nil {
		return processRetryDelay(attempt)
	}
	return process.RetryDelay(attempt)
}

func processRetryDelay(attempt int) time.Duration {
	if attempt < 1 {
		attempt = 1
	}
	return time.Duration(attempt) * 250 * time.Millisecond
}

func waitForRetry(ctx context.Context, delay time.Duration) error {
	timer := time.NewTimer(delay)
	defer timer.Stop()
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-timer.C:
		return nil
	}
}

func numericID(value any) (uint64, bool) {
	switch typed := value.(type) {
	case float64:
		if typed < 0 || typed != float64(uint64(typed)) {
			return 0, false
		}
		return uint64(typed), true
	case uint64:
		return typed, true
	case int:
		if typed < 0 {
			return 0, false
		}
		return uint64(typed), true
	default:
		return 0, false
	}
}

func isApprovalRequest(object map[string]any) bool {
	if object["id"] == nil {
		return false
	}
	method, _ := object["method"].(string)
	return method == "item/commandExecution/requestApproval" ||
		method == "item/fileChange/requestApproval"
}

func normalizeWaitError(err error) error {
	if err == nil {
		return nil
	}
	var exit *exec.ExitError
	if errors.As(err, &exit) {
		return nil
	}
	return err
}
