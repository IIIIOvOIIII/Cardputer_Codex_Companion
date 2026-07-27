import ProductAudio

private final class CollectingAudioSink: AudioSampleSink {
    private(set) var writes: [[Float]] = []
    private(set) var resetCount = 0

    func write(samples: UnsafeBufferPointer<Float>) -> Int {
        writes.append(Array(samples))
        return samples.count
    }

    func reset() {
        resetCount += 1
        writes.removeAll(keepingCapacity: true)
    }
}

func testPipelineDecodesGapsResamplesAndFlushesRateChanges() throws {
    let sink = CollectingAudioSink()
    let pipeline = AudioPipeline(
        sink: sink,
        targetDepthFrames: 6,
        maximumBufferedFrames: 16
    )

    for sequence: UInt16 in [0, 2, 3, 4, 5, 6, 7] {
        pipeline.receive(try makeAudioFrame(
            sequence: sequence,
            flags: sequence == 0 ? [.start] : [],
            predictor: sequence == 0 ? 1_000 : 0
        ))
    }
    pipeline.waitUntilIdle()

    var metrics = pipeline.metrics
    assert(metrics.receivedFrames == 7)
    assert(metrics.decodedFrames == 1)
    assert(metrics.decodedSourceSamples == 456)
    assert(metrics.sequenceGaps == 1)
    assert(metrics.outputSamples == 1_824)
    assert(metrics.sinkShortWrites == 0)
    assert(metrics.signalValueCount == 456)
    assert(metrics.signalNonzeroValues > 0)
    assert(metrics.signalPeak > 0)
    assert(metrics.signalSquareSum > 0)
    assert(sink.writes.map(\.count) == [912, 912])
    assert(sink.writes[1].allSatisfy {
        $0.bitPattern == Float.zero.bitPattern
    })

    for sequence: UInt16 in 100...105 {
        pipeline.receive(try makeAudioFrame(
            sequence: sequence,
            rate: .hz16000,
            flags: sequence == 100 ? [.discontinuity, .degradedRate]
                                   : [.degradedRate],
            predictor: sequence == 100 ? -1_000 : 0
        ))
    }
    pipeline.waitUntilIdle()
    metrics = pipeline.metrics
    assert(metrics.rateChanges == 1)
    assert(metrics.streamResets == 2)
    assert(metrics.decodedFrames == 2)
    assert(metrics.decodedSourceSamples == 904)
    assert(metrics.signalValueCount == 904)
    assert(metrics.signalNonzeroValues > 0)
    assert(metrics.signalPeak > 0)
    assert(metrics.signalSquareSum > 0)
    assert(sink.resetCount == 2)
    assert(sink.writes.map(\.count) == [1_344])

    let metricFieldNames = Set(Mirror(reflecting: metrics).children.compactMap {
        $0.label
    })
    assert(!metricFieldNames.contains("audio"))
    assert(!metricFieldNames.contains("samples"))
    assert(!metricFieldNames.contains("payload"))
}
