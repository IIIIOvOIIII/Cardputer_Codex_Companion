import Foundation

public enum AudioSampleRate: UInt8, Sendable {
    case hz24000 = 1
    case hz16000 = 2

    public var payloadLength: Int {
        switch self {
        case .hz24000: 232
        case .hz16000: 228
        }
    }

    public var durationMilliseconds: Int {
        switch self {
        case .hz24000: 19
        case .hz16000: 28
        }
    }

    public var sampleCount: Int {
        switch self {
        case .hz24000: 456
        case .hz16000: 448
        }
    }
}

public struct AudioFrameFlags: OptionSet, Equatable, Sendable {
    public let rawValue: UInt8

    public init(rawValue: UInt8) {
        self.rawValue = rawValue
    }

    public static let start = AudioFrameFlags(rawValue: 1 << 0)
    public static let discontinuity = AudioFrameFlags(rawValue: 1 << 1)
    public static let degradedRate = AudioFrameFlags(rawValue: 1 << 2)
    public static let allowed: AudioFrameFlags = [
        .start, .discontinuity, .degradedRate
    ]
}

public enum AudioProtocolError: Error, Equatable, Sendable {
    case packetTooShort
    case unsupportedVersion
    case invalidFlags
    case invalidRate
    case invalidDuration
    case payloadLength
}

public struct AudioWireFrame: Equatable, Sendable {
    public static let protocolVersion: UInt8 = 1
    public static let headerLength = 8
    public let flags: AudioFrameFlags
    public let sequence: UInt16
    public let sampleRate: AudioSampleRate
    public let payload: Data

    public init(data: Data) throws {
        let bytes = [UInt8](data)
        guard bytes.count >= Self.headerLength else {
            throw AudioProtocolError.packetTooShort
        }
        guard bytes[0] == Self.protocolVersion else {
            throw AudioProtocolError.unsupportedVersion
        }
        let rawFlags = bytes[1]
        guard rawFlags & ~AudioFrameFlags.allowed.rawValue == 0 else {
            throw AudioProtocolError.invalidFlags
        }
        guard let rate = AudioSampleRate(rawValue: bytes[4]) else {
            throw AudioProtocolError.invalidRate
        }
        guard bytes[5] == rate.durationMilliseconds else {
            throw AudioProtocolError.invalidDuration
        }
        let declaredLength = Int(bytes[6]) | (Int(bytes[7]) << 8)
        guard declaredLength == rate.payloadLength,
              bytes.count == Self.headerLength + declaredLength else {
            throw AudioProtocolError.payloadLength
        }

        flags = AudioFrameFlags(rawValue: rawFlags)
        sequence = UInt16(bytes[2]) | (UInt16(bytes[3]) << 8)
        sampleRate = rate
        payload = Data(bytes[Self.headerLength...])
    }

    public static func decodeBatch(_ data: Data) throws -> [AudioWireFrame] {
        guard !data.isEmpty else {
            throw AudioProtocolError.packetTooShort
        }
        var frames: [AudioWireFrame] = []
        var offset = 0
        while offset < data.count {
            guard data.count - offset >= Self.headerLength else {
                throw AudioProtocolError.packetTooShort
            }
            let declaredLength =
                Int(data[offset + 6]) | (Int(data[offset + 7]) << 8)
            let packetLength = Self.headerLength + declaredLength
            guard packetLength <= data.count - offset else {
                throw AudioProtocolError.payloadLength
            }
            frames.append(try AudioWireFrame(
                data: data.subdata(in: offset..<(offset + packetLength))
            ))
            offset += packetLength
        }
        return frames
    }
}
