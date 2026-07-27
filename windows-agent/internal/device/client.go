package device

import (
	"context"
	"crypto/sha256"
	"crypto/tls"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"

	"github.com/cardputer/codex-companion/windows-agent/internal/config"
)

const (
	PairingHeader        = "X-Cardputer-Pairing"
	actionPath           = "/api/v1/companion/action"
	statusPath           = "/api/v1/companion/status"
	maxResponseBytes     = 64 * 1024
	defaultAttempts      = 3
	defaultClientTimeout = 10 * time.Second
)

type ActionEnvelope struct {
	Sequence      uint32 `json:"sequence"`
	Action        string `json:"action"`
	NeedsSnapshot bool   `json:"needs_snapshot"`
	NextPairing   string `json:"next_pairing,omitempty"`
	PINRevision   uint32 `json:"pin_revision,omitempty"`
}

type Client struct {
	address    string
	pairingPIN string
	httpClient *http.Client

	Attempts   int
	RetryDelay func(attempt int) time.Duration
}

func FirstPair(ctx context.Context, address string, pairingPIN string) (string, ActionEnvelope, error) {
	normalized, err := normalizeAddress(address)
	if err != nil {
		return "", ActionEnvelope{}, err
	}
	if !config.PairingPINValid(pairingPIN) {
		return "", ActionEnvelope{}, errors.New("pairing PIN must contain exactly eight digits")
	}
	var fingerprint string
	tlsConfig := &tls.Config{
		MinVersion:         tls.VersionTLS12,
		InsecureSkipVerify: true,
		VerifyConnection: func(state tls.ConnectionState) error {
			if len(state.PeerCertificates) == 0 {
				return errors.New("device did not provide a certificate")
			}
			sum := sha256.Sum256(state.PeerCertificates[0].Raw)
			fingerprint = hex.EncodeToString(sum[:])
			return nil
		},
	}
	httpClient := newHTTPClient(tlsConfig)
	action, status, requestErr := requestAction(ctx, httpClient, normalized, pairingPIN)
	if requestErr != nil {
		return "", ActionEnvelope{}, requestErr
	}
	if status < 200 || status >= 300 {
		return "", ActionEnvelope{}, fmt.Errorf("device authentication failed with HTTP status %d", status)
	}
	if fingerprint == "" {
		return "", ActionEnvelope{}, errors.New("device certificate fingerprint was not captured")
	}
	return fingerprint, action, nil
}

func NewClient(address string, pairingPIN string, certificateSHA256 string) (*Client, error) {
	normalized, err := normalizeAddress(address)
	if err != nil {
		return nil, err
	}
	configuration := config.Config{
		DeviceURL:         normalized,
		CertificateSHA256: certificateSHA256,
	}
	if err := configuration.Validate(); err != nil {
		return nil, err
	}
	if !config.PairingPINValid(pairingPIN) {
		return nil, errors.New("pairing PIN must contain exactly eight digits")
	}
	expectedFingerprint := strings.ToLower(certificateSHA256)
	tlsConfig := &tls.Config{
		MinVersion:         tls.VersionTLS12,
		InsecureSkipVerify: true,
		VerifyConnection: func(state tls.ConnectionState) error {
			if len(state.PeerCertificates) == 0 {
				return errors.New("device did not provide a certificate")
			}
			sum := sha256.Sum256(state.PeerCertificates[0].Raw)
			if hex.EncodeToString(sum[:]) != expectedFingerprint {
				return errors.New("certificate fingerprint mismatch")
			}
			return nil
		},
	}
	return &Client{
		address:    normalized,
		pairingPIN: pairingPIN,
		httpClient: newHTTPClient(tlsConfig),
		Attempts:   defaultAttempts,
		RetryDelay: defaultRetryDelay,
	}, nil
}

func (client *Client) Heartbeat(ctx context.Context) (ActionEnvelope, error) {
	attempts := client.Attempts
	if attempts < 1 {
		attempts = 1
	}
	delay := client.RetryDelay
	if delay == nil {
		delay = defaultRetryDelay
	}
	var lastErr error
	for attempt := 1; attempt <= attempts; attempt++ {
		action, status, err := requestAction(
			ctx,
			client.httpClient,
			client.address,
			client.pairingPIN,
		)
		retryable := err != nil || status == http.StatusTooManyRequests || status >= 500
		if err == nil && status >= 200 && status < 300 {
			return action, nil
		}
		if err != nil {
			lastErr = err
		} else {
			lastErr = fmt.Errorf("device returned HTTP status %d", status)
		}
		if !retryable || attempt == attempts {
			break
		}
		timer := time.NewTimer(delay(attempt))
		select {
		case <-ctx.Done():
			timer.Stop()
			return ActionEnvelope{}, ctx.Err()
		case <-timer.C:
		}
	}
	return ActionEnvelope{}, lastErr
}

func (client *Client) PostStatus(ctx context.Context, snapshot any) error {
	body, err := json.Marshal(snapshot)
	if err != nil {
		return errors.New("encode device status")
	}
	request, err := http.NewRequestWithContext(
		ctx,
		http.MethodPost,
		client.address+statusPath,
		strings.NewReader(string(body)),
	)
	if err != nil {
		return errors.New("create device status request")
	}
	request.Header.Set(PairingHeader, client.pairingPIN)
	request.Header.Set("Content-Type", "application/json")
	request.Header.Set("Accept", "application/json")
	response, err := client.httpClient.Do(request)
	if err != nil {
		return fmt.Errorf("device status transport failed: %w", err)
	}
	defer response.Body.Close()
	_, _ = io.Copy(io.Discard, io.LimitReader(response.Body, maxResponseBytes))
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		return fmt.Errorf("device returned HTTP status %d", response.StatusCode)
	}
	return nil
}

func (client *Client) UpdatePairing(pairingPIN string) error {
	if !config.PairingPINValid(pairingPIN) {
		return errors.New("pairing PIN must contain exactly eight digits")
	}
	client.pairingPIN = pairingPIN
	return nil
}

func requestAction(
	ctx context.Context,
	httpClient *http.Client,
	address string,
	pairingPIN string,
) (ActionEnvelope, int, error) {
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, address+actionPath, nil)
	if err != nil {
		return ActionEnvelope{}, 0, errors.New("create device heartbeat request")
	}
	request.Header.Set(PairingHeader, pairingPIN)
	request.Header.Set("Accept", "application/json")
	response, err := httpClient.Do(request)
	if err != nil {
		return ActionEnvelope{}, 0, fmt.Errorf("device heartbeat transport failed: %w", err)
	}
	defer response.Body.Close()
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		_, _ = io.Copy(io.Discard, io.LimitReader(response.Body, maxResponseBytes))
		return ActionEnvelope{}, response.StatusCode, nil
	}
	decoder := json.NewDecoder(io.LimitReader(response.Body, maxResponseBytes+1))
	var action ActionEnvelope
	if err := decoder.Decode(&action); err != nil {
		return ActionEnvelope{}, response.StatusCode, errors.New("decode device heartbeat response")
	}
	if action.Action == "" {
		return ActionEnvelope{}, response.StatusCode, errors.New("device heartbeat response omitted action")
	}
	return action, response.StatusCode, nil
}

func newHTTPClient(tlsConfig *tls.Config) *http.Client {
	return &http.Client{
		Timeout: defaultClientTimeout,
		Transport: &http.Transport{
			Proxy:                 http.ProxyFromEnvironment,
			TLSClientConfig:       tlsConfig,
			ForceAttemptHTTP2:     false,
			MaxIdleConns:          2,
			MaxIdleConnsPerHost:   2,
			IdleConnTimeout:       30 * time.Second,
			TLSHandshakeTimeout:   5 * time.Second,
			ResponseHeaderTimeout: 5 * time.Second,
		},
	}
}

func defaultRetryDelay(attempt int) time.Duration {
	if attempt < 1 {
		attempt = 1
	}
	return time.Duration(attempt) * 250 * time.Millisecond
}
