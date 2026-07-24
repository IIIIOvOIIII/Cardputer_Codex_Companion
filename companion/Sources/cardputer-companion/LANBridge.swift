import Foundation
import ProductContracts

enum LANBridgeError: Error {
    case invalidResponse
    case rejected(Int)
    case curlFailed(Int32, String)
}

final class LANBridge {
    private let baseURL: URL
    private let pairingCode: String

    init(baseURL: URL, pairingCode: String) {
        self.baseURL = baseURL
        self.pairingCode = pairingCode
    }

    func post(_ snapshot: CompanionSnapshot) async throws {
        let data = try JSONEncoder().encode(snapshot)
        guard let body = String(data: data, encoding: .utf8) else {
            throw LANBridgeError.invalidResponse
        }
        _ = try runCurl(
            method: "POST",
            path: "api/v1/companion/status",
            body: body
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

    private func runCurl(
        method: String,
        path: String,
        body: String? = nil
    ) throws -> Data {
        let process = Process()
        let input = Pipe()
        let output = Pipe()
        let error = Pipe()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/curl")
        process.arguments = ["--config", "-"]
        process.standardInput = input
        process.standardOutput = output
        process.standardError = error
        try process.run()
        try input.fileHandleForWriting.write(
            contentsOf: Data(curlConfig(method: method, path: path, body: body).utf8)
        )
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

    private func curlConfig(method: String, path: String, body: String?) -> String {
        var lines = [
            "silent",
            "show-error",
            "insecure",
            "globoff",
            "connect-timeout = 5",
            "max-time = 5",
            "request = \(curlQuote(method))",
            "url = \(curlQuote(baseURL.appending(path: path).absoluteString))",
            "header = \(curlQuote("X-Cardputer-Pairing: \(pairingCode)"))",
            "write-out = \(curlQuote("\n%{http_code}"))",
        ]
        if let body {
            lines.append("header = \(curlQuote("Content-Type: application/json"))")
            lines.append("data-binary = \(curlQuote(body))")
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
