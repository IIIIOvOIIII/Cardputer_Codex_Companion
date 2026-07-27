package pet

import (
	"errors"
	"image"
	"image/draw"
	"os"

	xdraw "golang.org/x/image/draw"
	_ "golang.org/x/image/webp"
)

const (
	FrameWidth        = 96
	FrameHeight       = 104
	FramesPerState    = 8
	DefaultBackground = 0x05080d
)

type State string

const (
	StateIdle    State = "idle"
	StateWorking State = "working"
	StateWaiting State = "waiting"
	StateReview  State = "review"
	StateFailed  State = "failed"
)

var States = []State{
	StateIdle,
	StateWorking,
	StateWaiting,
	StateReview,
	StateFailed,
}

var stateRows = map[State]int{
	StateIdle:    0,
	StateWorking: 7,
	StateWaiting: 6,
	StateReview:  8,
	StateFailed:  5,
}

type Transcoder struct {
	BackgroundRGB888 uint32
}

func (transcoder Transcoder) Transcode(source Source) (Bundle, error) {
	file, err := os.Open(source.AtlasPath)
	if err != nil {
		return Bundle{}, errors.New("open pet atlas")
	}
	defer file.Close()
	atlas, _, err := image.Decode(file)
	if err != nil {
		return Bundle{}, errors.New("decode pet atlas")
	}
	background := transcoder.BackgroundRGB888
	if background == 0 {
		background = DefaultBackground
	}
	return transcodeImage(source.ID, source.Version, atlas, background)
}

func transcodeImage(
	petID string,
	version AtlasVersion,
	atlas image.Image,
	background uint32,
) (Bundle, error) {
	expectedWidth, expectedHeight, err := expectedAtlasDimensions(version)
	if err != nil {
		return Bundle{}, err
	}
	bounds := atlas.Bounds()
	if bounds.Dx() != expectedWidth || bounds.Dy() != expectedHeight {
		return Bundle{}, errors.New("pet atlas dimensions are invalid")
	}
	states := make(map[State][][]uint16, len(States))
	for _, state := range States {
		row := stateRows[state]
		visible := make([][]uint16, 0, FramesPerState)
		for column := 0; column < AtlasColumns; column++ {
			pixels, isVisible := renderFrame(atlas, column, row, background)
			if isVisible {
				visible = append(visible, pixels)
			}
		}
		if len(visible) == 0 {
			return Bundle{}, errors.New("pet atlas state has no visible frame")
		}
		states[state] = expandFrames(visible)
	}
	return EncodeBundle(petID, states)
}

func renderFrame(
	atlas image.Image,
	column int,
	row int,
	background uint32,
) ([]uint16, bool) {
	sourceRect := image.Rect(
		atlas.Bounds().Min.X+column*CellWidth,
		atlas.Bounds().Min.Y+row*CellHeight,
		atlas.Bounds().Min.X+(column+1)*CellWidth,
		atlas.Bounds().Min.Y+(row+1)*CellHeight,
	)
	destination := image.NewNRGBA(image.Rect(0, 0, FrameWidth, FrameHeight))
	draw.Draw(destination, destination.Bounds(), image.Transparent, image.Point{}, draw.Src)
	xdraw.NearestNeighbor.Scale(
		destination,
		destination.Bounds(),
		atlas,
		sourceRect,
		draw.Src,
		nil,
	)
	backgroundRed := uint16((background >> 16) & 0xff)
	backgroundGreen := uint16((background >> 8) & 0xff)
	backgroundBlue := uint16(background & 0xff)
	pixels := make([]uint16, 0, FrameWidth*FrameHeight)
	visible := false
	for y := 0; y < FrameHeight; y++ {
		for x := 0; x < FrameWidth; x++ {
			red16, green16, blue16, alpha16 := destination.At(x, y).RGBA()
			alpha := uint16(alpha16 >> 8)
			if alpha != 0 {
				visible = true
			}
			inverse := 255 - alpha
			red := uint16(red16>>8) + (backgroundRed*inverse+127)/255
			green := uint16(green16>>8) + (backgroundGreen*inverse+127)/255
			blue := uint16(blue16>>8) + (backgroundBlue*inverse+127)/255
			pixels = append(
				pixels,
				((red&0xf8)<<8)|((green&0xfc)<<3)|(blue>>3),
			)
		}
	}
	return pixels, visible
}

func expandFrames(visible [][]uint16) [][]uint16 {
	period := 0
	if len(visible) >= 3 {
		for candidate := 1; candidate <= len(visible)-2; candidate++ {
			proven := true
			for index := candidate; index < len(visible); index++ {
				if !equalPixels(visible[index], visible[index%candidate]) {
					proven = false
					break
				}
			}
			if proven {
				period = candidate
				break
			}
		}
	}
	source := visible
	if period != 0 {
		source = visible[:period]
	}
	expanded := make([][]uint16, FramesPerState)
	for index := range expanded {
		expanded[index] = append([]uint16(nil), source[index%len(source)]...)
	}
	return expanded
}

func equalPixels(left []uint16, right []uint16) bool {
	if len(left) != len(right) {
		return false
	}
	for index := range left {
		if left[index] != right[index] {
			return false
		}
	}
	return true
}
