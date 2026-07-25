import CryptoKit
import Foundation
import ProductContracts

public enum PetFrameEncoding: UInt8, Sendable {
    case rawRGB565 = 0
    case rleRGB565 = 1
}

public struct EncodedPetFrame: Sendable {
    public let encoding: PetFrameEncoding
    public let data: Data
}

public struct PetBundle: Sendable {
    public static let schemaVersion: UInt16 = 1
    public static let maximumBytes = 820 * 1024

    public let petID: String
    public let data: Data
    public let contentDigestHex: String
    public let uploadDigestHex: String
}

public enum PetBundleError: Error {
    case invalidPetID
    case missingState(PetState)
    case invalidFrameCount(PetState)
    case invalidFrameSize
    case tooLarge
}

public enum PetBundleEncoder {
    private static let width = 96
    private static let height = 104
    private static let headerLength = 132
    private static let stateTableOffset = 132
    private static let frameTableOffset = 172
    private static let payloadOffset = 812

    public static func encodeFrame(_ pixels: [UInt16]) -> EncodedPetFrame {
        precondition(pixels.count == width * height)
        var raw = Data(capacity: pixels.count * 2)
        for pixel in pixels {
            raw.appendLittleEndian(pixel)
        }

        var rle = Data()
        for row in 0..<height {
            let start = row * width
            var runs: [(UInt16, UInt16)] = []
            var pixel = pixels[start]
            var count: UInt16 = 1
            for column in 1..<width {
                let next = pixels[start + column]
                if next == pixel && count < UInt16.max {
                    count += 1
                } else {
                    runs.append((count, pixel))
                    pixel = next
                    count = 1
                }
            }
            runs.append((count, pixel))
            rle.appendLittleEndian(UInt16(runs.count))
            for run in runs {
                rle.appendLittleEndian(run.0)
                rle.appendLittleEndian(run.1)
            }
        }
        if rle.count < raw.count {
            return EncodedPetFrame(encoding: .rleRGB565, data: rle)
        }
        return EncodedPetFrame(encoding: .rawRGB565, data: raw)
    }

    public static func encode(
        petID: String,
        frames: [PetState: [[UInt16]]]
    ) throws -> PetBundle {
        guard !petID.isEmpty, petID.utf8.count <= 64 else {
            throw PetBundleError.invalidPetID
        }
        let states = PetState.allCases
        var encoded: [(PetState, [EncodedPetFrame])] = []
        for state in states {
            guard let stateFrames = frames[state] else {
                throw PetBundleError.missingState(state)
            }
            guard stateFrames.count == 8 else {
                throw PetBundleError.invalidFrameCount(state)
            }
            guard stateFrames.allSatisfy({ $0.count == width * height }) else {
                throw PetBundleError.invalidFrameSize
            }
            encoded.append((state, stateFrames.map(encodeFrame)))
        }

        var output = Data(repeating: 0, count: payloadOffset)
        output.replaceSubrange(0..<4, with: Data("CCPT".utf8))
        output.writeLittleEndian(PetBundle.schemaVersion, at: 4)
        output.writeLittleEndian(UInt16(headerLength), at: 6)
        output[12] = UInt8(petID.utf8.count)
        output.writeLittleEndian(UInt16(width), at: 16)
        output.writeLittleEndian(UInt16(height), at: 18)
        output.writeLittleEndian(UInt16(400), at: 20)
        output[22] = UInt8(states.count)
        output[23] = 8
        output.writeLittleEndian(UInt32(stateTableOffset), at: 56)
        output.writeLittleEndian(UInt32(frameTableOffset), at: 60)
        output.writeLittleEndian(UInt32(payloadOffset), at: 64)
        output.replaceSubrange(
            68..<(68 + petID.utf8.count),
            with: Data(petID.utf8)
        )

        var frameIndex = 0
        var payloadCursor = payloadOffset
        for (stateIndex, pair) in encoded.enumerated() {
            let stateOffset = stateTableOffset + stateIndex * 8
            output[stateOffset] = UInt8(stateIndex)
            output.writeLittleEndian(UInt16(8), at: stateOffset + 2)
            output.writeLittleEndian(UInt32(frameIndex), at: stateOffset + 4)
            for frame in pair.1 {
                let frameOffset = frameTableOffset + frameIndex * 16
                output[frameOffset] = frame.encoding.rawValue
                output.writeLittleEndian(UInt32(payloadCursor), at: frameOffset + 4)
                output.writeLittleEndian(UInt32(frame.data.count), at: frameOffset + 8)
                output.writeLittleEndian(UInt32(width * height * 2), at: frameOffset + 12)
                output.append(frame.data)
                payloadCursor += frame.data.count
                frameIndex += 1
            }
        }
        guard output.count <= PetBundle.maximumBytes else {
            throw PetBundleError.tooLarge
        }
        output.writeLittleEndian(UInt32(output.count), at: 8)
        var contentInput = output
        contentInput.replaceSubrange(24..<56, with: Data(repeating: 0, count: 32))
        let contentDigest = Data(SHA256.hash(data: contentInput))
        output.replaceSubrange(24..<56, with: contentDigest)
        let uploadDigest = Data(SHA256.hash(data: output))
        return PetBundle(
            petID: petID,
            data: output,
            contentDigestHex: contentDigest.hex,
            uploadDigestHex: uploadDigest.hex
        )
    }
}

private extension Data {
    mutating func appendLittleEndian<T: FixedWidthInteger>(_ value: T) {
        var little = value.littleEndian
        Swift.withUnsafeBytes(of: &little) { append(contentsOf: $0) }
    }

    mutating func writeLittleEndian<T: FixedWidthInteger>(
        _ value: T,
        at offset: Int
    ) {
        var bytes = Data()
        bytes.appendLittleEndian(value)
        replaceSubrange(offset..<(offset + bytes.count), with: bytes)
    }

    var hex: String {
        map { String(format: "%02x", $0) }.joined()
    }
}
