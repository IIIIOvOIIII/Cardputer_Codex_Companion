import Foundation
import ProductAudio

final class AudioBridgeCoordinator: AudioSampleSink, @unchecked Sendable {
    private let lock = NSLock()
    private var connection: AudioDriverConnection?
    var readinessHandler: (@Sendable (Bool) -> Void)?

    var isReady: Bool {
        lock.lock()
        defer { lock.unlock() }
        return connection?.isReady == true
    }

    @discardableResult
    func reconnectIfNeeded() -> Bool {
        lock.lock()
        if connection?.isReady == true {
            lock.unlock()
            return true
        }
        lock.unlock()

        do {
            let candidate = AudioDriverConnection(
                transport: try XPCDriverTransport()
            )
            try candidate.start()
            candidate.readinessHandler = { [weak self] ready in
                if !ready {
                    self?.driverBecameUnavailable()
                }
            }
            lock.lock()
            connection = candidate
            let handler = readinessHandler
            lock.unlock()
            handler?(true)
            return true
        } catch {
            return false
        }
    }

    func stop() {
        lock.lock()
        let current = connection
        connection = nil
        let handler = readinessHandler
        lock.unlock()
        current?.readinessHandler = nil
        current?.stop()
        if current != nil {
            handler?(false)
        }
    }

    func write(samples: UnsafeBufferPointer<Float>) -> Int {
        lock.lock()
        let current = connection
        lock.unlock()
        return current?.write(samples: samples) ?? 0
    }

    func reset() {
        lock.lock()
        let current = connection
        lock.unlock()
        current?.reset()
    }

    private func driverBecameUnavailable() {
        lock.lock()
        connection = nil
        let handler = readinessHandler
        lock.unlock()
        handler?(false)
    }
}

