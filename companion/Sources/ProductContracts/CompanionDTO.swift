import Foundation

public enum PetState: String, Codable, CaseIterable, Sendable {
    case idle
    case working
    case waiting
    case review
    case failed

    public static func resolve(
        sessionState: String,
        flags: [String]
    ) -> PetState {
        let normalized = sessionState
            .lowercased()
            .replacingOccurrences(of: "_", with: "")
        if ["failed", "error", "cancelled"].contains(normalized) {
            return .failed
        }
        if flags.contains("waitingOnApproval") ||
            flags.contains("waitingOnUserInput") {
            return .waiting
        }
        if normalized == "review" || flags.contains("review") {
            return .review
        }
        if ["active", "running", "inprogress"].contains(normalized) {
            return .working
        }
        return .idle
    }
}

public struct CompanionSnapshot: Codable, Equatable, Sendable {
    public let type: String
    public let sequence: UInt64
    public let sessionID: String
    public let title: String
    public let cwd: String
    public let state: String
    public let approvals: UInt8
    public let inputs: UInt8
    public let petID: String
    public let petDigest: String
    public let petState: PetState

    enum CodingKeys: String, CodingKey {
        case type, sequence, title, cwd, state, approvals, inputs
        case sessionID = "session_id"
        case petID = "pet_id"
        case petDigest = "pet_digest"
        case petState = "pet_state"
    }

    public init(
        sequence: UInt64,
        sessionID: String,
        title: String,
        cwd: String,
        state: String,
        approvals: UInt8,
        inputs: UInt8,
        petID: String = "",
        petDigest: String = "",
        petState: PetState = .idle
    ) {
        self.type = "snapshot"
        self.sequence = sequence
        self.sessionID = sessionID
        self.title = title
        self.cwd = cwd
        self.state = state
        self.approvals = approvals
        self.inputs = inputs
        self.petID = petID
        self.petDigest = petDigest
        self.petState = petState
    }

    public func hasSameContent(as other: CompanionSnapshot) -> Bool {
        sessionID == other.sessionID &&
            title == other.title &&
            cwd == other.cwd &&
            state == other.state &&
            approvals == other.approvals &&
            inputs == other.inputs &&
            petID == other.petID &&
            petDigest == other.petDigest &&
            petState == other.petState
    }

    public func withSequence(_ value: UInt64) -> CompanionSnapshot {
        CompanionSnapshot(
            sequence: value,
            sessionID: sessionID,
            title: title,
            cwd: cwd,
            state: state,
            approvals: approvals,
            inputs: inputs,
            petID: petID,
            petDigest: petDigest,
            petState: petState
        )
    }

    public func withPet(id: String, digest: String) -> CompanionSnapshot {
        CompanionSnapshot(
            sequence: sequence,
            sessionID: sessionID,
            title: title,
            cwd: cwd,
            state: state,
            approvals: approvals,
            inputs: inputs,
            petID: id,
            petDigest: digest,
            petState: petState
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
