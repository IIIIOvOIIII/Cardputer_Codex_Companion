public struct AudioProbeDeadline: Sendable {
    private let deadlineNanoseconds: UInt64

    public init(
        durationSeconds: Int,
        startedAtNanoseconds: UInt64
    ) {
        precondition(durationSeconds > 0)
        let durationNanoseconds = UInt64(durationSeconds) * 1_000_000_000
        deadlineNanoseconds = startedAtNanoseconds + durationNanoseconds
    }

    public func remainingNanoseconds(at nowNanoseconds: UInt64) -> UInt64 {
        guard nowNanoseconds < deadlineNanoseconds else {
            return 0
        }
        return deadlineNanoseconds - nowNanoseconds
    }
}
