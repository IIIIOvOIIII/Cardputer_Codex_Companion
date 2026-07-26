import Foundation
import ProductAudio

func testDecodesCommittedGoldenVectorsExactly() throws {
    let fixture = try AudioFixture.load()

    for vector in fixture.codecVectors {
        let block = try AudioFixture.data(from: vector.blockHex)
        let samples = try IMAADPCM.decode(
            block: block,
            sampleCount: vector.sampleCount
        )
        assert(samples == vector.decodedSamples, vector.name)
    }
}

func testRejectsTruncatedAndUnsupportedSampleCounts() {
    expectThrows(IMAADPCMError.invalidBlockLength) {
        _ = try IMAADPCM.decode(
            block: Data(repeating: 0, count: 3),
            sampleCount: 9
        )
    }
    expectThrows(IMAADPCMError.invalidSampleCount) {
        _ = try IMAADPCM.decode(
            block: Data(repeating: 0, count: 4),
            sampleCount: 0
        )
    }
}
