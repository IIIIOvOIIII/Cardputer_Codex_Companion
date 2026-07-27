import Foundation
import ProductAudio

func testParsesBothCommittedPacketSizesAndFields() throws {
    let fixture = try AudioFixture.load()

    let frame24 = try AudioWireFrame(data: fixture.packet(named: "24khz"))
    assert(frame24.sequence == 65_535)
    assert(frame24.sampleRate == .hz24000)
    assert(frame24.flags == [.start])
    assert(frame24.payload.count == 232)

    let frame16 = try AudioWireFrame(data: fixture.packet(named: "16khz"))
    assert(frame16.sequence == 0)
    assert(frame16.sampleRate == .hz16000)
    assert(frame16.flags == [.degradedRate])
    assert(frame16.payload.count == 228)
}

func testRejectsMalformedFieldsBeforeCodecUse() throws {
    let fixture = try AudioFixture.load()

    expectThrows(AudioProtocolError.payloadLength) {
        _ = try AudioWireFrame(
            data: fixture.invalidPacket(named: "malformed_length")
        )
    }
    expectThrows(AudioProtocolError.unsupportedVersion) {
        _ = try AudioWireFrame(
            data: fixture.invalidPacket(named: "unsupported_version")
        )
    }
}

func testParsesTwoFramesFromOneGattNotification() throws {
    let fixture = try AudioFixture.load()
    var notification = try fixture.packet(named: "24khz")
    notification.append(try fixture.packet(named: "16khz"))

    let frames = try AudioWireFrame.decodeBatch(notification)

    assert(frames.count == 2)
    assert(frames[0].sequence == 65_535)
    assert(frames[1].sequence == 0)
    assert(frames[0].sampleRate == .hz24000)
    assert(frames[1].sampleRate == .hz16000)
}

func testParsesMaximumAdaptiveGattBatches() throws {
    let fixture = try AudioFixture.load()
    let packet24 = try fixture.packet(named: "24khz")
    let packet16 = try fixture.packet(named: "16khz")
    let notification24 = (0..<3).reduce(into: Data()) { value, _ in
        value.append(packet24)
    }
    let notification16 = (0..<5).reduce(into: Data()) { value, _ in
        value.append(packet16)
    }

    let frames24 = try AudioWireFrame.decodeBatch(notification24)
    let frames16 = try AudioWireFrame.decodeBatch(notification16)
    assert(frames24.count == 3)
    assert(frames16.count == 5)
}

func expectThrows<Expected: Error & Equatable>(
    _ expected: Expected,
    operation: () throws -> Void
) {
    do {
        try operation()
        assertionFailure("expected \(expected)")
    } catch let error as Expected {
        assert(error == expected, "expected \(expected), got \(error)")
    } catch {
        assertionFailure("expected \(expected), got \(error)")
    }
}

struct AudioFixture: Decodable {
    struct Packet: Decodable {
        let name: String
        let packetHex: String

        enum CodingKeys: String, CodingKey {
            case name
            case packetHex = "packet_hex"
        }
    }

    struct CodecVector: Decodable {
        let name: String
        let sampleCount: Int
        let blockHex: String
        let decodedSamples: [Int16]

        enum CodingKeys: String, CodingKey {
            case name
            case sampleCount = "sample_count"
            case blockHex = "block_hex"
            case decodedSamples = "decoded_samples"
        }
    }

    let packets: [Packet]
    let invalidPackets: [Packet]
    let codecVectors: [CodecVector]

    enum CodingKeys: String, CodingKey {
        case packets
        case invalidPackets = "invalid_packets"
        case codecVectors = "codec_vectors"
    }

    static func load() throws -> AudioFixture {
        let testFile = URL(fileURLWithPath: #filePath)
        let repositoryRoot = testFile
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
        let fixtureURL = repositoryRoot
            .appendingPathComponent("protocol/audio-v1/fixtures/audio-v1.json")
        return try JSONDecoder().decode(
            AudioFixture.self,
            from: Data(contentsOf: fixtureURL)
        )
    }

    func packet(named name: String) throws -> Data {
        try Self.data(from: packets.first { $0.name == name }!.packetHex)
    }

    func invalidPacket(named name: String) throws -> Data {
        try Self.data(from: invalidPackets.first { $0.name == name }!.packetHex)
    }

    static func data(from hex: String) throws -> Data {
        guard hex.count.isMultiple(of: 2) else {
            throw CocoaError(.fileReadCorruptFile)
        }
        var data = Data()
        var index = hex.startIndex
        while index < hex.endIndex {
            let next = hex.index(index, offsetBy: 2)
            guard let byte = UInt8(hex[index..<next], radix: 16) else {
                throw CocoaError(.fileReadCorruptFile)
            }
            data.append(byte)
            index = next
        }
        return data
    }
}
