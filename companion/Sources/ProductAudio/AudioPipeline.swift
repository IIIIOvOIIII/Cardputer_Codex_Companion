import Dispatch
import Foundation

public protocol AudioSampleSink: AnyObject {
    func write(samples: UnsafeBufferPointer<Float>) -> Int
    func reset()
}

public struct AudioPipelineMetrics: Equatable, Sendable {
    public var receivedFrames: UInt64 = 0
    public var decodedFrames: UInt64 = 0
    public var decodedSourceSamples: UInt64 = 0
    public var sequenceGaps: UInt64 = 0
    public var duplicateFrames: UInt64 = 0
    public var lateFrames: UInt64 = 0
    public var rejectedFrames: UInt64 = 0
    public var decodeErrors: UInt64 = 0
    public var streamResets: UInt64 = 0
    public var rateChanges: UInt64 = 0
    public var outputSamples: UInt64 = 0
    public var sinkShortWrites: UInt64 = 0

    public init() {}
}

public final class AudioPipeline: @unchecked Sendable {
    private let queue = DispatchQueue(
        label: "com.lynx.cardputer.audio.pipeline",
        qos: .userInitiated
    )
    private let sink: AudioSampleSink
    private var jitter: AudioJitterBuffer
    private let resampler = AdaptiveResampler()
    private var activeRate: AudioSampleRate?
    private var metricsStorage = AudioPipelineMetrics()

    public init(
        sink: AudioSampleSink,
        targetDepthFrames: Int = 8,
        maximumBufferedFrames: Int = 32
    ) {
        self.sink = sink
        jitter = AudioJitterBuffer(
            targetDepthFrames: targetDepthFrames,
            capacity: maximumBufferedFrames
        )
    }

    public func receive(_ frame: AudioWireFrame) {
        queue.async { [self] in
            process(frame)
        }
    }

    public func reset() {
        queue.async { [self] in
            jitter.flush()
            resampler.reset()
            sink.reset()
            activeRate = nil
            metricsStorage.streamResets &+= 1
        }
    }

    public func setWatermarkCorrection(partsPerMillion: Double) {
        queue.async { [self] in
            resampler.setWatermarkCorrection(
                partsPerMillion: partsPerMillion
            )
        }
    }

    public var metrics: AudioPipelineMetrics {
        queue.sync { metricsStorage }
    }

    public func waitUntilIdle() {
        queue.sync {}
    }

    private func process(_ frame: AudioWireFrame) {
        metricsStorage.receivedFrames &+= 1
        let previousRate = activeRate
        let result = jitter.push(frame)
        if result.didReset {
            resampler.reset()
            sink.reset()
            metricsStorage.streamResets &+= 1
            if let previousRate, previousRate != frame.sampleRate {
                metricsStorage.rateChanges &+= 1
            }
        }
        activeRate = frame.sampleRate

        switch result.disposition {
        case .accepted:
            break
        case .duplicate:
            metricsStorage.duplicateFrames &+= 1
        case .late:
            metricsStorage.lateFrames &+= 1
        case .outsideWindow:
            metricsStorage.rejectedFrames &+= 1
        }
        guard let output = result.output else {
            return
        }

        let rate: AudioSampleRate
        let source: [Int16]
        switch output {
        case .frame(let ordered):
            rate = ordered.sampleRate
            do {
                source = try IMAADPCM.decode(
                    block: ordered.payload,
                    sampleCount: sampleCount(for: ordered.sampleRate)
                )
                metricsStorage.decodedFrames &+= 1
                metricsStorage.decodedSourceSamples &+=
                    UInt64(source.count)
            } catch {
                metricsStorage.decodeErrors &+= 1
                return
            }
        case .silence(let silenceRate):
            rate = silenceRate
            source = [Int16](
                repeating: 0,
                count: sampleCount(for: silenceRate)
            )
            metricsStorage.sequenceGaps &+= 1
        }

        let converted = resampler.convert(
            source,
            sourceRate: sampleRate(for: rate)
        )
        let written = converted.withUnsafeBufferPointer {
            sink.write(samples: $0)
        }
        metricsStorage.outputSamples &+= UInt64(converted.count)
        if written != converted.count {
            metricsStorage.sinkShortWrites &+= 1
        }
    }

    private func sampleCount(for rate: AudioSampleRate) -> Int {
        switch rate {
        case .hz24000: 240
        case .hz16000: 160
        }
    }

    private func sampleRate(for rate: AudioSampleRate) -> Int {
        switch rate {
        case .hz24000: 24_000
        case .hz16000: 16_000
        }
    }
}
