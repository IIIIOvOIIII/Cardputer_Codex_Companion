import Foundation

public struct PetSyncCadence: Sendable {
    private var nextAttempt: ContinuousClock.Instant?

    public init() {}

    public func isDue(at now: ContinuousClock.Instant) -> Bool {
        guard let nextAttempt else { return true }
        return now >= nextAttempt
    }

    public mutating func record(
        result: PetSyncResult,
        at now: ContinuousClock.Instant
    ) {
        let interval: Duration = result.errorCode == nil
            ? .seconds(30)
            : .seconds(5)
        nextAttempt = now.advanced(by: interval)
    }
}
