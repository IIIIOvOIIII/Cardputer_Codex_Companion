package device

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/cardputer/codex-companion/windows-agent/internal/testutil"
)

func fixtureAction(t *testing.T) ActionEnvelope {
	t.Helper()
	var fixture struct {
		Response ActionEnvelope `json:"response"`
	}
	if err := json.Unmarshal(testutil.ProductFixture(t, "actions.json"), &fixture); err != nil {
		t.Fatal(err)
	}
	return fixture.Response
}

func TestFirstPairCapturesFingerprintOnlyAfterAuthenticatedRequest(t *testing.T) {
	const pin = "12345678"
	want := fixtureAction(t)
	server := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get(PairingHeader) != pin {
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}
		_ = json.NewEncoder(w).Encode(want)
	}))
	defer server.Close()

	fingerprint, got, err := FirstPair(context.Background(), server.URL, pin)
	if err != nil {
		t.Fatal(err)
	}
	sum := sha256.Sum256(server.Certificate().Raw)
	if fingerprint != hex.EncodeToString(sum[:]) {
		t.Fatalf("fingerprint mismatch: %s", fingerprint)
	}
	if got != want {
		t.Fatalf("action mismatch: %#v", got)
	}
	if _, _, err := FirstPair(context.Background(), server.URL, "00000000"); err == nil {
		t.Fatal("expected authentication failure")
	} else if strings.Contains(err.Error(), "00000000") {
		t.Fatal("error leaked pairing PIN")
	}
}

func TestPinnedClientRejectsCertificateMismatch(t *testing.T) {
	const pin = "12345678"
	handler := http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		_ = json.NewEncoder(w).Encode(ActionEnvelope{Action: "none"})
	})
	server := httptest.NewTLSServer(handler)
	defer server.Close()

	client, err := NewClient(server.URL, pin, strings.Repeat("0", 64))
	if err != nil {
		t.Fatal(err)
	}
	if _, err := client.Heartbeat(context.Background()); err == nil ||
		!strings.Contains(err.Error(), "certificate fingerprint mismatch") {
		t.Fatalf("expected fingerprint mismatch, got %v", err)
	}
}

func TestHeartbeatRetriesWithinBoundAndUsesAuthHeader(t *testing.T) {
	const pin = "87654321"
	want := fixtureAction(t)
	var calls atomic.Int32
	server := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get(PairingHeader) != pin {
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}
		if calls.Add(1) < 3 {
			http.Error(w, "retry", http.StatusServiceUnavailable)
			return
		}
		_ = json.NewEncoder(w).Encode(want)
	}))
	defer server.Close()
	sum := sha256.Sum256(server.Certificate().Raw)
	client, err := NewClient(server.URL, pin, hex.EncodeToString(sum[:]))
	if err != nil {
		t.Fatal(err)
	}
	client.Attempts = 3
	client.RetryDelay = func(int) time.Duration { return 0 }
	got, err := client.Heartbeat(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if got != want || calls.Load() != 3 {
		t.Fatalf("retry mismatch: got=%#v calls=%d", got, calls.Load())
	}
}

func TestPostStatusUsesPinnedAuthenticatedProductContract(t *testing.T) {
	const pin = "87654321"
	wantBody := []byte(`{"type":"snapshot","sequence":7}`)
	server := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/api/v1/companion/status" ||
			r.Header.Get(PairingHeader) != pin ||
			r.Header.Get("Content-Type") != "application/json" {
			http.Error(w, "invalid request", http.StatusBadRequest)
			return
		}
		var body map[string]any
		if json.NewDecoder(r.Body).Decode(&body) != nil ||
			body["type"] != "snapshot" || body["sequence"] != float64(7) {
			http.Error(w, "invalid body", http.StatusBadRequest)
			return
		}
		_ = json.NewEncoder(w).Encode(map[string]bool{"accepted": true})
	}))
	defer server.Close()
	sum := sha256.Sum256(server.Certificate().Raw)
	client, err := NewClient(server.URL, pin, hex.EncodeToString(sum[:]))
	if err != nil {
		t.Fatal(err)
	}
	var snapshot map[string]any
	if json.Unmarshal(wantBody, &snapshot) != nil {
		t.Fatal("invalid test snapshot")
	}
	if err := client.PostStatus(context.Background(), snapshot); err != nil {
		t.Fatal(err)
	}
}

func TestManualAddressFallbackAndFixtureContract(t *testing.T) {
	address, err := SelectAddress("https://192.168.1.50", []string{"https://cardputer.local"})
	if err != nil || address != "https://192.168.1.50" {
		t.Fatalf("manual address not preferred: %q %v", address, err)
	}
	address, err = SelectAddress("", []string{"https://cardputer.local"})
	if err != nil || address != "https://cardputer.local" {
		t.Fatalf("discovery fallback failed: %q %v", address, err)
	}
	if _, err := SelectAddress("", nil); err == nil {
		t.Fatal("expected no-device error")
	}
	var fixture struct {
		AuthHeader string         `json:"auth_header"`
		Response   ActionEnvelope `json:"response"`
	}
	if err := json.Unmarshal(testutil.ProductFixture(t, "actions.json"), &fixture); err != nil {
		t.Fatal(err)
	}
	if fixture.AuthHeader != PairingHeader || fixture.Response.Action != "none" {
		t.Fatalf("fixture contract mismatch: %#v", fixture)
	}
}
