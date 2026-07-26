import CAudioBridge
import Darwin

public enum SharedAudioRingError: Error, Equatable {
    case invalidFileDescriptor
    case fileTooSmall
    case mappingFailed
    case invalidHeader
}

public final class SharedAudioRing: AudioSampleSink, @unchecked Sendable {
    public static var byteCount: Int {
        Int(cardputer_audio_ring_size())
    }

    private let fileDescriptor: Int32
    private let ownsFileDescriptor: Bool
    private let mapping: UnsafeMutableRawPointer
    private let ring: UnsafeMutablePointer<CardputerAudioRing>

    public init(
        fileDescriptor: Int32,
        ownsFileDescriptor: Bool,
        initialize: Bool = false
    ) throws {
        guard fileDescriptor >= 0 else {
            throw SharedAudioRingError.invalidFileDescriptor
        }
        var status = stat()
        guard fstat(fileDescriptor, &status) == 0 else {
            throw SharedAudioRingError.invalidFileDescriptor
        }
        guard status.st_size >= off_t(Self.byteCount) else {
            throw SharedAudioRingError.fileTooSmall
        }
        let mapped = mmap(
            nil,
            Self.byteCount,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fileDescriptor,
            0
        )
        guard mapped != MAP_FAILED, let mapped else {
            throw SharedAudioRingError.mappingFailed
        }
        mapping = mapped
        ring = mapped.assumingMemoryBound(to: CardputerAudioRing.self)
        self.fileDescriptor = fileDescriptor
        self.ownsFileDescriptor = ownsFileDescriptor
        if initialize {
            cardputer_audio_ring_initialize(ring)
        } else if !cardputer_audio_ring_is_valid(ring) {
            munmap(mapping, Self.byteCount)
            if ownsFileDescriptor {
                close(fileDescriptor)
            }
            throw SharedAudioRingError.invalidHeader
        }
    }

    deinit {
        munmap(mapping, Self.byteCount)
        if ownsFileDescriptor {
            close(fileDescriptor)
        }
    }

    public var availableFrames: Int {
        Int(cardputer_audio_ring_available(ring))
    }

    public var lastHeartbeatNanoseconds: UInt64 {
        cardputer_audio_ring_last_heartbeat(ring)
    }

    public func write(samples: UnsafeBufferPointer<Float>) -> Int {
        guard let baseAddress = samples.baseAddress else { return 0 }
        return Int(cardputer_audio_ring_write(
            ring,
            baseAddress,
            UInt32(samples.count)
        ))
    }

    public func read(
        into output: UnsafeMutableBufferPointer<Float>,
        fillSilence: Bool
    ) -> Int {
        guard let baseAddress = output.baseAddress else { return 0 }
        let count = UInt32(output.count)
        if fillSilence {
            return Int(cardputer_audio_ring_read_or_silence(
                ring,
                baseAddress,
                count
            ))
        }
        return Int(cardputer_audio_ring_read(ring, baseAddress, count))
    }

    public func reset() {
        cardputer_audio_ring_reset(ring)
    }

    public func heartbeat(nanoseconds: UInt64) {
        cardputer_audio_ring_heartbeat(ring, nanoseconds)
    }
}
