package config

import (
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/url"
	"os"
	"path/filepath"
)

const (
	configSchemaVersion = 1
	maxConfigBytes      = 64 * 1024
)

type Config struct {
	DeviceURL         string `json:"device_url"`
	CertificateSHA256 string `json:"certificate_sha256"`
}

func (c Config) Validate() error {
	parsed, err := url.Parse(c.DeviceURL)
	if err != nil || parsed.Scheme != "https" || parsed.Host == "" ||
		parsed.User != nil || parsed.RawQuery != "" || parsed.Fragment != "" ||
		(parsed.Path != "" && parsed.Path != "/") {
		return errors.New("device URL must be an HTTPS origin")
	}
	if len(c.CertificateSHA256) != 64 {
		return errors.New("certificate fingerprint must be SHA-256")
	}
	if _, err := hex.DecodeString(c.CertificateSHA256); err != nil {
		return errors.New("certificate fingerprint must be SHA-256")
	}
	return nil
}

func PairingPINValid(pin string) bool {
	if len(pin) != 8 {
		return false
	}
	for _, character := range pin {
		if character < '0' || character > '9' {
			return false
		}
	}
	return true
}

type Store struct {
	Path      string
	Protector Protector
}

type diskConfig struct {
	SchemaVersion       int    `json:"schema_version"`
	DeviceURL           string `json:"device_url"`
	CertificateSHA256   string `json:"certificate_sha256"`
	ProtectedPairingPIN string `json:"protected_pairing_pin"`
}

func (s Store) Save(configuration Config, pairingPIN string) error {
	if err := configuration.Validate(); err != nil {
		return err
	}
	if !PairingPINValid(pairingPIN) {
		return errors.New("pairing PIN must contain exactly eight digits")
	}
	if s.Path == "" || s.Protector == nil {
		return errors.New("secure configuration store is unavailable")
	}

	plaintext := []byte(pairingPIN)
	protected, err := s.Protector.Protect(plaintext)
	clearBytes(plaintext)
	if err != nil {
		return fmt.Errorf("protect pairing material: %w", err)
	}
	defer clearBytes(protected)

	encoded, err := json.MarshalIndent(diskConfig{
		SchemaVersion:       configSchemaVersion,
		DeviceURL:           configuration.DeviceURL,
		CertificateSHA256:   configuration.CertificateSHA256,
		ProtectedPairingPIN: base64.StdEncoding.EncodeToString(protected),
	}, "", "  ")
	if err != nil {
		return errors.New("encode secure configuration")
	}
	encoded = append(encoded, '\n')

	parent := filepath.Dir(s.Path)
	if err := os.MkdirAll(parent, 0o700); err != nil {
		return fmt.Errorf("create configuration directory: %w", err)
	}
	if err := os.Chmod(parent, 0o700); err != nil {
		return fmt.Errorf("secure configuration directory: %w", err)
	}
	temporary, err := os.CreateTemp(parent, ".config-*")
	if err != nil {
		return fmt.Errorf("create temporary configuration: %w", err)
	}
	temporaryPath := temporary.Name()
	keepTemporary := true
	defer func() {
		if keepTemporary {
			_ = os.Remove(temporaryPath)
		}
	}()
	if err := temporary.Chmod(0o600); err != nil {
		_ = temporary.Close()
		return fmt.Errorf("secure temporary configuration: %w", err)
	}
	if _, err := temporary.Write(encoded); err != nil {
		_ = temporary.Close()
		return fmt.Errorf("write secure configuration: %w", err)
	}
	if err := temporary.Sync(); err != nil {
		_ = temporary.Close()
		return fmt.Errorf("sync secure configuration: %w", err)
	}
	if err := temporary.Close(); err != nil {
		return fmt.Errorf("close secure configuration: %w", err)
	}
	if err := replaceFile(temporaryPath, s.Path); err != nil {
		return fmt.Errorf("replace secure configuration: %w", err)
	}
	keepTemporary = false
	if err := os.Chmod(s.Path, 0o600); err != nil {
		return fmt.Errorf("secure configuration permissions: %w", err)
	}
	return nil
}

func (s Store) Load() (Config, string, error) {
	if s.Path == "" || s.Protector == nil {
		return Config{}, "", errors.New("secure configuration store is unavailable")
	}
	file, err := os.Open(s.Path)
	if err != nil {
		return Config{}, "", fmt.Errorf("open secure configuration: %w", err)
	}
	defer file.Close()
	decoder := json.NewDecoder(io.LimitReader(file, maxConfigBytes+1))
	decoder.DisallowUnknownFields()
	var stored diskConfig
	if err := decoder.Decode(&stored); err != nil {
		return Config{}, "", errors.New("decode secure configuration")
	}
	if stored.SchemaVersion != configSchemaVersion {
		return Config{}, "", errors.New("unsupported secure configuration version")
	}
	configuration := Config{
		DeviceURL:         stored.DeviceURL,
		CertificateSHA256: stored.CertificateSHA256,
	}
	if err := configuration.Validate(); err != nil {
		return Config{}, "", err
	}
	protected, err := base64.StdEncoding.DecodeString(stored.ProtectedPairingPIN)
	if err != nil {
		return Config{}, "", errors.New("decode protected pairing material")
	}
	defer clearBytes(protected)
	plaintext, err := s.Protector.Unprotect(protected)
	if err != nil {
		return Config{}, "", fmt.Errorf("unprotect pairing material: %w", err)
	}
	defer clearBytes(plaintext)
	pairingPIN := string(plaintext)
	if !PairingPINValid(pairingPIN) {
		return Config{}, "", errors.New("protected pairing material is invalid")
	}
	return configuration, pairingPIN, nil
}

func clearBytes(value []byte) {
	for index := range value {
		value[index] = 0
	}
}
