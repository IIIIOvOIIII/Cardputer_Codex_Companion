import Darwin
import Foundation

public struct PairingMigration: Equatable, Sendable {
    public let nextPairing: String
    public let pinRevision: UInt32

    public init(nextPairing: String, pinRevision: UInt32) {
        self.nextPairing = nextPairing
        self.pinRevision = pinRevision
    }
}

public enum PairingConfigWriterError: Error {
    case invalidPairing
    case invalidConfig
    case writeFailed
}

public enum PairingConfigWriter {
    public static func persist(
        _ migration: PairingMigration,
        to configURL: URL
    ) throws {
        guard validPairing(migration.nextPairing) else {
            throw PairingConfigWriterError.invalidPairing
        }
        let data = try Data(contentsOf: configURL)
        guard var object = try JSONSerialization.jsonObject(with: data)
                as? [String: Any] else {
            throw PairingConfigWriterError.invalidConfig
        }
        let existing = (object["pin_revision"] as? NSNumber)?
            .uint32Value ?? 0
        guard migration.pinRevision > existing else {
            return
        }
        object["pairing"] = migration.nextPairing
        object["pin_revision"] = migration.pinRevision
        let replacement = try JSONSerialization.data(
            withJSONObject: object,
            options: [.prettyPrinted, .sortedKeys]
        )
        let temporary = configURL
            .deletingLastPathComponent()
            .appending(
                path: ".\(configURL.lastPathComponent).\(UUID().uuidString).tmp"
            )
        let descriptor = Darwin.open(
            temporary.path,
            O_WRONLY | O_CREAT | O_EXCL,
            S_IRUSR | S_IWUSR
        )
        guard descriptor >= 0 else {
            throw PairingConfigWriterError.writeFailed
        }
        var succeeded = false
        defer {
            Darwin.close(descriptor)
            if !succeeded {
                try? FileManager.default.removeItem(at: temporary)
            }
        }
        try replacement.withUnsafeBytes { bytes in
            var written = 0
            while written < bytes.count {
                let result = Darwin.write(
                    descriptor,
                    bytes.baseAddress!.advanced(by: written),
                    bytes.count - written
                )
                guard result > 0 else {
                    throw PairingConfigWriterError.writeFailed
                }
                written += result
            }
        }
        guard Darwin.fsync(descriptor) == 0,
              Darwin.rename(temporary.path, configURL.path) == 0,
              Darwin.chmod(configURL.path, S_IRUSR | S_IWUSR) == 0 else {
            throw PairingConfigWriterError.writeFailed
        }
        succeeded = true
    }

    public static func validPairing(_ value: String) -> Bool {
        let bytes = Array(value.utf8)
        return bytes.count == 8 &&
            bytes.allSatisfy { (48...57).contains($0) }
    }
}

