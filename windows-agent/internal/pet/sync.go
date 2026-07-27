package pet

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"os"
	"strings"
)

const ChunkMaximum = 8192

type Transaction struct {
	Active   bool   `json:"active"`
	ID       string `json:"id"`
	Received int    `json:"received"`
	Expected int    `json:"expected"`
}

type Status struct {
	PetID         string      `json:"pet_id"`
	Digest        string      `json:"digest"`
	FormatVersion uint16      `json:"format_version"`
	StorageUsed   int         `json:"storage_used"`
	Transaction   Transaction `json:"transaction"`
	LastResult    string      `json:"last_result"`
}

type Receipt struct {
	TransactionID string `json:"transaction_id"`
	Received      int    `json:"received"`
}

type Device interface {
	PetStatus(context.Context) (Status, error)
	BeginPetUpload(context.Context, Bundle) (Receipt, error)
	PutPetChunk(context.Context, string, int, []byte) error
	CommitPetUpload(context.Context, string) (Status, error)
}

type Result struct {
	PetID     string
	Digest    string
	ErrorCode string
}

type Coordinator struct {
	LoadSource   func() (Source, error)
	Transcode    func(Source) (Bundle, error)
	Device       Device
	SourceDigest func(Source) (string, error)

	cachedInputDigest string
	cachedBundle      *Bundle
	failedInputDigest string
	failedErrorCode   string
	lastSuccess       Result
}

func NewCoordinator(
	loadSource func() (Source, error),
	transcode func(Source) (Bundle, error),
	device Device,
) *Coordinator {
	coordinator := &Coordinator{
		LoadSource: loadSource,
		Transcode:  transcode,
		Device:     device,
	}
	coordinator.SourceDigest = coordinator.sourceDigest
	return coordinator
}

func (coordinator *Coordinator) Synchronize(ctx context.Context) Result {
	source, err := coordinator.LoadSource()
	if err != nil {
		return coordinator.failure(stableErrorCode(err))
	}
	inputDigest, err := coordinator.SourceDigest(source)
	if err != nil {
		return coordinator.failure(stableErrorCode(err))
	}
	if inputDigest == coordinator.failedInputDigest &&
		coordinator.failedErrorCode != "" {
		return coordinator.failure(coordinator.failedErrorCode)
	}
	var bundle Bundle
	if inputDigest == coordinator.cachedInputDigest &&
		coordinator.cachedBundle != nil {
		bundle = *coordinator.cachedBundle
	} else {
		bundle, err = coordinator.Transcode(source)
		if err != nil {
			code := stableErrorCode(err)
			coordinator.failedInputDigest = inputDigest
			coordinator.failedErrorCode = code
			return coordinator.failure(code)
		}
		coordinator.cachedInputDigest = inputDigest
		cached := bundle
		coordinator.cachedBundle = &cached
		coordinator.failedInputDigest = ""
		coordinator.failedErrorCode = ""
	}

	status, err := coordinator.Device.PetStatus(ctx)
	if err != nil {
		return coordinator.failure("sync_failed")
	}
	if status.Digest == bundle.ContentDigest {
		coordinator.lastSuccess = Result{
			PetID: bundle.PetID, Digest: bundle.ContentDigest,
		}
		return coordinator.lastSuccess
	}

	transactionID := ""
	offset := 0
	if status.Transaction.Active &&
		status.Transaction.Expected == len(bundle.Data) &&
		status.Transaction.ID != "" {
		transactionID = status.Transaction.ID
		offset = status.Transaction.Received
	} else {
		receipt, beginErr := coordinator.Device.BeginPetUpload(ctx, bundle)
		if beginErr != nil {
			return coordinator.failure("sync_failed")
		}
		transactionID = receipt.TransactionID
		offset = receipt.Received
	}
	if transactionID == "" || offset < 0 || offset > len(bundle.Data) {
		return coordinator.failure("invalid_device_offset")
	}
	for offset < len(bundle.Data) {
		end := min(offset+ChunkMaximum, len(bundle.Data))
		chunk := bundle.Data[offset:end]
		if putErr := coordinator.Device.PutPetChunk(
			ctx,
			transactionID,
			offset,
			chunk,
		); putErr != nil {
			status, statusErr := coordinator.Device.PetStatus(ctx)
			if statusErr == nil &&
				status.Transaction.ID == transactionID &&
				status.Transaction.Received >= end &&
				status.Transaction.Received <= len(bundle.Data) {
				offset = status.Transaction.Received
				continue
			}
			if retryErr := coordinator.Device.PutPetChunk(
				ctx,
				transactionID,
				offset,
				chunk,
			); retryErr != nil {
				return coordinator.failure("sync_failed")
			}
		}
		offset = end
	}
	committed, err := coordinator.Device.CommitPetUpload(ctx, transactionID)
	if err != nil {
		return coordinator.failure("sync_failed")
	}
	if committed.Digest != bundle.ContentDigest {
		return coordinator.failure("commit_digest_mismatch")
	}
	coordinator.lastSuccess = Result{
		PetID: bundle.PetID, Digest: bundle.ContentDigest,
	}
	return coordinator.lastSuccess
}

func (coordinator *Coordinator) sourceDigest(source Source) (string, error) {
	data, err := os.ReadFile(source.AtlasPath)
	if err != nil {
		return "", err
	}
	input := make([]byte, 0, len(source.ID)+len(data)+12)
	input = append(input, []byte(source.ID)...)
	input = append(input, byte(source.Version))
	input = append(input, 0x00, 96, 0x00, 104)
	input = append(input, 0x01, 0x90)
	input = append(input, 0x05, 0x08, 0x0d)
	input = append(input, data...)
	sum := sha256.Sum256(input)
	return hex.EncodeToString(sum[:]), nil
}

func (coordinator *Coordinator) failure(code string) Result {
	return Result{
		PetID:     coordinator.lastSuccess.PetID,
		Digest:    coordinator.lastSuccess.Digest,
		ErrorCode: code,
	}
}

func stableErrorCode(err error) string {
	message := err.Error()
	switch {
	case errors.Is(err, os.ErrNotExist),
		message == "pet selection is missing",
		message == "custom pet source was not found":
		return "source_not_found"
	case strings.Contains(message, "invalid"),
		strings.Contains(message, "path traversal"):
		return "source_invalid"
	default:
		return "sync_failed"
	}
}
