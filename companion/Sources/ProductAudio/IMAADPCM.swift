import Foundation

public enum IMAADPCMError: Error, Equatable, Sendable {
    case invalidSampleCount
    case invalidBlockLength
    case invalidStepIndex
    case invalidReservedByte
}

public enum IMAADPCM {
    private static let stepTable: [Int] = [
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
        19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
        50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
        130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
        337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
        2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
        5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
        15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
    ]

    private static let indexAdjustment: [Int] = [
        -1, -1, -1, -1, 2, 4, 6, 8,
        -1, -1, -1, -1, 2, 4, 6, 8
    ]

    public static func decode(
        block: Data,
        sampleCount: Int
    ) throws -> [Int16] {
        guard sampleCount > 0 else {
            throw IMAADPCMError.invalidSampleCount
        }
        let expectedLength = 4 + sampleCount / 2
        guard block.count == expectedLength else {
            throw IMAADPCMError.invalidBlockLength
        }
        let bytes = [UInt8](block)
        var predictor = Int(Int16(bitPattern:
            UInt16(bytes[0]) | (UInt16(bytes[1]) << 8)
        ))
        var stepIndex = Int(bytes[2])
        guard stepIndex < stepTable.count else {
            throw IMAADPCMError.invalidStepIndex
        }
        guard bytes[3] == 0 else {
            throw IMAADPCMError.invalidReservedByte
        }

        var samples = [Int16]()
        samples.reserveCapacity(sampleCount)
        samples.append(Int16(predictor))
        for sampleIndex in 1..<sampleCount {
            let packed = bytes[4 + (sampleIndex - 1) / 2]
            let code = sampleIndex.isMultiple(of: 2)
                ? Int(packed >> 4)
                : Int(packed & 0x0F)
            let step = stepTable[stepIndex]
            var delta = step >> 3
            if code & 4 != 0 { delta += step }
            if code & 2 != 0 { delta += step >> 1 }
            if code & 1 != 0 { delta += step >> 2 }
            predictor += code & 8 != 0 ? -delta : delta
            predictor = min(32_767, max(-32_768, predictor))
            stepIndex = min(
                88,
                max(0, stepIndex + indexAdjustment[code])
            )
            samples.append(Int16(predictor))
        }
        return samples
    }
}
