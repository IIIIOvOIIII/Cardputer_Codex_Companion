import Foundation

public enum AudioBridgeReceiverAction: Equatable, Sendable {
    case none
    case restartReceiverWithAudio
    case suspendAudio
    case resumeAudio
}

public final class AudioBridgeRecoveryPolicy: @unchecked Sendable {
    private let lock = NSLock()
    private var bridgeReady = false
    private var receiverStarted = false
    private var receiverAudioEnabled = false

    public init() {}

    public func receiverWillStart() -> Bool {
        lock.lock()
        defer { lock.unlock() }
        receiverStarted = true
        receiverAudioEnabled = bridgeReady
        return receiverAudioEnabled
    }

    public func bridgeReadinessChanged(
        _ ready: Bool
    ) -> AudioBridgeReceiverAction {
        lock.lock()
        defer { lock.unlock() }
        bridgeReady = ready
        guard receiverStarted else { return .none }
        if ready {
            if !receiverAudioEnabled {
                receiverAudioEnabled = true
                return .restartReceiverWithAudio
            }
            return .resumeAudio
        }
        return receiverAudioEnabled ? .suspendAudio : .none
    }
}
