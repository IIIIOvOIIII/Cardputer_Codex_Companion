package config

import (
	"bytes"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

type fakeProtector struct {
	fail bool
}

func (p fakeProtector) Protect(plaintext []byte) ([]byte, error) {
	if p.fail {
		return nil, errors.New("protector unavailable")
	}
	output := append([]byte(nil), plaintext...)
	for left, right := 0, len(output)-1; left < right; left, right = left+1, right-1 {
		output[left], output[right] = output[right], output[left]
	}
	return output, nil
}

func (p fakeProtector) Unprotect(ciphertext []byte) ([]byte, error) {
	return p.Protect(ciphertext)
}

func TestStoreEncryptsPairingAndUsesPrivateAtomicFile(t *testing.T) {
	path := filepath.Join(t.TempDir(), "config.json")
	store := Store{Path: path, Protector: fakeProtector{}}
	const pin = "12345678"
	want := Config{
		DeviceURL:         "https://192.168.1.195",
		CertificateSHA256: strings.Repeat("a", 64),
	}
	if err := store.Save(want, pin); err != nil {
		t.Fatal(err)
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if bytes.Contains(raw, []byte(pin)) {
		t.Fatal("pairing PIN was written in plaintext")
	}
	info, err := os.Stat(path)
	if err != nil {
		t.Fatal(err)
	}
	if info.Mode().Perm()&0o077 != 0 {
		t.Fatalf("config permissions are not private: %o", info.Mode().Perm())
	}
	got, gotPIN, err := store.Load()
	if err != nil {
		t.Fatal(err)
	}
	if got != want || gotPIN != pin {
		t.Fatalf("round trip mismatch: %#v PIN length=%d", got, len(gotPIN))
	}
	matches, err := filepath.Glob(filepath.Join(filepath.Dir(path), ".config-*"))
	if err != nil {
		t.Fatal(err)
	}
	if len(matches) != 0 {
		t.Fatalf("atomic temporary file left behind: %v", matches)
	}
}

func TestStoreDoesNotReplaceValidConfigWhenProtectionFails(t *testing.T) {
	path := filepath.Join(t.TempDir(), "config.json")
	good := Store{Path: path, Protector: fakeProtector{}}
	config := Config{
		DeviceURL:         "https://cardputer.local",
		CertificateSHA256: strings.Repeat("b", 64),
	}
	if err := good.Save(config, "87654321"); err != nil {
		t.Fatal(err)
	}
	before, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	broken := Store{Path: path, Protector: fakeProtector{fail: true}}
	err = broken.Save(config, "11112222")
	if err == nil {
		t.Fatal("expected protection failure")
	}
	after, readErr := os.ReadFile(path)
	if readErr != nil {
		t.Fatal(readErr)
	}
	if !bytes.Equal(before, after) {
		t.Fatal("failed write replaced the valid config")
	}
	if strings.Contains(err.Error(), "11112222") {
		t.Fatal("error leaked pairing PIN")
	}
}

func TestConfigValidationAndRedaction(t *testing.T) {
	valid := Config{
		DeviceURL:         "https://192.168.1.195",
		CertificateSHA256: strings.Repeat("c", 64),
	}
	if err := valid.Validate(); err != nil {
		t.Fatal(err)
	}
	for _, candidate := range []Config{
		{DeviceURL: "http://192.168.1.195", CertificateSHA256: valid.CertificateSHA256},
		{DeviceURL: valid.DeviceURL, CertificateSHA256: "short"},
	} {
		if err := candidate.Validate(); err == nil {
			t.Fatalf("expected invalid config: %#v", candidate)
		}
	}
	if PairingPINValid("1234567") || PairingPINValid("abcdefgh") ||
		!PairingPINValid("12345678") {
		t.Fatal("pairing PIN validation mismatch")
	}
}
