import Foundation

public protocol CodexRPCClient: AnyObject {
    func start() throws
    func request(
        method: String,
        params: [String: Any]
    ) throws -> [String: Any]
    func respondToPendingApproval(approved: Bool) throws
}

public enum JSONRPCProcessError: Error {
    case notRunning
    case malformedResponse
    case remoteError(String)
    case responseTooLarge
}

public final class JSONRPCProcess: CodexRPCClient, @unchecked Sendable {
    private let process = Process()
    private let input = Pipe()
    private let output = Pipe()
    private var nextID = 1
    private var pendingApproval: [String: Any]?

    public init(codexExecutable: String = "codex") {
        process.executableURL = URL(fileURLWithPath: "/usr/bin/env")
        process.arguments = [codexExecutable, "app-server", "--listen", "stdio://"]
        process.standardInput = input
        process.standardOutput = output
        process.standardError = FileHandle.nullDevice
    }

    deinit {
        if process.isRunning {
            process.terminate()
        }
    }

    public func start() throws {
        guard !process.isRunning else { return }
        try process.run()
        _ = try request(
            method: "initialize",
            params: [
                "clientInfo": [
                    "name": "cardputer-companion",
                    "title": "Cardputer Codex Companion",
                    "version": "1.2.0"
                ],
                "capabilities": ["experimentalApi": false]
            ]
        )
        try send([
            "jsonrpc": "2.0",
            "method": "initialized",
            "params": [:]
        ])
    }

    public func request(
        method: String,
        params: [String: Any]
    ) throws -> [String: Any] {
        guard process.isRunning else { throw JSONRPCProcessError.notRunning }
        let requestID = nextID
        nextID += 1
        try send([
            "jsonrpc": "2.0",
            "id": requestID,
            "method": method,
            "params": params
        ])
        while true {
            let object = try readObject()
            if let method = object["method"] as? String,
               object["id"] != nil,
               method == "item/commandExecution/requestApproval" ||
                method == "item/fileChange/requestApproval" {
                pendingApproval = object
                continue
            }
            guard let id = object["id"] as? Int, id == requestID else {
                continue
            }
            if let error = object["error"] as? [String: Any] {
                throw JSONRPCProcessError.remoteError(
                    String(describing: error["message"] ?? error)
                )
            }
            guard let result = object["result"] as? [String: Any] else {
                throw JSONRPCProcessError.malformedResponse
            }
            return result
        }
    }

    public func respondToPendingApproval(approved: Bool) throws {
        guard let request = pendingApproval,
              let id = request["id"] else {
            return
        }
        pendingApproval = nil
        try send([
            "jsonrpc": "2.0",
            "id": id,
            "result": ["decision": approved ? "accept" : "decline"]
        ])
    }

    private func send(_ object: [String: Any]) throws {
        var data = try JSONSerialization.data(withJSONObject: object)
        data.append(0x0A)
        try input.fileHandleForWriting.write(contentsOf: data)
    }

    private func readObject() throws -> [String: Any] {
        var line = Data()
        while line.count <= 4 * 1024 * 1024 {
            let byte = output.fileHandleForReading.readData(ofLength: 1)
            if byte.isEmpty {
                throw JSONRPCProcessError.notRunning
            }
            if byte[byte.startIndex] == 0x0A {
                guard !line.isEmpty,
                      let object = try JSONSerialization.jsonObject(with: line)
                        as? [String: Any] else {
                    continue
                }
                return object
            }
            line.append(byte)
        }
        throw JSONRPCProcessError.responseTooLarge
    }
}
