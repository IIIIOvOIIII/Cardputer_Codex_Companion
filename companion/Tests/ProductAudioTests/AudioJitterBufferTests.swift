import Foundation
import ProductAudio

func makeAudioFrame(
    sequence: UInt16,
    rate: AudioSampleRate = .hz24000,
    flags: AudioFrameFlags = []
) throws -> AudioWireFrame {
    var bytes = [UInt8](repeating: 0, count: 8 + rate.payloadLength)
    bytes[0] = AudioWireFrame.protocolVersion
    bytes[1] = flags.rawValue
    bytes[2] = UInt8(sequence & 0xFF)
    bytes[3] = UInt8(sequence >> 8)
    bytes[4] = rate.rawValue
    bytes[5] = UInt8(rate.durationMilliseconds)
    bytes[6] = UInt8(rate.payloadLength & 0xFF)
    bytes[7] = UInt8(rate.payloadLength >> 8)
    return try AudioWireFrame(data: Data(bytes))
}

func testJitterBufferOrdersFramesAndInsertsOneFrameOfSilence() throws {
    var jitter = AudioJitterBuffer(targetDepthFrames: 6, capacity: 16)
    var output: [AudioJitterOutput] = []
    for sequence: UInt16 in [0, 2, 3, 4, 5, 6, 7] {
        let result = jitter.push(try makeAudioFrame(
            sequence: sequence,
            flags: sequence == 0 ? [.start] : []
        ))
        if let emitted = result.output {
            output.append(emitted)
        }
    }

    assert(jitter.targetDepthMilliseconds == 114)
    assert(output.count == 2)
    guard case .frame(let first) = output[0] else {
        return assertionFailure("first output must be frame")
    }
    assert(first.sequence == 0)
    guard case .silence(let rate) = output[1] else {
        return assertionFailure("missing sequence must produce silence")
    }
    assert(rate == .hz24000)
}

func testJitterBufferIgnoresDuplicateLateAndWrapsSequence() throws {
    var jitter = AudioJitterBuffer(targetDepthFrames: 6, capacity: 16)
    let first = jitter.push(try makeAudioFrame(
        sequence: 65_533,
        flags: [.start]
    ))
    assert(first.disposition == .accepted)
    let duplicate = jitter.push(try makeAudioFrame(
        sequence: 65_533
    ))
    assert(duplicate.disposition == .duplicate)

    var emitted: [UInt16] = []
    for sequence: UInt16 in [65_534, 65_535, 0, 1, 2, 3, 4, 5] {
        let result = jitter.push(try makeAudioFrame(sequence: sequence))
        if case .frame(let frame)? = result.output {
            emitted.append(frame.sequence)
        }
    }
    assert(emitted.prefix(4) == [65_533, 65_534, 65_535, 0])
    let late = jitter.push(try makeAudioFrame(
        sequence: 65_535
    ))
    assert(late.disposition == .late)
}

func testJitterBufferFlushesOnStartDiscontinuityAndRateChange() throws {
    var jitter = AudioJitterBuffer(targetDepthFrames: 8, capacity: 16)
    for sequence: UInt16 in 10...13 {
        _ = jitter.push(try makeAudioFrame(sequence: sequence))
    }
    assert(jitter.pendingFrameCount == 4)

    let discontinuity = jitter.push(try makeAudioFrame(
        sequence: 100,
        flags: [.discontinuity]
    ))
    assert(discontinuity.didReset)
    assert(discontinuity.output == nil)
    assert(jitter.pendingFrameCount == 1)

    let rateChange = jitter.push(try makeAudioFrame(
        sequence: 200,
        rate: .hz16000
    ))
    assert(rateChange.didReset)
    assert(jitter.pendingFrameCount == 1)
    assert(jitter.targetDepthMilliseconds == 224)
}
