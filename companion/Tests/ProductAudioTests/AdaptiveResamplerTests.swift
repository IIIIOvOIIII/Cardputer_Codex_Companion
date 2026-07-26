import ProductAudio

func testResamplerProducesExact48kMonoSilence() {
    let resampler = AdaptiveResampler()

    let from24 = resampler.convert(
        [Int16](repeating: 0, count: 24_000),
        sourceRate: 24_000
    )
    assert(from24.count == 48_000)
    assert(from24.allSatisfy { $0.bitPattern == Float.zero.bitPattern })

    resampler.reset()
    let from16 = resampler.convert(
        [Int16](repeating: 0, count: 16_000),
        sourceRate: 16_000
    )
    assert(from16.count == 48_000)
    assert(from16.allSatisfy { $0.bitPattern == Float.zero.bitPattern })
    assert(resampler.outputChannelCount == 1)
}

func testResamplerBoundsWatermarkCorrectionTo500PPM() {
    let resampler = AdaptiveResampler()
    resampler.setWatermarkCorrection(partsPerMillion: 5_000)
    assert(resampler.watermarkCorrectionPPM == 500)
    assert(resampler.convert(
        [Int16](repeating: 0, count: 24_000),
        sourceRate: 24_000
    ).count == 48_024)

    resampler.reset()
    resampler.setWatermarkCorrection(partsPerMillion: -5_000)
    assert(resampler.watermarkCorrectionPPM == -500)
    assert(resampler.convert(
        [Int16](repeating: 0, count: 24_000),
        sourceRate: 24_000
    ).count == 47_976)
    assert(resampler.outputChannelCount == 1)
}
