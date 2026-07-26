import Dispatch
import Foundation
import ProductAudio
import ProductUnicode

public enum ProductGATTDeviceIdentity {
    public static let currentName = "Cardputer Codex"
    public static let legacyName = "Cardputer Companion"

    public static func accepts(
        peripheralName: String?,
        advertisedName: String?
    ) -> Bool {
        peripheralName == currentName ||
            advertisedName == currentName ||
            peripheralName == legacyName ||
            advertisedName == legacyName
    }
}

public struct ProductGATTAudioMetrics: Equatable, Sendable {
    public var receivedFrames: UInt64 = 0
    public var parseErrors: UInt64 = 0
    public var reconnects: UInt64 = 0
    public var maximumGapMilliseconds: UInt64 = 0
    public var sampleRateHertz: Int = 0
    public var pipeline = AudioPipelineMetrics()

    public init() {}
}

private struct PartialText {
    let count: Int
    var fragments: [Int: Data]
}

public final class ProductGATTReceiver: @unchecked Sendable {
    private let injector: UnicodeInjector
    private let metricsLock = NSLock()
    private lazy var connection = ProductGATTConnection(delegate: self)
    private var partial: [UInt32: PartialText] = [:]
    private var audioPipeline: AudioPipeline?
    private var metricsStorage = ProductGATTAudioMetrics()
    private var lastAudioFrameNanoseconds: UInt64?

    public init(injector: UnicodeInjector = UnicodeInjector()) {
        self.injector = injector
    }

    public func start(audioSink: AudioSampleSink? = nil) {
        if let audioSink {
            audioPipeline = AudioPipeline(sink: audioSink)
        }
        connection.start(audioEnabled: audioSink != nil)
    }

    public func stop() {
        connection.stop()
    }

    public var audioMetrics: ProductGATTAudioMetrics {
        metricsLock.lock()
        var snapshot = metricsStorage
        metricsLock.unlock()
        if let audioPipeline {
            snapshot.pipeline = audioPipeline.metrics
        }
        return snapshot
    }

    private func receiveUnicode(_ data: Data) {
        guard data.count >= 10,
              data[0] == 1,
              data[1] == 1 else {
            return
        }
        let operationID =
            UInt32(data[2]) << 24 |
            UInt32(data[3]) << 16 |
            UInt32(data[4]) << 8 |
            UInt32(data[5])
        let index = Int(data[6])
        let count = Int(data[7])
        let length = Int(data[8]) << 8 | Int(data[9])
        guard count > 0, index < count, length == data.count - 10 else {
            return
        }
        var value = partial[operationID] ?? PartialText(
            count: count,
            fragments: [:]
        )
        guard value.count == count, value.fragments[index] == nil else {
            partial.removeValue(forKey: operationID)
            return
        }
        value.fragments[index] = data.subdata(in: 10..<data.count)
        partial[operationID] = value
        guard value.fragments.count == count else { return }
        var complete = Data()
        for fragmentIndex in 0..<count {
            guard let fragment = value.fragments[fragmentIndex] else { return }
            complete.append(fragment)
        }
        partial.removeValue(forKey: operationID)
        guard complete.count <= 1024,
              let text = String(data: complete, encoding: .utf8) else {
            return
        }
        DispatchQueue.main.async { [injector] in
            try? injector.inject(text)
        }
    }

    private func receiveAudio(_ frame: AudioWireFrame) {
        let now = DispatchTime.now().uptimeNanoseconds
        metricsLock.lock()
        metricsStorage.receivedFrames &+= 1
        metricsStorage.sampleRateHertz =
            frame.sampleRate == .hz24000 ? 24_000 : 16_000
        let streamReset = !frame.flags.intersection([
            .start, .discontinuity
        ]).isEmpty
        if !streamReset,
           let previous = lastAudioFrameNanoseconds,
           now >= previous {
            let gap = (now - previous) / 1_000_000
            metricsStorage.maximumGapMilliseconds = max(
                metricsStorage.maximumGapMilliseconds,
                gap
            )
        }
        lastAudioFrameNanoseconds = now
        metricsLock.unlock()
        audioPipeline?.receive(frame)
    }
}

extension ProductGATTReceiver: ProductGATTConnectionDelegate {
    func productGATT(
        _ connection: ProductGATTConnection,
        didReceive characteristic: ProductGATTCharacteristic,
        value: Data
    ) {
        switch ProductGATTValueRouter.route(
            characteristic: characteristic,
            value: value
        ) {
        case .unicode:
            receiveUnicode(value)
        case .audio(let frame):
            receiveAudio(frame)
        case .invalidAudio:
            metricsLock.lock()
            metricsStorage.parseErrors &+= 1
            metricsLock.unlock()
        case .audioStatus, .ignored:
            break
        }
    }

    func productGATTDidDisconnect(
        _ connection: ProductGATTConnection,
        intentional: Bool
    ) {
        partial.removeAll(keepingCapacity: true)
        audioPipeline?.reset()
        metricsLock.lock()
        lastAudioFrameNanoseconds = nil
        if !intentional {
            metricsStorage.reconnects &+= 1
        }
        metricsLock.unlock()
    }
}
