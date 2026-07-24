import Foundation

public struct GateCheck: Codable, Equatable, Sendable {
    public let id: String
    public let status: GateStatus
    public let detail: String

    public init(id: String, status: GateStatus, detail: String) {
        self.id = id
        self.status = status
        self.detail = detail
    }
}

public struct HILEvidence: Codable, Equatable, Sendable {
    public let schemaVersion: String
    public let runID: UUID
    public let startedAt: Date
    public let completedAt: Date
    public let checks: [GateCheck]

    public init(
        schemaVersion: String,
        runID: UUID,
        startedAt: Date,
        completedAt: Date,
        checks: [GateCheck]
    ) {
        self.schemaVersion = schemaVersion
        self.runID = runID
        self.startedAt = startedAt
        self.completedAt = completedAt
        self.checks = checks
    }

    public var overallStatus: GateStatus {
        if checks.contains(where: { $0.status == .fail }) {
            return .fail
        }
        if checks.isEmpty || checks.contains(where: { $0.status == .blocked }) {
            return .blocked
        }
        return .pass
    }
}
