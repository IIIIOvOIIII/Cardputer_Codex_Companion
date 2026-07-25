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
    public let model: String?
    public let thinkingLevel: String?
    public let fast: Bool?
    public let limits: [CodexLimitUsage]?

    enum CodingKeys: String, CodingKey {
        case type, sequence, title, cwd, state, approvals, inputs
        case sessionID = "session_id"
        case petID = "pet_id"
        case petDigest = "pet_digest"
        case petState = "pet_state"
        case model, fast, limits
        case thinkingLevel = "thinking_level"
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
        petState: PetState = .idle,
        model: String? = nil,
        thinkingLevel: String? = nil,
        fast: Bool? = nil,
        limits: [CodexLimitUsage]? = nil
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
        self.model = model
        self.thinkingLevel = thinkingLevel
        self.fast = fast
        self.limits = limits.map { Array($0.prefix(4)) }
    }

    public init(from decoder: Decoder) throws {
        let values = try decoder.container(keyedBy: CodingKeys.self)
        type = try values.decodeIfPresent(String.self, forKey: .type)
            ?? "snapshot"
        sequence = try values.decode(UInt64.self, forKey: .sequence)
        sessionID = try values.decode(String.self, forKey: .sessionID)
        title = try values.decode(String.self, forKey: .title)
        cwd = try values.decode(String.self, forKey: .cwd)
        state = try values.decode(String.self, forKey: .state)
        approvals = try values.decode(UInt8.self, forKey: .approvals)
        inputs = try values.decode(UInt8.self, forKey: .inputs)
        petID = try values.decodeIfPresent(String.self, forKey: .petID) ?? ""
        petDigest = try values.decodeIfPresent(String.self, forKey: .petDigest)
            ?? ""
        petState = try values.decodeIfPresent(PetState.self, forKey: .petState)
            ?? .idle
        model = try values.decodeIfPresent(String.self, forKey: .model)
        thinkingLevel = try values.decodeIfPresent(
            String.self,
            forKey: .thinkingLevel
        )
        fast = try values.decodeIfPresent(Bool.self, forKey: .fast)
        limits = try values.decodeIfPresent(
            [CodexLimitUsage].self,
            forKey: .limits
        ).map { Array($0.prefix(4)) }
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
            petState == other.petState &&
            model == other.model &&
            thinkingLevel == other.thinkingLevel &&
            fast == other.fast &&
            limits == other.limits
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
            petState: petState,
            model: model,
            thinkingLevel: thinkingLevel,
            fast: fast,
            limits: limits
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
            petState: petState,
            model: model,
            thinkingLevel: thinkingLevel,
            fast: fast,
            limits: limits
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
