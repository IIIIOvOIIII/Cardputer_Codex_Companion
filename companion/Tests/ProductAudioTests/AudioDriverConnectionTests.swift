import Darwin
import Foundation
import ProductAudio

private enum FakeDriverError: Error {
    case rejected
}

private final class ReadinessRecorder: @unchecked Sendable {
    private let lock = NSLock()
    private var valuesStorage: [Bool] = []

    func append(_ value: Bool) {
        lock.lock()
        valuesStorage.append(value)
        lock.unlock()
    }

    var values: [Bool] {
        lock.lock()
        defer { lock.unlock() }
        return valuesStorage
    }
}

private final class FakeDriverTransport: AudioDriverTransport {
    var operations: [AudioDriverOperation] = []
    var failHeartbeat = false
    private let descriptor: Int32
    let observer: SharedAudioRing

    init() throws {
        let path = URL(fileURLWithPath: NSTemporaryDirectory())
            .appendingPathComponent("cardputer-xpc-\(UUID().uuidString)")
            .path
        descriptor = open(
            path,
            O_RDWR | O_CREAT | O_EXCL,
            S_IRUSR | S_IWUSR
        )
        guard descriptor >= 0 else { throw FakeDriverError.rejected }
        guard unlink(path) == 0 else { throw FakeDriverError.rejected }
        guard ftruncate(descriptor, off_t(SharedAudioRing.byteCount)) == 0 else {
            throw FakeDriverError.rejected
        }
        observer = try SharedAudioRing(
            fileDescriptor: dup(descriptor),
            ownsFileDescriptor: true,
            initialize: true
        )
    }

    deinit {
        close(descriptor)
    }

    func hello(version: UInt32) throws {
        operations.append(.hello(version: version))
    }

    func claim() throws -> Int32 {
        operations.append(.claim)
        return dup(descriptor)
    }

    func heartbeat() throws {
        operations.append(.heartbeat)
        if failHeartbeat {
            throw FakeDriverError.rejected
        }
    }

    func release() {
        operations.append(.release)
    }
}

func testAudioDriverConnectionRequiresMappedRingAndHeartbeat() throws {
    let transport = try FakeDriverTransport()
    let connection = AudioDriverConnection(
        transport: transport,
        heartbeatInterval: .milliseconds(500)
    )
    let readiness = ReadinessRecorder()
    connection.readinessHandler = { readiness.append($0) }
    try connection.start()
    assert(connection.isReady)
    assert(readiness.values == [true])
    assert(
        transport.operations.prefix(3) ==
        [.hello(version: 1), .claim, .heartbeat]
    )

    let samples = [Float](repeating: 0.25, count: 480)
    assert(samples.withUnsafeBufferPointer {
        connection.write(samples: $0)
    } == 480)
    assert(transport.observer.availableFrames == 480)

    connection.reset()
    assert(transport.observer.availableFrames == 0)
    connection.stop()
    assert(!connection.isReady)
    assert(readiness.values == [true, false])
    assert(transport.operations.last == .release)

    let rejected = try FakeDriverTransport()
    rejected.failHeartbeat = true
    let rejectedConnection = AudioDriverConnection(
        transport: rejected,
        heartbeatInterval: .milliseconds(500)
    )
    do {
        try rejectedConnection.start()
        assertionFailure("heartbeat failure was accepted")
    } catch {
        assert(!rejectedConnection.isReady)
        assert(rejected.operations.last == .release)
    }
}
