import Foundation
import ProductContracts

enum LANBridgeError: Error {
    case invalidResponse
    case rejected(Int)
}

private final class LocalTLSDelegate: NSObject, URLSessionDelegate,
    @unchecked Sendable {
    func urlSession(
        _ session: URLSession,
        didReceive challenge: URLAuthenticationChallenge
    ) async -> (URLSession.AuthChallengeDisposition, URLCredential?) {
        guard challenge.protectionSpace.authenticationMethod ==
                NSURLAuthenticationMethodServerTrust,
              let trust = challenge.protectionSpace.serverTrust else {
            return (.performDefaultHandling, nil)
        }
        return (.useCredential, URLCredential(trust: trust))
    }
}

final class LANBridge {
    private let baseURL: URL
    private let pairingCode: String
    private var lastActionSequence: UInt64 = 0
    private let tlsDelegate = LocalTLSDelegate()
    private lazy var session = URLSession(
        configuration: .ephemeral,
        delegate: tlsDelegate,
        delegateQueue: nil
    )

    init(baseURL: URL, pairingCode: String) {
        self.baseURL = baseURL
        self.pairingCode = pairingCode
    }

    func post(_ snapshot: CompanionSnapshot) async throws {
        var request = URLRequest(
            url: baseURL.appending(path: "api/v1/companion/status")
        )
        request.httpMethod = "POST"
        request.setValue(pairingCode, forHTTPHeaderField: "X-Cardputer-Pairing")
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = try JSONEncoder().encode(snapshot)
        let (_, response) = try await session.data(for: request)
        try check(response)
    }

    func pollAction() async throws -> RemoteAction {
        var request = URLRequest(
            url: baseURL.appending(path: "api/v1/companion/action")
        )
        request.setValue(pairingCode, forHTTPHeaderField: "X-Cardputer-Pairing")
        let (data, response) = try await session.data(for: request)
        try check(response)
        let envelope = try JSONDecoder().decode(
            RemoteActionEnvelope.self,
            from: data
        )
        guard envelope.sequence > lastActionSequence else { return .none }
        lastActionSequence = envelope.sequence
        return envelope.action
    }

    private func check(_ response: URLResponse) throws {
        guard let http = response as? HTTPURLResponse else {
            throw LANBridgeError.invalidResponse
        }
        guard (200..<300).contains(http.statusCode) else {
            throw LANBridgeError.rejected(http.statusCode)
        }
    }
}
