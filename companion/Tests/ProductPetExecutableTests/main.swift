import CoreGraphics
import CryptoKit
import Foundation
import ImageIO
import ProductContracts
import ProductPet

enum HarnessError: Error {
    case failed(String)
}

func expect(_ condition: @autoclosure () -> Bool, _ message: String) throws {
    if !condition() {
        throw HarnessError.failed(message)
    }
}

func temporaryDirectory() throws -> URL {
    let url = FileManager.default.temporaryDirectory
        .appending(path: UUID().uuidString, directoryHint: .isDirectory)
    try FileManager.default.createDirectory(
        at: url,
        withIntermediateDirectories: true
    )
    return url
}

func testSelectionReader() throws {
    let root = try temporaryDirectory()
    defer { try? FileManager.default.removeItem(at: root) }
    try FileManager.default.createDirectory(
        at: root.appending(path: "cache/tui-pets/v1/assets"),
        withIntermediateDirectories: true
    )
    try Data().write(
        to: root.appending(path: "cache/tui-pets/v1/assets/rocky-spritesheet-v2.webp")
    )
    try Data().write(
        to: root.appending(path: "cache/tui-pets/v1/assets/rocky-spritesheet-v11.webp")
    )
    try Data("[tui]\npet = \"rocky\" # selected\n".utf8)
        .write(to: root.appending(path: "config.toml"))

    let reader = PetSelectionReader(
        environment: ["CODEX_HOME": root.path],
        atlasDimensions: { _ in (1536, 1872) }
    )
    let source = try reader.selectedSource()
    try expect(source.id == "rocky", "selected pet ID")
    try expect(
        source.atlasURL.lastPathComponent == "rocky-spritesheet-v11.webp",
        "highest numeric asset version"
    )
    try expect(source.atlasVersion == .v1, "official atlas version")
}

func testCustomTraversalRejected() throws {
    let root = try temporaryDirectory()
    defer { try? FileManager.default.removeItem(at: root) }
    let pet = root.appending(path: "pets/local", directoryHint: .isDirectory)
    try FileManager.default.createDirectory(at: pet, withIntermediateDirectories: true)
    try Data("[tui]\npet = \"local\"\n".utf8)
        .write(to: root.appending(path: "config.toml"))
    try Data(
        """
        {"id":"local","spriteVersionNumber":2,
         "spritesheetPath":"../../outside.webp"}
        """.utf8
    ).write(to: pet.appending(path: "pet.json"))

    let reader = PetSelectionReader(
        environment: ["CODEX_HOME": root.path],
        atlasDimensions: { _ in (1536, 2288) }
    )
    do {
        _ = try reader.selectedSource()
        throw HarnessError.failed("custom path traversal accepted")
    } catch PetSelectionError.pathTraversal {
    }
}

func testCustomSymlinkTraversalRejected() throws {
    let root = try temporaryDirectory()
    defer { try? FileManager.default.removeItem(at: root) }
    let pet = root.appending(path: "pets/local", directoryHint: .isDirectory)
    try FileManager.default.createDirectory(at: pet, withIntermediateDirectories: true)
    try Data("[tui]\npet = \"local\"\n".utf8)
        .write(to: root.appending(path: "config.toml"))
    let outside = root.appending(path: "outside.webp")
    try Data().write(to: outside)
    try FileManager.default.createSymbolicLink(
        at: pet.appending(path: "spritesheet.webp"),
        withDestinationURL: outside
    )
    try Data(
        """
        {"id":"local","spriteVersionNumber":2,
         "spritesheetPath":"spritesheet.webp"}
        """.utf8
    ).write(to: pet.appending(path: "pet.json"))

    let reader = PetSelectionReader(
        environment: ["CODEX_HOME": root.path],
        atlasDimensions: { _ in (1536, 2288) }
    )
    do {
        _ = try reader.selectedSource()
        throw HarnessError.failed("custom symlink traversal accepted")
    } catch PetSelectionError.pathTraversal {
    }
}

func testPetStatePriority() throws {
    try expect(
        PetState.resolve(sessionState: "failed", flags: []) == .failed,
        "failed priority"
    )
    try expect(
        PetState.resolve(
            sessionState: "active",
            flags: ["waitingOnApproval"]
        ) == .waiting,
        "waiting priority"
    )
    try expect(
        PetState.resolve(sessionState: "review", flags: []) == .review,
        "review mapping"
    )
    try expect(
        PetState.resolve(sessionState: "active", flags: []) == .working,
        "working mapping"
    )
    try expect(
        PetState.resolve(sessionState: "new-value", flags: []) == .idle,
        "unknown maps idle"
    )
}

func testSnapshotPetEquality() throws {
    let base = CompanionSnapshot(
        sequence: 1,
        sessionID: "s",
        title: "t",
        cwd: "/tmp",
        state: "active",
        approvals: 0,
        inputs: 0,
        petState: .working
    )
    let rocky = base.withPet(id: "rocky", digest: String(repeating: "a", count: 64))
    let codex = base.withPet(id: "codex", digest: String(repeating: "b", count: 64))
    try expect(!rocky.hasSameContent(as: codex), "pet participates in equality")
    try expect(rocky.withSequence(9).petID == "rocky", "sequence preserves pet")
}

func testFrameEncoding() throws {
    let solid = [UInt16](repeating: 0x1234, count: 96 * 104)
    let encoded = PetBundleEncoder.encodeFrame(solid)
    try expect(encoded.encoding == .rleRGB565, "solid frame uses RLE")
    try expect(encoded.data.count < 96 * 104 * 2, "RLE is smaller")

    let noisy = (0..<(96 * 104)).map { UInt16(truncatingIfNeeded: $0) }
    let raw = PetBundleEncoder.encodeFrame(noisy)
    try expect(raw.encoding == .rawRGB565, "noisy frame uses raw")
    try expect(raw.data.count == 96 * 104 * 2, "raw frame length")
}

func testBundleWireFormat() throws {
    let frame = [UInt16](repeating: 0x07e0, count: 96 * 104)
    let states = Dictionary(
        uniqueKeysWithValues: PetState.allCases.map {
            ($0, Array(repeating: frame, count: 8))
        }
    )
    let first = try PetBundleEncoder.encode(petID: "rocky", frames: states)
    let second = try PetBundleEncoder.encode(petID: "rocky", frames: states)
    try expect(first.data == second.data, "bundle is deterministic")
    try expect(first.data.prefix(4) == Data("CCPT".utf8), "bundle magic")
    try expect(first.data.count <= 820 * 1024, "bundle maximum")
    try expect(first.contentDigestHex.count == 64, "content digest length")
    try expect(first.uploadDigestHex.count == 64, "upload digest length")
    try expect(first.contentDigestHex != first.uploadDigestHex, "digests are distinct")
}

func testAtlasTranscode() throws {
    let width = 1536
    let height = 1872
    var pixels = Data(repeating: 0, count: width * height * 4)
    let rowColors: [(UInt8, UInt8, UInt8)] = [
        (255, 0, 0), (1, 2, 3), (4, 5, 6),
        (7, 8, 9), (10, 11, 12), (0, 0, 255),
        (255, 255, 0), (0, 255, 0), (255, 0, 255)
    ]
    pixels.withUnsafeMutableBytes { raw in
        let bytes = raw.bindMemory(to: UInt8.self)
        for y in 0..<height {
            let color = rowColors[y / 208]
            for x in 0..<width {
                let offset = (y * width + x) * 4
                bytes[offset] = color.0
                bytes[offset + 1] = color.1
                bytes[offset + 2] = color.2
                bytes[offset + 3] = 255
            }
        }
    }
    guard let provider = CGDataProvider(data: pixels as CFData),
          let image = CGImage(
            width: width,
            height: height,
            bitsPerComponent: 8,
            bitsPerPixel: 32,
            bytesPerRow: width * 4,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(
                rawValue: CGImageAlphaInfo.last.rawValue
            ),
            provider: provider,
            decode: nil,
            shouldInterpolate: false,
            intent: .defaultIntent
          ) else {
        throw HarnessError.failed("create atlas fixture")
    }
    let root = try temporaryDirectory()
    defer { try? FileManager.default.removeItem(at: root) }
    let atlas = root.appending(path: "atlas.png")
    guard let destination = CGImageDestinationCreateWithURL(
        atlas as CFURL,
        "public.png" as CFString,
        1,
        nil
    ) else {
        throw HarnessError.failed("create image destination")
    }
    CGImageDestinationAddImage(destination, image, nil)
    try expect(CGImageDestinationFinalize(destination), "write atlas fixture")

    let bundle = try PetTranscoder().transcode(
        PetSource(id: "fixture", atlasURL: atlas, atlasVersion: .v1)
    )
    try expect(bundle.petID == "fixture", "transcoder preserves ID")
    try expect(bundle.data.count > 812, "transcoder emits payload")
    let states: [(PetState, UInt16)] = [
        (.idle, 0xf800),
        (.working, 0x07e0),
        (.waiting, 0xffe0),
        (.review, 0xf81f),
        (.failed, 0x001f)
    ]
    for (state, expectedPixel) in states {
        let stateIndex = PetState.allCases.firstIndex(of: state)!
        let frameRecord = 172 + stateIndex * 8 * 16
        let payloadOffset = Int(bundle.data.uint32LE(at: frameRecord + 4))
        let encoding = bundle.data[frameRecord]
        try expect(encoding == PetFrameEncoding.rleRGB565.rawValue, "solid row RLE")
        try expect(
            bundle.data.uint16LE(at: payloadOffset + 4) == expectedPixel,
            "state row \(state.rawValue)"
        )
    }
}

actor FakePetDevice: PetDeviceClient {
    var digest = ""
    var transaction = DevicePetTransaction(
        active: false,
        id: "",
        received: 0,
        expected: 0
    )
    var beginCount = 0
    var chunkOffsets: [Int] = []
    var pendingDigest = ""

    func petStatus() async throws -> DevicePetStatus {
        DevicePetStatus(
            petID: digest.isEmpty ? "" : "fixture",
            digest: digest,
            formatVersion: digest.isEmpty ? 0 : 1,
            storageUsed: 0,
            transaction: transaction,
            lastResult: "ok"
        )
    }

    func beginPetUpload(_ bundle: PetBundle) async throws -> PetUploadReceipt {
        beginCount += 1
        pendingDigest = bundle.contentDigestHex
        transaction = DevicePetTransaction(
            active: true,
            id: "tx",
            received: 0,
            expected: bundle.data.count
        )
        return PetUploadReceipt(transactionID: "tx", received: 0)
    }

    func putPetChunk(
        transactionID: String,
        offset: Int,
        data: Data
    ) async throws {
        chunkOffsets.append(offset)
        transaction = DevicePetTransaction(
            active: true,
            id: transactionID,
            received: offset + data.count,
            expected: transaction.expected
        )
    }

    func commitPetUpload(transactionID: String) async throws -> DevicePetStatus {
        digest = pendingDigest
        transaction = DevicePetTransaction(
            active: false,
            id: "",
            received: 0,
            expected: 0
        )
        return try await petStatus()
    }

    func observations() -> (Int, [Int], String) {
        (beginCount, chunkOffsets, digest)
    }
}

final class InvocationCounter: @unchecked Sendable {
    private let lock = NSLock()
    private var value = 0

    func increment() {
        lock.lock()
        value += 1
        lock.unlock()
    }

    func read() -> Int {
        lock.lock()
        defer { lock.unlock() }
        return value
    }
}

func testPetSyncCoordinator() async throws {
    let frame = [UInt16](repeating: 0x07e0, count: 96 * 104)
    let states = Dictionary(
        uniqueKeysWithValues: PetState.allCases.map {
            ($0, Array(repeating: frame, count: 8))
        }
    )
    let bundle = try PetBundleEncoder.encode(petID: "fixture", frames: states)
    let root = try temporaryDirectory()
    defer { try? FileManager.default.removeItem(at: root) }
    let sourceURL = root.appending(path: "source.webp")
    try Data("source-v1".utf8).write(to: sourceURL)
    let source = PetSource(id: "fixture", atlasURL: sourceURL, atlasVersion: .v1)
    let coordinator = PetSyncCoordinator(
        loadSource: { source },
        transcode: { _ in bundle }
    )
    let device = FakePetDevice()

    let first = await coordinator.synchronize(client: device)
    try expect(first.digest == bundle.contentDigestHex, "sync publishes digest")
    let firstObservation = await device.observations()
    try expect(firstObservation.0 == 1, "one begin")
    try expect(
        firstObservation.1 == [0, 8192, 16384, 24576],
        "bundle uses sequential 8192-byte chunks"
    )

    let second = await coordinator.synchronize(client: device)
    let secondObservation = await device.observations()
    try expect(second.digest == first.digest, "same digest retained")
    try expect(secondObservation.0 == 1, "matching digest skips upload")
}

func testPetSyncBacksOffFailedInputUntilSourceChanges() async throws {
    let root = try temporaryDirectory()
    defer { try? FileManager.default.removeItem(at: root) }
    let sourceURL = root.appending(path: "source.webp")
    try Data("broken-v1".utf8).write(to: sourceURL)
    let source = PetSource(id: "fixture", atlasURL: sourceURL, atlasVersion: .v1)
    let counter = InvocationCounter()
    let coordinator = PetSyncCoordinator(
        loadSource: { source },
        transcode: { _ in
            counter.increment()
            throw PetSelectionError.invalidAtlas
        }
    )
    let device = FakePetDevice()

    let first = await coordinator.synchronize(client: device)
    let second = await coordinator.synchronize(client: device)
    try expect(first.errorCode == "source_invalid", "stable transcode error")
    try expect(second.errorCode == "source_invalid", "cached transcode error")
    try expect(counter.read() == 1, "unchanged failed input is not transcoded again")

    try Data("broken-v2".utf8).write(to: sourceURL)
    _ = await coordinator.synchronize(client: device)
    try expect(counter.read() == 2, "changed failed input retries transcode")
}

@main
struct ProductPetHarness {
    static func main() async throws {
        try testSelectionReader()
        try testCustomTraversalRejected()
        try testCustomSymlinkTraversalRejected()
        try testPetStatePriority()
        try testSnapshotPetEquality()
        try testFrameEncoding()
        try testBundleWireFormat()
        try testAtlasTranscode()
        try await testPetSyncCoordinator()
        try await testPetSyncBacksOffFailedInputUntilSourceChanges()
        print("product-pet-tests: PASS")
    }
}

private extension Data {
    func uint16LE(at offset: Int) -> UInt16 {
        UInt16(self[offset]) | (UInt16(self[offset + 1]) << 8)
    }

    func uint32LE(at offset: Int) -> UInt32 {
        UInt32(self[offset]) |
            (UInt32(self[offset + 1]) << 8) |
            (UInt32(self[offset + 2]) << 16) |
            (UInt32(self[offset + 3]) << 24)
    }
}
