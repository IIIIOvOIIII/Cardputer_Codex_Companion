import CAudioBridge
import Darwin
import Dispatch
import Foundation

public enum AudioDriverOperation: Equatable, Sendable {
    case hello(version: UInt32)
    case claim
    case heartbeat
    case release
}

public protocol AudioDriverTransport: AnyObject {
    func hello(version: UInt32) throws
    func claim() throws -> Int32
    func heartbeat() throws
    func release()
}

public enum AudioDriverConnectionError: Error, Equatable {
    case unavailable
    case rejected
}

public final class XPCDriverTransport: AudioDriverTransport {
    private let client: OpaquePointer

    public init() throws {
        guard let client = cardputerAudioXPCClientCreate() else {
            throw AudioDriverConnectionError.unavailable
        }
        self.client = client
    }

    deinit {
        cardputerAudioXPCClientDestroy(client)
    }

    public func hello(version: UInt32) throws {
        guard cardputerAudioXPCClientHello(client, version: version) else {
            throw AudioDriverConnectionError.rejected
        }
    }

    public func claim() throws -> Int32 {
        let descriptor = cardputerAudioXPCClientClaim(client, version: 1)
        guard descriptor >= 0 else {
            throw AudioDriverConnectionError.rejected
        }
        return descriptor
    }

    public func heartbeat() throws {
        guard cardputerAudioXPCClientHeartbeat(client) else {
            throw AudioDriverConnectionError.rejected
        }
    }

    public func release() {
        cardputerAudioXPCClientRelease(client)
    }
}

public final class AudioDriverConnection: AudioSampleSink, @unchecked Sendable {
    private let transport: AudioDriverTransport
    private let heartbeatInterval: DispatchTimeInterval
    private let queue = DispatchQueue(
        label: "com.lynx.cardputer.audio.driver-heartbeat"
    )
    private let lock = NSLock()
    private var ring: SharedAudioRing?
    private var timer: DispatchSourceTimer?
    private var ready = false

    public init(
        transport: AudioDriverTransport,
        heartbeatInterval: DispatchTimeInterval = .milliseconds(500)
    ) {
        self.transport = transport
        self.heartbeatInterval = heartbeatInterval
    }

    deinit {
        stop()
    }

    public var isReady: Bool {
        lock.lock()
        defer { lock.unlock() }
        return ready
    }

    public func start() throws {
        lock.lock()
        if ready {
            lock.unlock()
            return
        }
        lock.unlock()

        var descriptor: Int32 = -1
        do {
            try transport.hello(version: 1)
            descriptor = try transport.claim()
            let mapped = try SharedAudioRing(
                fileDescriptor: descriptor,
                ownsFileDescriptor: true
            )
            descriptor = -1
            try transport.heartbeat()

            lock.lock()
            ring = mapped
            ready = true
            let heartbeat = DispatchSource.makeTimerSource(queue: queue)
            heartbeat.schedule(
                deadline: .now() + heartbeatInterval,
                repeating: heartbeatInterval
            )
            heartbeat.setEventHandler { [weak self] in
                self?.sendHeartbeat()
            }
            timer = heartbeat
            lock.unlock()
            heartbeat.resume()
        } catch {
            if descriptor >= 0 {
                close(descriptor)
            }
            transport.release()
            throw error
        }
    }

    public func stop() {
        lock.lock()
        let currentTimer = timer
        timer = nil
        let currentRing = ring
        ring = nil
        let wasReady = ready
        ready = false
        lock.unlock()
        currentTimer?.cancel()
        currentRing?.reset()
        if wasReady {
            transport.release()
        }
    }

    public func write(samples: UnsafeBufferPointer<Float>) -> Int {
        lock.lock()
        let current = ready ? ring : nil
        lock.unlock()
        return current?.write(samples: samples) ?? 0
    }

    public func reset() {
        lock.lock()
        let current = ring
        lock.unlock()
        current?.reset()
    }

    private func sendHeartbeat() {
        do {
            try transport.heartbeat()
            lock.lock()
            ring?.heartbeat(
                nanoseconds: DispatchTime.now().uptimeNanoseconds
            )
            lock.unlock()
        } catch {
            stop()
        }
    }
}
