public enum AudioProbeSinkGateResult: Equatable, Sendable {
    case waiting
    case ready
    case timedOut
}

public struct AudioProbeSinkGate: Sendable {
    private var remainingSeconds: Int
    private var ready = false

    public init(timeoutSeconds: Int) {
        precondition(timeoutSeconds > 0)
        remainingSeconds = timeoutSeconds
    }

    public mutating func observe(
        bridgeReady: Bool
    ) -> AudioProbeSinkGateResult {
        if ready || bridgeReady {
            ready = true
            return .ready
        }
        remainingSeconds -= 1
        return remainingSeconds == 0 ? .timedOut : .waiting
    }
}
