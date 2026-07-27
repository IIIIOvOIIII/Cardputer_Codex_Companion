package device

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strconv"

	"github.com/cardputer/codex-companion/windows-agent/internal/pet"
)

func (client *Client) PetStatus(ctx context.Context) (pet.Status, error) {
	var status pet.Status
	err := client.petRequest(
		ctx,
		http.MethodGet,
		"/api/v1/companion/pet",
		nil,
		nil,
		&status,
	)
	return status, err
}

func (client *Client) BeginPetUpload(
	ctx context.Context,
	bundle pet.Bundle,
) (pet.Receipt, error) {
	body, err := json.Marshal(map[string]any{
		"pet_id":         bundle.PetID,
		"format_version": pet.SchemaVersion,
		"length":         len(bundle.Data),
		"sha256":         bundle.UploadDigest,
	})
	if err != nil {
		return pet.Receipt{}, errors.New("encode pet upload request")
	}
	var receipt pet.Receipt
	err = client.petRequest(
		ctx,
		http.MethodPost,
		"/api/v1/companion/pet/begin",
		map[string]string{"Content-Type": "application/json"},
		body,
		&receipt,
	)
	return receipt, err
}

func (client *Client) PutPetChunk(
	ctx context.Context,
	transactionID string,
	offset int,
	data []byte,
) error {
	if transactionID == "" || offset < 0 || len(data) == 0 ||
		len(data) > pet.ChunkMaximum {
		return errors.New("pet upload chunk is invalid")
	}
	sum := sha256.Sum256(data)
	return client.petRequest(
		ctx,
		http.MethodPut,
		"/api/v1/companion/pet/chunk",
		map[string]string{
			"Content-Type":       "application/octet-stream",
			"X-Pet-Transaction":  transactionID,
			"X-Pet-Offset":       strconv.Itoa(offset),
			"X-Pet-Chunk-SHA256": hex.EncodeToString(sum[:]),
		},
		data,
		nil,
	)
}

func (client *Client) CommitPetUpload(
	ctx context.Context,
	transactionID string,
) (pet.Status, error) {
	if transactionID == "" {
		return pet.Status{}, errors.New("pet upload transaction is invalid")
	}
	body, err := json.Marshal(map[string]string{
		"transaction_id": transactionID,
	})
	if err != nil {
		return pet.Status{}, errors.New("encode pet commit request")
	}
	var status pet.Status
	err = client.petRequest(
		ctx,
		http.MethodPost,
		"/api/v1/companion/pet/commit",
		map[string]string{"Content-Type": "application/json"},
		body,
		&status,
	)
	return status, err
}

func (client *Client) petRequest(
	ctx context.Context,
	method string,
	path string,
	headers map[string]string,
	body []byte,
	target any,
) error {
	request, err := http.NewRequestWithContext(
		ctx,
		method,
		client.address+path,
		bytes.NewReader(body),
	)
	if err != nil {
		return errors.New("create pet synchronization request")
	}
	request.Header.Set(PairingHeader, client.pairingPIN)
	request.Header.Set("Accept", "application/json")
	for name, value := range headers {
		request.Header.Set(name, value)
	}
	response, err := client.httpClient.Do(request)
	if err != nil {
		return fmt.Errorf("pet synchronization transport failed: %w", err)
	}
	defer response.Body.Close()
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		_, _ = io.Copy(io.Discard, io.LimitReader(response.Body, maxResponseBytes))
		return fmt.Errorf("device returned HTTP status %d", response.StatusCode)
	}
	if target == nil {
		_, _ = io.Copy(io.Discard, io.LimitReader(response.Body, maxResponseBytes))
		return nil
	}
	decoder := json.NewDecoder(io.LimitReader(response.Body, maxResponseBytes+1))
	if err := decoder.Decode(target); err != nil {
		return errors.New("decode pet synchronization response")
	}
	return nil
}
