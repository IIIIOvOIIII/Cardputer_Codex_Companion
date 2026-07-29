public enum ProductGATTRecoveryPhase: Equatable, Sendable {
    case idle
    case scanning
    case connecting
    case discovering
    case subscribing
    case ready
    case stopped
}

public enum ProductGATTRecoveryEvent: Equatable, Sendable {
    case start
    case bluetoothPoweredOn
    case bluetoothUnavailable
    case candidateSelected
    case connected
    case subscribing
    case ready
    case failed
    case timedOut
    case disconnected(intentional: Bool)
    case stop
}

public struct ProductGATTRecoveryDecision: Equatable, Sendable {
    public let phase: ProductGATTRecoveryPhase
    public let generation: UInt64
    public let cancelPeripheral: Bool
    public let retryAfterMilliseconds: Int?
    public let watchdogMilliseconds: Int?
}

public struct ProductGATTRecoveryPolicy: Sendable {
    public static let watchdogMilliseconds = 8_000
    private static let retryMilliseconds = [500, 1_000, 2_000, 5_000]

    public private(set) var phase: ProductGATTRecoveryPhase = .idle
    public private(set) var generation: UInt64 = 0
    private var failureCount = 0

    public init() {}

    public mutating func apply(
        _ event: ProductGATTRecoveryEvent
    ) -> ProductGATTRecoveryDecision {
        if phase == .stopped, event != .start {
            return decision()
        }

        switch event {
        case .start, .bluetoothPoweredOn:
            failureCount = 0
            phase = .scanning
            generation &+= 1
            return decision(retryAfterMilliseconds: 0)
        case .bluetoothUnavailable:
            phase = .idle
            generation &+= 1
            return decision()
        case .candidateSelected:
            phase = .connecting
            generation &+= 1
            return decision(
                watchdogMilliseconds: Self.watchdogMilliseconds
            )
        case .connected:
            phase = .discovering
            generation &+= 1
            return decision(
                watchdogMilliseconds: Self.watchdogMilliseconds
            )
        case .subscribing:
            phase = .subscribing
            generation &+= 1
            return decision(
                watchdogMilliseconds: Self.watchdogMilliseconds
            )
        case .ready:
            phase = .ready
            failureCount = 0
            generation &+= 1
            return decision()
        case .failed, .timedOut, .disconnected(intentional: false):
            phase = .scanning
            generation &+= 1
            let index = min(
                failureCount,
                Self.retryMilliseconds.count - 1
            )
            failureCount &+= 1
            return decision(
                cancelPeripheral: true,
                retryAfterMilliseconds: Self.retryMilliseconds[index]
            )
        case .disconnected(intentional: true), .stop:
            phase = .stopped
            generation &+= 1
            return decision(cancelPeripheral: true)
        }
    }

    private func decision(
        cancelPeripheral: Bool = false,
        retryAfterMilliseconds: Int? = nil,
        watchdogMilliseconds: Int? = nil
    ) -> ProductGATTRecoveryDecision {
        ProductGATTRecoveryDecision(
            phase: phase,
            generation: generation,
            cancelPeripheral: cancelPeripheral,
            retryAfterMilliseconds: retryAfterMilliseconds,
            watchdogMilliseconds: watchdogMilliseconds
        )
    }
}
