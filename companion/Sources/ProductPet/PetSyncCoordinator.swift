import CryptoKit
import Foundation

public struct DevicePetTransaction: Codable, Equatable, Sendable {
    public let active: Bool
    public let id: String
    public let received: Int
    public let expected: Int

    public init(active: Bool, id: String, received: Int, expected: Int) {
        self.active = active
        self.id = id
        self.received = received
        self.expected = expected
    }
}

public struct DevicePetStatus: Codable, Equatable, Sendable {
    public let petID: String
    public let digest: String
    public let formatVersion: UInt16
    public let storageUsed: Int
    public let transaction: DevicePetTransaction
    public let lastResult: String

    enum CodingKeys: String, CodingKey {
        case digest, transaction
        case petID = "pet_id"
        case formatVersion = "format_version"
        case storageUsed = "storage_used"
        case lastResult = "last_result"
    }

    public init(
        petID: String,
        digest: String,
        formatVersion: UInt16,
        storageUsed: Int,
        transaction: DevicePetTransaction,
        lastResult: String
    ) {
        self.petID = petID
        self.digest = digest
        self.formatVersion = formatVersion
        self.storageUsed = storageUsed
        self.transaction = transaction
        self.lastResult = lastResult
    }
}

public struct PetUploadReceipt: Codable, Equatable, Sendable {
    public let transactionID: String
    public let received: Int

    enum CodingKeys: String, CodingKey {
        case received
        case transactionID = "transaction_id"
    }

    public init(transactionID: String, received: Int) {
        self.transactionID = transactionID
        self.received = received
    }
}

public protocol PetDeviceClient: Sendable {
    func petStatus() async throws -> DevicePetStatus
    func beginPetUpload(_ bundle: PetBundle) async throws -> PetUploadReceipt
    func putPetChunk(
        transactionID: String,
        offset: Int,
        data: Data
    ) async throws
    func commitPetUpload(transactionID: String) async throws -> DevicePetStatus
}

public struct PetSyncResult: Equatable, Sendable {
    public let petID: String
    public let digest: String
    public let errorCode: String?

    public init(petID: String, digest: String, errorCode: String?) {
        self.petID = petID
        self.digest = digest
        self.errorCode = errorCode
    }
}

public actor PetSyncCoordinator {
    public typealias SourceLoader = () throws -> PetSource
    public typealias Transcode = (PetSource) throws -> PetBundle

    private let loadSource: SourceLoader
    private let transcode: Transcode
    private var cachedInputDigest = ""
    private var cachedBundle: PetBundle?
    private var lastSuccess = PetSyncResult(
        petID: "",
        digest: "",
        errorCode: nil
    )

    public init(reader: PetSelectionReader, transcoder: PetTranscoder) {
        self.loadSource = { try reader.selectedSource() }
        self.transcode = { try transcoder.transcode($0) }
    }

    public init(
        loadSource: @escaping SourceLoader,
        transcode: @escaping Transcode
    ) {
        self.loadSource = loadSource
        self.transcode = transcode
    }

    public func synchronize(
        client: any PetDeviceClient
    ) async -> PetSyncResult {
        do {
            let source = try loadSource()
            let inputDigest = try sourceInputDigest(source)
            let bundle: PetBundle
            if inputDigest == cachedInputDigest, let cachedBundle {
                bundle = cachedBundle
            } else {
                bundle = try transcode(source)
                cachedInputDigest = inputDigest
                cachedBundle = bundle
            }

            var status = try await client.petStatus()
            if status.digest == bundle.contentDigestHex {
                lastSuccess = PetSyncResult(
                    petID: bundle.petID,
                    digest: bundle.contentDigestHex,
                    errorCode: nil
                )
                return lastSuccess
            }

            let transactionID: String
            var offset: Int
            if status.transaction.active,
               status.transaction.expected == bundle.data.count,
               !status.transaction.id.isEmpty {
                transactionID = status.transaction.id
                offset = status.transaction.received
            } else {
                let receipt = try await client.beginPetUpload(bundle)
                transactionID = receipt.transactionID
                offset = receipt.received
            }
            guard offset >= 0, offset <= bundle.data.count else {
                throw PetSyncError.invalidDeviceOffset
            }
            while offset < bundle.data.count {
                let end = min(offset + 8192, bundle.data.count)
                let chunk = Data(bundle.data[offset..<end])
                do {
                    try await client.putPetChunk(
                        transactionID: transactionID,
                        offset: offset,
                        data: chunk
                    )
                    offset = end
                } catch {
                    status = try await client.petStatus()
                    if status.transaction.id == transactionID,
                       status.transaction.received >= end {
                        offset = status.transaction.received
                    } else {
                        try await client.putPetChunk(
                            transactionID: transactionID,
                            offset: offset,
                            data: chunk
                        )
                        offset = end
                    }
                }
            }
            let committed = try await client.commitPetUpload(
                transactionID: transactionID
            )
            guard committed.digest == bundle.contentDigestHex else {
                throw PetSyncError.commitDigestMismatch
            }
            lastSuccess = PetSyncResult(
                petID: bundle.petID,
                digest: bundle.contentDigestHex,
                errorCode: nil
            )
            return lastSuccess
        } catch {
            return PetSyncResult(
                petID: lastSuccess.petID,
                digest: lastSuccess.digest,
                errorCode: stableErrorCode(error)
            )
        }
    }

    private func sourceInputDigest(_ source: PetSource) throws -> String {
        let sourceData = try Data(contentsOf: source.atlasURL)
        var input = Data(source.id.utf8)
        input.append(source.atlasVersion.rawValue)
        input.append(contentsOf: [0x00, 96, 0x00, 104])
        input.append(contentsOf: [0x01, 0x90])
        input.append(contentsOf: [0x05, 0x08, 0x0d])
        input.append(sourceData)
        return Data(SHA256.hash(data: input)).hexString
    }

    private func stableErrorCode(_ error: Error) -> String {
        switch error {
        case PetSelectionError.sourceNotFound,
             PetSelectionError.missingSelection:
            return "source_not_found"
        case PetSelectionError.invalidAtlas,
             PetSelectionError.invalidManifest,
             PetSelectionError.pathTraversal:
            return "source_invalid"
        case PetSyncError.invalidDeviceOffset:
            return "invalid_device_offset"
        case PetSyncError.commitDigestMismatch:
            return "commit_digest_mismatch"
        default:
            return "sync_failed"
        }
    }
}

private enum PetSyncError: Error {
    case invalidDeviceOffset
    case commitDigestMismatch
}

private extension Data {
    var hexString: String {
        map { String(format: "%02x", $0) }.joined()
    }
}
