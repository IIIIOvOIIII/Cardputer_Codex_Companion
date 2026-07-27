package pet

import (
	"context"
	"encoding/json"
	"image"
	"image/color"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"

	"github.com/cardputer/codex-companion/windows-agent/internal/testutil"
)

func TestSelectionPrefersHighestOfficialAtlasAndSupportsCustomV2(t *testing.T) {
	root := t.TempDir()
	assets := filepath.Join(root, "cache", "tui-pets", "v1", "assets")
	if err := os.MkdirAll(assets, 0o700); err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{
		"rocky-spritesheet-v2.webp",
		"rocky-spritesheet-v11.webp",
	} {
		if err := os.WriteFile(filepath.Join(assets, name), []byte("fixture"), 0o600); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.WriteFile(
		filepath.Join(root, "config.toml"),
		[]byte("[tui]\npet = \"rocky\" # selected\n"),
		0o600,
	); err != nil {
		t.Fatal(err)
	}
	reader := SelectionReader{
		Environment: map[string]string{"CODEX_HOME": root},
		Dimensions: func(string) (int, int, error) {
			return 1536, 1872, nil
		},
	}
	source, err := reader.SelectedSource()
	if err != nil {
		t.Fatal(err)
	}
	if source.ID != "rocky" ||
		filepath.Base(source.AtlasPath) != "rocky-spritesheet-v11.webp" ||
		source.Version != AtlasV1 {
		t.Fatalf("official selection mismatch: %#v", source)
	}

	customDirectory := filepath.Join(root, "pets", "local")
	if err := os.MkdirAll(customDirectory, 0o700); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(
		filepath.Join(root, "config.toml"),
		[]byte("[tui]\npet = 'local'\n"),
		0o600,
	); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(
		filepath.Join(customDirectory, "pet.json"),
		[]byte(`{"id":"local","spriteVersionNumber":2,"spritesheetPath":"atlas.webp"}`),
		0o600,
	); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(
		filepath.Join(customDirectory, "atlas.webp"),
		[]byte("fixture"),
		0o600,
	); err != nil {
		t.Fatal(err)
	}
	reader.Dimensions = func(string) (int, int, error) {
		return 1536, 2288, nil
	}
	source, err = reader.SelectedSource()
	if err != nil {
		t.Fatal(err)
	}
	if source.ID != "local" || source.Version != AtlasV2 {
		t.Fatalf("custom selection mismatch: %#v", source)
	}
}

func TestSelectionRejectsCustomPathTraversal(t *testing.T) {
	root := t.TempDir()
	petDirectory := filepath.Join(root, "pets", "local")
	if err := os.MkdirAll(petDirectory, 0o700); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(
		filepath.Join(root, "config.toml"),
		[]byte("[tui]\npet = \"local\"\n"),
		0o600,
	); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(
		filepath.Join(petDirectory, "pet.json"),
		[]byte(`{"id":"local","spriteVersionNumber":1,"spritesheetPath":"../../outside.webp"}`),
		0o600,
	); err != nil {
		t.Fatal(err)
	}
	reader := SelectionReader{
		Environment: map[string]string{"CODEX_HOME": root},
		Dimensions: func(string) (int, int, error) {
			return 1536, 1872, nil
		},
	}
	if _, err := reader.SelectedSource(); err == nil ||
		!strings.Contains(err.Error(), "path traversal") {
		t.Fatalf("path traversal was not rejected: %v", err)
	}
}

func TestTranscodeComposesBackgroundAndUsesOnlyProvenPixelPeriod(t *testing.T) {
	atlas := image.NewNRGBA(image.Rect(0, 0, 1536, 1872))
	sequences := map[int][]color.NRGBA{
		0: {
			{R: 208, G: 16, B: 16, A: 255},
			{R: 16, G: 208, B: 16, A: 255},
			{R: 16, G: 16, B: 208, A: 255},
			{R: 208, G: 208, B: 16, A: 255},
			{R: 208, G: 16, B: 16, A: 255},
			{R: 16, G: 208, B: 16, A: 255},
		},
		7: {
			{R: 208, G: 16, B: 16, A: 255},
			{R: 16, G: 208, B: 16, A: 255},
			{R: 16, G: 16, B: 208, A: 255},
			{R: 208, G: 208, B: 16, A: 255},
			{R: 208, G: 16, B: 16, A: 255},
			{R: 255, G: 208, B: 16, A: 255},
		},
		6: {
			{R: 208, G: 16, B: 16, A: 255},
			{R: 16, G: 208, B: 16, A: 255},
			{R: 16, G: 16, B: 208, A: 255},
			{R: 208, G: 208, B: 16, A: 255},
			{R: 208, G: 16, B: 208, A: 255},
			{R: 208, G: 16, B: 16, A: 255},
		},
		8: {
			{R: 208, G: 16, B: 16, A: 255},
			{R: 16, G: 208, B: 16, A: 255},
			{R: 16, G: 16, B: 208, A: 255},
			{R: 208, G: 208, B: 16, A: 255},
			{R: 208, G: 16, B: 208, A: 255},
			{R: 16, G: 208, B: 208, A: 255},
		},
		5: {
			{R: 208, G: 16, B: 16, A: 255},
			{R: 16, G: 208, B: 16, A: 255},
			{R: 16, G: 16, B: 208, A: 255},
			{R: 208, G: 208, B: 16, A: 255},
			{R: 208, G: 16, B: 16, A: 255},
			{R: 16, G: 208, B: 16, A: 255},
			{R: 16, G: 16, B: 208, A: 255},
			{R: 208, G: 208, B: 16, A: 255},
		},
	}
	for row, frames := range sequences {
		for column, frameColor := range frames {
			fillCell(atlas, column, row, frameColor)
		}
	}
	bundle, err := transcodeImage("strict-period", AtlasV1, atlas, 0x05080d)
	if err != nil {
		t.Fatal(err)
	}
	idle6 := framePixels(t, bundle, StateIdle, 6)
	idle2 := framePixels(t, bundle, StateIdle, 2)
	if !reflect.DeepEqual(idle6, idle2) {
		t.Fatal("strictly proven ABCDAB period was not expanded")
	}
	working5 := framePixels(t, bundle, StateWorking, 5)
	working1 := framePixels(t, bundle, StateWorking, 1)
	if reflect.DeepEqual(working5, working1) {
		t.Fatal("one-pixel/RGB565 difference was incorrectly normalized")
	}
	if framePixels(t, bundle, StateWaiting, 7)[0] !=
		framePixels(t, bundle, StateWaiting, 1)[0] {
		t.Fatal("unproven tail match did not preserve complete visible sequence")
	}
	if got := framePixels(t, bundle, StateIdle, 0)[96*103+95]; got == 0 {
		t.Fatal("transparent area was not composed onto the display background")
	}
}

func TestCCPTEncodingIsDeterministicAndBounded(t *testing.T) {
	states := map[State][][]uint16{}
	colors := map[State]uint16{
		StateIdle: 0xf800, StateWorking: 0x07e0, StateWaiting: 0xffe0,
		StateReview: 0xf81f, StateFailed: 0x001f,
	}
	for _, state := range States {
		frame := make([]uint16, FrameWidth*FrameHeight)
		for index := range frame {
			frame[index] = colors[state]
		}
		states[state] = make([][]uint16, FramesPerState)
		for index := range states[state] {
			states[state][index] = append([]uint16(nil), frame...)
		}
	}
	first, err := EncodeBundle("fixture", states)
	if err != nil {
		t.Fatal(err)
	}
	second, err := EncodeBundle("fixture", states)
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(first.Data, second.Data) ||
		len(first.Data) > MaximumBundleBytes ||
		!strings.HasPrefix(string(first.Data[:4]), "CCPT") ||
		len(first.ContentDigest) != 64 || len(first.UploadDigest) != 64 ||
		first.ContentDigest == first.UploadDigest {
		t.Fatalf("invalid deterministic bundle: %#v", first)
	}
}

func TestCCPTMatchesCrossPlatformProductFixture(t *testing.T) {
	var fixture struct {
		PetID                 string   `json:"pet_id"`
		StateOrder            []string `json:"state_order"`
		FramesPerState        int      `json:"frames_per_state"`
		FrameWidth            int      `json:"frame_width"`
		FrameHeight           int      `json:"frame_height"`
		PixelRGB565           uint16   `json:"pixel_rgb565"`
		ExpectedLength        int      `json:"expected_length"`
		ExpectedContentSHA256 string   `json:"expected_content_sha256"`
		ExpectedUploadSHA256  string   `json:"expected_upload_sha256"`
	}
	if err := json.Unmarshal(
		testutil.ProductFixture(t, "pet-bundle.json"),
		&fixture,
	); err != nil {
		t.Fatal(err)
	}
	states := map[State][][]uint16{}
	for _, state := range States {
		frame := make(
			[]uint16,
			fixture.FrameWidth*fixture.FrameHeight,
		)
		for index := range frame {
			frame[index] = fixture.PixelRGB565
		}
		states[state] = make([][]uint16, fixture.FramesPerState)
		for index := range states[state] {
			states[state][index] = append([]uint16(nil), frame...)
		}
	}
	bundle, err := EncodeBundle(fixture.PetID, states)
	if err != nil {
		t.Fatal(err)
	}
	if len(bundle.Data) != fixture.ExpectedLength ||
		bundle.ContentDigest != fixture.ExpectedContentSHA256 ||
		bundle.UploadDigest != fixture.ExpectedUploadSHA256 {
		t.Fatalf(
			"cross-platform fixture mismatch: length=%d content=%s upload=%s",
			len(bundle.Data),
			bundle.ContentDigest,
			bundle.UploadDigest,
		)
	}
}

func TestExternalWebPCompatibilityProbe(t *testing.T) {
	atlasPath := os.Getenv("CARDPUTER_PET_TEST_ATLAS")
	if atlasPath == "" {
		t.Skip("CARDPUTER_PET_TEST_ATLAS is not set")
	}
	bundle, err := (Transcoder{}).Transcode(Source{
		ID: "compatibility-probe", AtlasPath: atlasPath, Version: AtlasV1,
	})
	if err != nil {
		t.Fatal(err)
	}
	t.Logf(
		"product-pet-external: length=%d content=%s upload=%s",
		len(bundle.Data),
		bundle.ContentDigest,
		bundle.UploadDigest,
	)
}

func TestSyncResumesChunksAndSkipsMatchingDigest(t *testing.T) {
	bundle := solidBundle(t)
	client := &fakePetDevice{
		status: Status{
			Transaction: Transaction{
				Active: true, ID: "tx", Received: 8192, Expected: len(bundle.Data),
			},
		},
		bundleDigest: bundle.ContentDigest,
	}
	coordinator := NewCoordinator(
		func() (Source, error) {
			return Source{ID: "fixture", AtlasPath: "fixture.webp", Version: AtlasV1}, nil
		},
		func(Source) (Bundle, error) { return bundle, nil },
		client,
	)
	coordinator.SourceDigest = func(Source) (string, error) { return "input-v1", nil }
	result := coordinator.Synchronize(context.Background())
	if result.ErrorCode != "" || result.Digest != bundle.ContentDigest {
		t.Fatalf("sync failed: %#v", result)
	}
	if client.beginCount != 0 ||
		!reflect.DeepEqual(client.offsets, []int{8192, 16384, 24576}) {
		t.Fatalf("resume mismatch: begin=%d offsets=%v", client.beginCount, client.offsets)
	}
	client.offsets = nil
	result = coordinator.Synchronize(context.Background())
	if result.ErrorCode != "" || len(client.offsets) != 0 || client.beginCount != 0 {
		t.Fatalf("matching digest did not skip upload: %#v", result)
	}
}

type fakePetDevice struct {
	status       Status
	bundleDigest string
	beginCount   int
	offsets      []int
}

func (client *fakePetDevice) PetStatus(context.Context) (Status, error) {
	return client.status, nil
}

func (client *fakePetDevice) BeginPetUpload(
	_ context.Context,
	bundle Bundle,
) (Receipt, error) {
	client.beginCount++
	client.status.Transaction = Transaction{
		Active: true, ID: "tx", Expected: len(bundle.Data),
	}
	return Receipt{TransactionID: "tx"}, nil
}

func (client *fakePetDevice) PutPetChunk(
	_ context.Context,
	transactionID string,
	offset int,
	data []byte,
) error {
	client.offsets = append(client.offsets, offset)
	client.status.Transaction = Transaction{
		Active: true, ID: transactionID,
		Received: offset + len(data), Expected: client.status.Transaction.Expected,
	}
	return nil
}

func (client *fakePetDevice) CommitPetUpload(
	_ context.Context,
	_ string,
) (Status, error) {
	client.status.Digest = client.bundleDigest
	client.status.PetID = "fixture"
	client.status.Transaction = Transaction{}
	return client.status, nil
}

func fillCell(atlas *image.NRGBA, column int, row int, value color.NRGBA) {
	left := column * CellWidth
	top := row * CellHeight
	for y := top; y < top+CellHeight; y++ {
		for x := left; x < left+CellWidth; x++ {
			atlas.SetNRGBA(x, y, value)
		}
	}
}

func framePixels(t *testing.T, bundle Bundle, state State, frame int) []uint16 {
	t.Helper()
	pixels, err := DecodeFrame(bundle.Data, state, frame)
	if err != nil {
		t.Fatal(err)
	}
	return pixels
}

func solidBundle(t *testing.T) Bundle {
	t.Helper()
	states := map[State][][]uint16{}
	for _, state := range States {
		frame := make([]uint16, FrameWidth*FrameHeight)
		for index := range frame {
			frame[index] = 0x07e0
		}
		states[state] = make([][]uint16, FramesPerState)
		for index := range states[state] {
			states[state][index] = append([]uint16(nil), frame...)
		}
	}
	bundle, err := EncodeBundle("fixture", states)
	if err != nil {
		t.Fatal(err)
	}
	return bundle
}
