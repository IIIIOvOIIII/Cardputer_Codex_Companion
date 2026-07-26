import Foundation

public final class AdaptiveResampler {
    public let outputRate = 48_000
    public let outputChannelCount = 1
    public private(set) var watermarkCorrectionPPM: Double = 0
    private var fractionalOutputCount: Double = 0

    public init() {}

    public func setWatermarkCorrection(partsPerMillion: Double) {
        watermarkCorrectionPPM = min(500, max(-500, partsPerMillion))
    }

    public func reset() {
        fractionalOutputCount = 0
    }

    public func convert(
        _ input: [Int16],
        sourceRate: Int
    ) -> [Float] {
        guard !input.isEmpty, sourceRate > 0 else {
            return []
        }
        let correction = 1 + watermarkCorrectionPPM / 1_000_000
        let exactOutputCount =
            Double(input.count) * Double(outputRate) /
            Double(sourceRate) * correction + fractionalOutputCount
        let outputCount = max(0, Int(exactOutputCount.rounded(.down)))
        fractionalOutputCount = exactOutputCount - Double(outputCount)
        guard outputCount > 0 else {
            return []
        }

        var output = [Float]()
        output.reserveCapacity(outputCount)
        let scale = Double(input.count) / Double(outputCount)
        for outputIndex in 0..<outputCount {
            let sourcePosition =
                (Double(outputIndex) + 0.5) * scale - 0.5
            let lower = max(
                0,
                min(input.count - 1, Int(floor(sourcePosition)))
            )
            let upper = min(input.count - 1, lower + 1)
            let fraction = max(0, min(1, sourcePosition - Double(lower)))
            let interpolated =
                Double(input[lower]) +
                (Double(input[upper]) - Double(input[lower])) * fraction
            output.append(Float(interpolated / 32_768))
        }
        return output
    }
}
