public enum AudioProbeStartGateResult: Equatable, Sendable {
    case waiting
    case started
    case timedOut
}

public struct AudioProbeStartGate: Sendable {
    private var remainingSeconds: Int
    private var started = false

    public init(timeoutSeconds: Int) {
        precondition(timeoutSeconds > 0)
        remainingSeconds = timeoutSeconds
    }

    public mutating func observe(
        receivedFrames: UInt64
    ) -> AudioProbeStartGateResult {
        if started || receivedFrames > 0 {
            started = true
            return .started
        }
        remainingSeconds -= 1
        return remainingSeconds == 0 ? .timedOut : .waiting
    }
}
