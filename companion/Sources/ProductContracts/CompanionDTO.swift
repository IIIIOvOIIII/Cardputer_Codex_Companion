import Foundation

public struct CompanionSnapshot: Codable, Equatable, Sendable {
    public let type: String
    public let sequence: UInt64
    public let sessionID: String
    public let title: String
    public let cwd: String
    public let state: String
    public let approvals: UInt8
    public let inputs: UInt8

    enum CodingKeys: String, CodingKey {
        case type, sequence, title, cwd, state, approvals, inputs
        case sessionID = "session_id"
    }

    public init(
        sequence: UInt64,
        sessionID: String,
        title: String,
        cwd: String,
        state: String,
        approvals: UInt8,
        inputs: UInt8
    ) {
        self.type = "snapshot"
        self.sequence = sequence
        self.sessionID = sessionID
        self.title = title
        self.cwd = cwd
        self.state = state
        self.approvals = approvals
        self.inputs = inputs
    }

    public func hasSameContent(as other: CompanionSnapshot) -> Bool {
        sessionID == other.sessionID &&
            title == other.title &&
            cwd == other.cwd &&
            state == other.state &&
            approvals == other.approvals &&
            inputs == other.inputs
    }

    public func withSequence(_ value: UInt64) -> CompanionSnapshot {
        CompanionSnapshot(
            sequence: value,
            sessionID: sessionID,
            title: title,
            cwd: cwd,
            state: state,
            approvals: approvals,
            inputs: inputs
        )
    }
}

public enum RemoteAction: String, Codable, Sendable {
    case none
    case selectNext = "select_next"
    case selectPrevious = "select_previous"
    case newSession = "new"
    case interrupt
    case approve
    case reject
    case provideInput = "provide_input"
}

public struct RemoteActionEnvelope: Codable, Sendable {
    public let sequence: UInt64
    public let action: RemoteAction
    public let needsSnapshot: Bool

    enum CodingKeys: String, CodingKey {
        case sequence, action
        case needsSnapshot = "needs_snapshot"
    }
}
