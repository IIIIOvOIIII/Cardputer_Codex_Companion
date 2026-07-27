package pet

import (
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"errors"
)

const (
	SchemaVersion      = 1
	MaximumBundleBytes = 820 * 1024
	headerLength       = 132
	stateTableOffset   = 132
	frameTableOffset   = 172
	payloadOffset      = 812
)

type FrameEncoding uint8

const (
	RawRGB565 FrameEncoding = iota
	RLERGB565
)

type encodedFrame struct {
	encoding FrameEncoding
	data     []byte
}

type Bundle struct {
	PetID         string
	Data          []byte
	ContentDigest string
	UploadDigest  string
}

func EncodeBundle(petID string, frames map[State][][]uint16) (Bundle, error) {
	if !validPetID(petID) {
		return Bundle{}, errors.New("pet ID is invalid")
	}
	encoded := make(map[State][]encodedFrame, len(States))
	for _, state := range States {
		stateFrames := frames[state]
		if len(stateFrames) != FramesPerState {
			return Bundle{}, errors.New("pet state frame count is invalid")
		}
		encoded[state] = make([]encodedFrame, FramesPerState)
		for index, pixels := range stateFrames {
			if len(pixels) != FrameWidth*FrameHeight {
				return Bundle{}, errors.New("pet frame dimensions are invalid")
			}
			encoded[state][index] = encodeFrame(pixels)
		}
	}

	output := make([]byte, payloadOffset)
	copy(output[0:4], []byte("CCPT"))
	binary.LittleEndian.PutUint16(output[4:6], SchemaVersion)
	binary.LittleEndian.PutUint16(output[6:8], headerLength)
	output[12] = byte(len([]byte(petID)))
	binary.LittleEndian.PutUint16(output[16:18], FrameWidth)
	binary.LittleEndian.PutUint16(output[18:20], FrameHeight)
	binary.LittleEndian.PutUint16(output[20:22], 400)
	output[22] = byte(len(States))
	output[23] = FramesPerState
	binary.LittleEndian.PutUint32(output[56:60], stateTableOffset)
	binary.LittleEndian.PutUint32(output[60:64], frameTableOffset)
	binary.LittleEndian.PutUint32(output[64:68], payloadOffset)
	copy(output[68:], []byte(petID))

	frameIndex := 0
	cursor := payloadOffset
	for stateIndex, state := range States {
		stateOffset := stateTableOffset + stateIndex*8
		output[stateOffset] = byte(stateIndex)
		binary.LittleEndian.PutUint16(output[stateOffset+2:stateOffset+4], FramesPerState)
		binary.LittleEndian.PutUint32(
			output[stateOffset+4:stateOffset+8],
			uint32(frameIndex),
		)
		for _, frame := range encoded[state] {
			frameOffset := frameTableOffset + frameIndex*16
			output[frameOffset] = byte(frame.encoding)
			binary.LittleEndian.PutUint32(
				output[frameOffset+4:frameOffset+8],
				uint32(cursor),
			)
			binary.LittleEndian.PutUint32(
				output[frameOffset+8:frameOffset+12],
				uint32(len(frame.data)),
			)
			binary.LittleEndian.PutUint32(
				output[frameOffset+12:frameOffset+16],
				FrameWidth*FrameHeight*2,
			)
			output = append(output, frame.data...)
			cursor += len(frame.data)
			frameIndex++
		}
	}
	if len(output) > MaximumBundleBytes {
		return Bundle{}, errors.New("pet bundle is too large")
	}
	binary.LittleEndian.PutUint32(output[8:12], uint32(len(output)))
	contentInput := append([]byte(nil), output...)
	clear(contentInput[24:56])
	contentSum := sha256.Sum256(contentInput)
	copy(output[24:56], contentSum[:])
	uploadSum := sha256.Sum256(output)
	return Bundle{
		PetID:         petID,
		Data:          output,
		ContentDigest: hex.EncodeToString(contentSum[:]),
		UploadDigest:  hex.EncodeToString(uploadSum[:]),
	}, nil
}

func encodeFrame(pixels []uint16) encodedFrame {
	raw := make([]byte, len(pixels)*2)
	for index, pixel := range pixels {
		binary.LittleEndian.PutUint16(raw[index*2:index*2+2], pixel)
	}
	rle := make([]byte, 0, len(raw))
	for row := 0; row < FrameHeight; row++ {
		start := row * FrameWidth
		type run struct {
			count uint16
			pixel uint16
		}
		runs := make([]run, 0, FrameWidth)
		pixel := pixels[start]
		count := uint16(1)
		for column := 1; column < FrameWidth; column++ {
			next := pixels[start+column]
			if next == pixel && count < ^uint16(0) {
				count++
			} else {
				runs = append(runs, run{count: count, pixel: pixel})
				pixel = next
				count = 1
			}
		}
		runs = append(runs, run{count: count, pixel: pixel})
		rle = binary.LittleEndian.AppendUint16(rle, uint16(len(runs)))
		for _, current := range runs {
			rle = binary.LittleEndian.AppendUint16(rle, current.count)
			rle = binary.LittleEndian.AppendUint16(rle, current.pixel)
		}
	}
	if len(rle) < len(raw) {
		return encodedFrame{encoding: RLERGB565, data: rle}
	}
	return encodedFrame{encoding: RawRGB565, data: raw}
}

func DecodeFrame(bundle []byte, state State, frame int) ([]uint16, error) {
	stateIndex := -1
	for index, candidate := range States {
		if candidate == state {
			stateIndex = index
			break
		}
	}
	if stateIndex < 0 || frame < 0 || frame >= FramesPerState ||
		len(bundle) < payloadOffset {
		return nil, errors.New("pet frame reference is invalid")
	}
	record := frameTableOffset + (stateIndex*FramesPerState+frame)*16
	encoding := FrameEncoding(bundle[record])
	offset := int(binary.LittleEndian.Uint32(bundle[record+4 : record+8]))
	length := int(binary.LittleEndian.Uint32(bundle[record+8 : record+12]))
	if offset < payloadOffset || length < 0 || offset+length > len(bundle) {
		return nil, errors.New("pet frame payload is invalid")
	}
	payload := bundle[offset : offset+length]
	pixels := make([]uint16, 0, FrameWidth*FrameHeight)
	switch encoding {
	case RawRGB565:
		if len(payload) != FrameWidth*FrameHeight*2 {
			return nil, errors.New("raw pet frame length is invalid")
		}
		for index := 0; index < len(payload); index += 2 {
			pixels = append(pixels, binary.LittleEndian.Uint16(payload[index:index+2]))
		}
	case RLERGB565:
		cursor := 0
		for row := 0; row < FrameHeight; row++ {
			if cursor+2 > len(payload) {
				return nil, errors.New("RLE pet frame is truncated")
			}
			runCount := int(binary.LittleEndian.Uint16(payload[cursor : cursor+2]))
			cursor += 2
			rowPixels := 0
			for run := 0; run < runCount; run++ {
				if cursor+4 > len(payload) {
					return nil, errors.New("RLE pet frame is truncated")
				}
				count := int(binary.LittleEndian.Uint16(payload[cursor : cursor+2]))
				pixel := binary.LittleEndian.Uint16(payload[cursor+2 : cursor+4])
				cursor += 4
				if count < 1 || rowPixels+count > FrameWidth {
					return nil, errors.New("RLE pet frame row is invalid")
				}
				for index := 0; index < count; index++ {
					pixels = append(pixels, pixel)
				}
				rowPixels += count
			}
			if rowPixels != FrameWidth {
				return nil, errors.New("RLE pet frame row is invalid")
			}
		}
		if cursor != len(payload) {
			return nil, errors.New("RLE pet frame has trailing data")
		}
	default:
		return nil, errors.New("pet frame encoding is unsupported")
	}
	return pixels, nil
}
