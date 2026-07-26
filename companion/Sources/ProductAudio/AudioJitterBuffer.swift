import Foundation

public enum AudioJitterOutput: Equatable, Sendable {
    case frame(AudioWireFrame)
    case silence(AudioSampleRate)
}

public enum AudioJitterDisposition: Equatable, Sendable {
    case accepted
    case duplicate
    case late
    case outsideWindow
}

public struct AudioJitterPushResult: Equatable, Sendable {
    public let disposition: AudioJitterDisposition
    public let output: AudioJitterOutput?
    public let didReset: Bool
}

public struct AudioJitterBuffer: Sendable {
    private struct Slot: Sendable {
        let sequence: UInt16
        let frame: AudioWireFrame
    }

    public let targetDepthFrames: Int
    public let capacity: Int
    private var slots: [Slot?]
    private var expectedSequence: UInt16?
    private var activeRate: AudioSampleRate?
    private var bufferedCount = 0
    private var primed = false

    public init(targetDepthFrames: Int = 8, capacity: Int = 32) {
        let boundedDepth = min(10, max(6, targetDepthFrames))
        let boundedCapacity = min(64, max(boundedDepth + 2, capacity))
        self.targetDepthFrames = boundedDepth
        self.capacity = boundedCapacity
        slots = [Slot?](repeating: nil, count: boundedCapacity)
    }

    public var targetDepthMilliseconds: Int {
        targetDepthFrames * 10
    }

    public var pendingFrameCount: Int {
        bufferedCount
    }

    public mutating func flush() {
        clearSlots()
        expectedSequence = nil
        activeRate = nil
        primed = false
    }

    public mutating func push(
        _ frame: AudioWireFrame
    ) -> AudioJitterPushResult {
        let explicitReset = !frame.flags.intersection([
            .start, .discontinuity
        ]).isEmpty
        let rateChanged = activeRate != nil && activeRate != frame.sampleRate
        let didReset = explicitReset || rateChanged
        if didReset {
            clearSlots()
            expectedSequence = frame.sequence
            activeRate = frame.sampleRate
            primed = false
        } else if expectedSequence == nil {
            expectedSequence = frame.sequence
            activeRate = frame.sampleRate
        }

        guard let expected = expectedSequence else {
            return AudioJitterPushResult(
                disposition: .outsideWindow,
                output: nil,
                didReset: didReset
            )
        }
        let distance = Int(Int16(bitPattern: frame.sequence &- expected))
        if distance < 0 {
            return AudioJitterPushResult(
                disposition: .late,
                output: nil,
                didReset: didReset
            )
        }
        guard distance < capacity else {
            return AudioJitterPushResult(
                disposition: .outsideWindow,
                output: nil,
                didReset: didReset
            )
        }

        let index = Int(frame.sequence) % capacity
        if let existing = slots[index] {
            return AudioJitterPushResult(
                disposition: existing.sequence == frame.sequence
                    ? .duplicate : .outsideWindow,
                output: nil,
                didReset: didReset
            )
        }
        slots[index] = Slot(sequence: frame.sequence, frame: frame)
        bufferedCount += 1
        if !primed && bufferedCount >= targetDepthFrames {
            primed = true
        }

        return AudioJitterPushResult(
            disposition: .accepted,
            output: primed ? emitExpected() : nil,
            didReset: didReset
        )
    }

    private mutating func emitExpected() -> AudioJitterOutput? {
        guard let expected = expectedSequence,
              let rate = activeRate else {
            return nil
        }
        expectedSequence = expected &+ 1
        let index = Int(expected) % capacity
        if let slot = slots[index], slot.sequence == expected {
            slots[index] = nil
            bufferedCount -= 1
            return .frame(slot.frame)
        }
        return .silence(rate)
    }

    private mutating func clearSlots() {
        for index in slots.indices {
            slots[index] = nil
        }
        bufferedCount = 0
    }
}
