import CryptoKit
import Darwin
import Foundation
import ProductContracts
import ProductPet

enum LANBridgeError: Error {
    case invalidResponse
    case rejected(Int)
    case curlFailed(Int32, String)
}

final class LANBridge: @unchecked Sendable, PetDeviceClient {
    private let baseURL: URL
    private let pairingCode: String

    init(baseURL: URL, pairingCode: String) {
        self.baseURL = baseURL
        self.pairingCode = pairingCode
    }

    func post(_ snapshot: CompanionSnapshot) async throws {
        let data = try JSONEncoder().encode(snapshot)
        _ = try runCurl(
            method: "POST",
            path: "api/v1/companion/status",
            headers: ["Content-Type": "application/json"],
            body: data
        )
    }

    func pollAction() async throws -> RemoteActionEnvelope {
        let data = try runCurl(
            method: "GET",
            path: "api/v1/companion/action"
        )
        return try JSONDecoder().decode(
            RemoteActionEnvelope.self,
            from: data
        )
    }

    func petStatus() async throws -> DevicePetStatus {
        let data = try runCurl(
            method: "GET",
            path: "api/v1/companion/pet"
        )
        return try JSONDecoder().decode(DevicePetStatus.self, from: data)
    }

    func beginPetUpload(_ bundle: PetBundle) async throws -> PetUploadReceipt {
        struct Request: Encodable {
            let petID: String
            let formatVersion: UInt16
            let length: Int
            let sha256: String

            enum CodingKeys: String, CodingKey {
                case length, sha256
                case petID = "pet_id"
                case formatVersion = "format_version"
            }
        }
        let body = try JSONEncoder().encode(
            Request(
                petID: bundle.petID,
                formatVersion: PetBundle.schemaVersion,
                length: bundle.data.count,
                sha256: bundle.uploadDigestHex
            )
        )
        let data = try runCurl(
            method: "POST",
            path: "api/v1/companion/pet/begin",
            headers: ["Content-Type": "application/json"],
            body: body,
            timeout: 15
        )
        return try JSONDecoder().decode(PetUploadReceipt.self, from: data)
    }

    func putPetChunk(
        transactionID: String,
        offset: Int,
        data: Data
    ) async throws {
        let digest = Data(SHA256.hash(data: data))
            .map { String(format: "%02x", $0) }
            .joined()
        _ = try runCurl(
            method: "PUT",
            path: "api/v1/companion/pet/chunk",
            headers: [
                "Content-Type": "application/octet-stream",
                "X-Pet-Transaction": transactionID,
                "X-Pet-Offset": String(offset),
                "X-Pet-Chunk-SHA256": digest
            ],
            body: data,
            timeout: 15
        )
    }

    func commitPetUpload(
        transactionID: String
    ) async throws -> DevicePetStatus {
        let body = try JSONEncoder().encode(
            ["transaction_id": transactionID]
        )
        let data = try runCurl(
            method: "POST",
            path: "api/v1/companion/pet/commit",
            headers: ["Content-Type": "application/json"],
            body: body,
            timeout: 15
        )
        return try JSONDecoder().decode(DevicePetStatus.self, from: data)
    }

    private func runCurl(
        method: String,
        path: String,
        headers: [String: String] = [:],
        body: Data? = nil,
        timeout: Int = 5
    ) throws -> Data {
        let configURL = FileManager.default.temporaryDirectory
            .appending(path: "cardputer-curl-\(UUID().uuidString).conf")
        let descriptor = open(
            configURL.path,
            O_WRONLY | O_CREAT | O_EXCL,
            S_IRUSR | S_IWUSR
        )
        guard descriptor >= 0 else {
            throw LANBridgeError.invalidResponse
        }
        let config = curlConfig(
            method: method,
            path: path,
            headers: headers,
            hasBody: body != nil,
            timeout: timeout
        )
        let configBytes = Array(config.utf8)
        let wrote = configBytes.withUnsafeBytes {
            Darwin.write(descriptor, $0.baseAddress, $0.count)
        }
        Darwin.close(descriptor)
        guard wrote == configBytes.count else {
            try? FileManager.default.removeItem(at: configURL)
            throw LANBridgeError.invalidResponse
        }
        defer { try? FileManager.default.removeItem(at: configURL) }

        let process = Process()
        let input = Pipe()
        let output = Pipe()
        let error = Pipe()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/curl")
        process.arguments = ["--config", configURL.path]
        process.standardInput = input
        process.standardOutput = output
        process.standardError = error
        defer {
            try? input.fileHandleForWriting.close()
            try? output.fileHandleForReading.close()
            try? error.fileHandleForReading.close()
        }
        try process.run()
        if let body {
            try input.fileHandleForWriting.write(contentsOf: body)
        }
        try input.fileHandleForWriting.close()
        let response = output.fileHandleForReading.readDataToEndOfFile()
        let stderr = error.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        guard process.terminationStatus == 0 else {
            throw LANBridgeError.curlFailed(
                process.terminationStatus,
                String(data: stderr, encoding: .utf8) ?? ""
            )
        }
        guard let separator = response.lastIndex(of: 0x0A),
              let status = Int(
                String(data: response[response.index(after: separator)...],
                       encoding: .utf8) ?? ""
              ) else {
            throw LANBridgeError.invalidResponse
        }
        guard (200..<300).contains(status) else {
            throw LANBridgeError.rejected(status)
        }
        return Data(response[..<separator])
    }

    private func curlConfig(
        method: String,
        path: String,
        headers: [String: String],
        hasBody: Bool,
        timeout: Int
    ) -> String {
        var lines = [
            "silent",
            "show-error",
            "insecure",
            "globoff",
            "connect-timeout = 5",
            "max-time = \(timeout)",
            "request = \(curlQuote(method))",
            "url = \(curlQuote(baseURL.appending(path: path).absoluteString))",
            "header = \(curlQuote("X-Cardputer-Pairing: \(pairingCode)"))",
            "write-out = \(curlQuote("\n%{http_code}"))",
        ]
        for key in headers.keys.sorted() {
            lines.append(
                "header = \(curlQuote("\(key): \(headers[key]!)"))"
            )
        }
        if hasBody {
            lines.append("data-binary = \(curlQuote("@-"))")
        }
        return lines.joined(separator: "\n") + "\n"
    }

    private func curlQuote(_ value: String) -> String {
        var result = "\""
        for character in value {
            switch character {
            case "\\":
                result += "\\\\"
            case "\"":
                result += "\\\""
            case "\n":
                result += "\\n"
            case "\r":
                result += "\\r"
            case "\t":
                result += "\\t"
            default:
                result.append(character)
            }
        }
        result += "\""
        return result
    }
}
