import Foundation

public enum OperationStatus: String, Codable, Sendable {
    case intent
    case accepted
    case completed
    case failed
    case partial
    case indeterminate

    public var isTerminal: Bool {
        switch self {
        case .completed, .failed, .partial, .indeterminate:
            return true
        case .intent, .accepted:
            return false
        }
    }
}

public struct TextOperationRecord: Codable, Equatable, Sendable {
    public let pairedDeviceID: String
    public let operationID: UUID
    public let payloadSHA256: Data
    public let targetPID: pid_t
    public let targetElementFingerprint: String
    public let totalUTF8Length: Int
    public let postedPrefixLength: Int
    public let verifiedPrefixLength: Int
    public let status: OperationStatus
    public let errorCode: StableErrorCode?
    public let createdAt: Date
    public let updatedAt: Date
    public let expiresAt: Date

    public init(
        pairedDeviceID: String,
        operationID: UUID,
        payloadSHA256: Data,
        targetPID: pid_t,
        targetElementFingerprint: String,
        totalUTF8Length: Int,
        postedPrefixLength: Int,
        verifiedPrefixLength: Int,
        status: OperationStatus,
        errorCode: StableErrorCode?,
        createdAt: Date,
        updatedAt: Date,
        expiresAt: Date
    ) throws {
        guard payloadSHA256.count == 32,
              (0...1024).contains(totalUTF8Length),
              (0...totalUTF8Length).contains(postedPrefixLength),
              (0...postedPrefixLength).contains(verifiedPrefixLength),
              expiresAt > createdAt else {
            throw StableErrorCode.invalidRequest
        }
        self.pairedDeviceID = pairedDeviceID
        self.operationID = operationID
        self.payloadSHA256 = payloadSHA256
        self.targetPID = targetPID
        self.targetElementFingerprint = targetElementFingerprint
        self.totalUTF8Length = totalUTF8Length
        self.postedPrefixLength = postedPrefixLength
        self.verifiedPrefixLength = verifiedPrefixLength
        self.status = status
        self.errorCode = errorCode
        self.createdAt = createdAt
        self.updatedAt = updatedAt
        self.expiresAt = expiresAt
    }
}

public struct InjectionResult: Codable, Equatable, Sendable {
    public let operationID: UUID
    public let status: OperationStatus
    public let postedPrefixLength: Int
    public let verifiedPrefixLength: Int
    public let errorCode: StableErrorCode?

    public init(
        operationID: UUID,
        status: OperationStatus,
        postedPrefixLength: Int,
        verifiedPrefixLength: Int,
        errorCode: StableErrorCode?
    ) {
        self.operationID = operationID
        self.status = status
        self.postedPrefixLength = postedPrefixLength
        self.verifiedPrefixLength = verifiedPrefixLength
        self.errorCode = errorCode
    }
}
