import Darwin
import Foundation
import ProductAudio

private func expectSharedRing(
    _ condition: @autoclosure () -> Bool,
    _ message: String
) {
    if !condition() {
        fatalError(message)
    }
}

func testSharedAudioRingMapsAnonymousFileDescriptor() throws {
    let path = URL(fileURLWithPath: NSTemporaryDirectory())
        .appendingPathComponent("cardputer-audio-\(UUID().uuidString)")
        .path
    let descriptor = open(path, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR)
    guard descriptor >= 0 else { fatalError("open failed") }
    guard unlink(path) == 0 else { fatalError("unlink failed") }
    guard ftruncate(descriptor, off_t(SharedAudioRing.byteCount)) == 0 else {
        fatalError("ftruncate failed")
    }

    let ring = try SharedAudioRing(
        fileDescriptor: descriptor,
        ownsFileDescriptor: true,
        initialize: true
    )
    let samples = (0..<480).map { Float($0) / 480.0 }
    let written = samples.withUnsafeBufferPointer {
        ring.write(samples: $0)
    }
    expectSharedRing(written == samples.count, "short write")
    expectSharedRing(
        ring.availableFrames == samples.count,
        "available frame mismatch"
    )

    var output = [Float](repeating: -1, count: samples.count)
    let read = output.withUnsafeMutableBufferPointer {
        ring.read(into: $0, fillSilence: false)
    }
    expectSharedRing(read == samples.count, "short read")
    expectSharedRing(output == samples, "sample mismatch")
    expectSharedRing(ring.availableFrames == 0, "ring not empty")

    ring.heartbeat(nanoseconds: 123_456)
    expectSharedRing(
        ring.lastHeartbeatNanoseconds == 123_456,
        "heartbeat mismatch"
    )
    ring.reset()
}
